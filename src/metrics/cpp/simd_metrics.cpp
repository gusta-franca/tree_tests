#include <bit>
#include <cmath>
#include "csv_index.h"
#include "hashmap/hashes/murmurhasher.hpp"
#include "hashmap/hashes/xxhasher.hpp"
#include "hashmap/hashmaps/bucketing_simd.hpp"
#include "metrics.h"
#include "simd_metrics.h"

template <size_t N>
using MurmurT = hashmap::hashing::MurmurHasher<std::array<uint32_t, N>, false>;

template <size_t N>
using XXHashT = hashmap::hashing::XXHasher<std::array<uint32_t, N>, false>;

template <size_t N, typename HasherX, typename HasherY, typename HasherXY>
Results execute(const ColumnarData& data, const std::vector<size_t>& lhs_indices, size_t rhs_idx, size_t est_xy_card) {
    std::chrono::duration<double> total_build_time(0);
    std::chrono::duration<double> total_compute_time(0);
    size_t peak_memory_b = 0;
    
    Results result = {0.0, 0.0, 0.0, 0.0, 0.0};

    auto build_start = std::chrono::steady_clock::now();

    // Arrays holding the X and Y columns; there will be one for every row
    using XKey = std::array<uint32_t, N>;
    using YKey = std::array<uint32_t, 1>;
    using XYKey = std::array<uint32_t, N+1>;

    // Row count will be the size of any entry in data.columns, as it is columnar
    size_t num_rows = data.columns[0].size();

    // Initial table size; if needed, BucketingSIMDHashTable will double it
    uint64_t table_size = std::bit_ceil(est_xy_card);

    // Counting XY from data
    hashmap::hashmaps::BucketingSIMDHashTable<XYKey, uint32_t, HasherXY> xy_table(table_size, 0);

    for (size_t row = 0; row < num_rows; row++) {
        XYKey xy_key;
        for (size_t i = 0; i < N; i++) {
            xy_key[i] = data.columns[lhs_indices[i]][row];
        }

        // Nth slot is reserved for Y 
        xy_key[N] = data.columns[rhs_idx][row];

        // Increments the current XY count if xy_key is already inserted on the hashtable; otherwise, inserts it with count = 1
        xy_table.increment(xy_key);
    }

    // Couting X and Y from XY
    // X or Y will have sizes at max equal to xy_table.size()
    uint64_t max_size = std::bit_ceil(xy_table.get_current_size());
    hashmap::hashmaps::BucketingSIMDHashTable<XKey, uint32_t, HasherX> x_table(max_size, 0);
    hashmap::hashmaps::BucketingSIMDHashTable<YKey, uint32_t, HasherY> y_table(max_size, 0);

    xy_table.each([&](const auto& kv_pair) {
        const XYKey& xy_key = kv_pair.first;
        uint32_t xy_count = kv_pair.second;;
        
        XKey x_key;
        std::copy(xy_key.begin(), xy_key.begin() + N, x_key.begin());
        x_table.increment(x_key, xy_count);
        
        YKey y_key = {xy_key[N]};
        y_table.increment(y_key, xy_count);
    });

    auto build_end = std::chrono::steady_clock::now();
    total_build_time += (build_end - build_start);

    peak_memory_b = x_table.get_memory_usage() + y_table.get_memory_usage() + xy_table.get_memory_usage();

    auto compute_start = std::chrono::steady_clock::now();
    
    // Compute XY measures
    double pdep_XY = 0.0;
    double shannon_XY = 0.0;
    
    XKey x_key;
    xy_table.each([&](const auto& kv_pair) {
        const XYKey& xy_key = kv_pair.first;
        uint32_t xy_count = kv_pair.second;

        std::copy(xy_key.begin(), xy_key.begin() + N, x_key.begin());

        uint32_t x_count = x_table.lookup(x_key);        

        pdep_XY += (static_cast<double>(xy_count) * xy_count) / x_count;
        shannon_XY += xy_count * std::log2(static_cast<double>(xy_count) / x_count);
    });

    pdep_XY = pdep_XY / static_cast<double>(num_rows);
    shannon_XY = -1.0 * (shannon_XY / num_rows);
   
    // Auxiliary vectors needed for RFI
    std::vector<uint32_t> x_counts;
    std::vector<uint32_t> y_counts;
    x_counts.reserve(x_table.get_current_size());
    y_counts.reserve(y_table.get_current_size());
    
    x_table.each([&](const auto& kv_pair) {
        x_counts.push_back(kv_pair.second);
    });

    // Compute Y measures
    double pdep_Y = 0.0;
    double shannon_Y = 0.0;

    y_table.each([&](const auto& kv_pair) {
        double y_count = static_cast<double>(kv_pair.second);

        pdep_Y += (y_count * y_count);
        shannon_Y += y_count * std::log2(static_cast<double>(y_count) / num_rows);

        y_counts.push_back(kv_pair.second);
    });

    pdep_Y = pdep_Y / (static_cast<double>(num_rows) * num_rows);
    shannon_Y = -1.0 * (shannon_Y / num_rows);

    size_t dom_x_size = x_table.get_current_size();

    //// don't forget to also register the measures in the results, not just the final metrics.
    //// test implementation with disjoint metric computations and this one
    
    // Compute metrics
    double mu = mu_plus(num_rows, dom_x_size, pdep_XY, pdep_Y);
    double rfi = rfi_prime_plus(num_rows, x_counts, y_counts, shannon_XY, shannon_Y);
    
    auto compute_end = std::chrono::steady_clock::now();
    total_compute_time += (compute_end - compute_start);    
    
    result.mu_plus = mu;
    result.rfi_prime_plus = rfi;
    result.build_time_s = total_build_time.count();
    result.compute_time_s = total_compute_time.count();
    result.memory_used_mb = peak_memory_b / (1024.0 * 1024.0);

    return result;
}

template <size_t N>
Results dispatch_hasher(const ColumnarData& data, const std::vector<size_t>& lhs_indices, size_t rhs_idx, const std::string& hash_algo, size_t est_xy_card) {
     
    if (hash_algo == "murmur") {
        return execute<N, MurmurT<N>, MurmurT<1>, MurmurT<N+1>>(data, lhs_indices, rhs_idx, est_xy_card);
    }

    else if (hash_algo == "xxhash") {
        return execute<N, XXHashT<N>, XXHashT<1>, XXHashT<N+1>>(data, lhs_indices, rhs_idx, est_xy_card);
    }

    return execute<N, MurmurT<N>, MurmurT<1>, MurmurT<N+1>>(data, lhs_indices, rhs_idx, est_xy_card);
}

Results compute_metrics(const ColumnarData& data, const FDSpec& fd, const std::string& hash_algo, size_t est_xy_card) {
    
    // Resolve LHS columns
	std::vector<size_t> lhs_indices;
	for (const auto &col_name : fd.lhs_columns) {
		lhs_indices.push_back(data.get_column_index(col_name));
	}

    size_t rhs_idx = data.get_column_index(fd.rhs_column);

    switch (lhs_indices.size()) {
        case 1: return dispatch_hasher<1>(data, lhs_indices, rhs_idx, hash_algo, est_xy_card);
        case 2: return dispatch_hasher<2>(data, lhs_indices, rhs_idx, hash_algo, est_xy_card);
        case 3: return dispatch_hasher<3>(data, lhs_indices, rhs_idx, hash_algo, est_xy_card);
        case 4: return dispatch_hasher<4>(data, lhs_indices, rhs_idx, hash_algo, est_xy_card);
        case 5: return dispatch_hasher<5>(data, lhs_indices, rhs_idx, hash_algo, est_xy_card);
        case 6: return dispatch_hasher<6>(data, lhs_indices, rhs_idx, hash_algo, est_xy_card);
        case 7: return dispatch_hasher<7>(data, lhs_indices, rhs_idx, hash_algo, est_xy_card);
        case 8: return dispatch_hasher<8>(data, lhs_indices, rhs_idx, hash_algo, est_xy_card);
        case 9: return dispatch_hasher<9>(data, lhs_indices, rhs_idx, hash_algo, est_xy_card);
        case 10: return dispatch_hasher<10>(data, lhs_indices, rhs_idx, hash_algo, est_xy_card);
        default: std::cout << "Unsupported number of LHS columns";
    }

    return Results();
}

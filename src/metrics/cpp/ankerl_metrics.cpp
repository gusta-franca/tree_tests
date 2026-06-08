#include <bit>
#include <cmath>
#include <iostream>
#include "ankerl/unordered_dense.h"
#include "metrics.h"
#include "simd_metrics.h"

#define XXH_INLINE_ALL
#include "xxhash.h"

template <size_t N>
struct array_hash {
    using is_avalanching = void;

    [[nodiscard]] uint64_t operator()(const std::array<uint32_t, N>& arr) const noexcept {
        return XXH3_64bits(arr.data(), N*sizeof(uint32_t));
    }
};

template <size_t N>
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

    ankerl::unordered_dense::map<XYKey, uint32_t, array_hash<N+1>> xy_table;
    xy_table.reserve(est_xy_card);

    for (size_t row = 0; row < num_rows; row++) {
        XYKey xy_key;
        for (size_t i = 0; i < N; i++) {
            xy_key[i] = data.columns[lhs_indices[i]][row];
        }

        // Nth slot is reserved for Y 
        xy_key[N] = data.columns[rhs_idx][row];

        // Increments the current XY count if xy_key is already inserted on the hashtable; otherwise, inserts it with count = 1
        xy_table[xy_key]++;
    }

    // size_t actual_xy_size = xy_table.get_current_size();
    // std::cout << "JSON_METRICS: {"
    //       << "\"num_rows\":" << num_rows << ","
    //       << "\"est_card\":" << est_xy_card << ","
    //       << "\"actual_card\":" << actual_xy_size << ","
    //       << "\"error_pct\":" << (std::abs(static_cast<double>(est_xy_card) - actual_xy_size) / actual_xy_size * 100.0)
    //       << "}" << std::endl;

    // Couting X and Y from XY
    // X or Y will have sizes at max equal to xy_table.size()
    uint64_t max_size = xy_table.size();
    ankerl::unordered_dense::map<XKey, uint32_t, array_hash<N>> x_table;
    ankerl::unordered_dense::map<YKey, uint32_t, array_hash<1>> y_table;
    x_table.reserve(max_size);
    y_table.reserve(max_size);

    for (const auto& [xy_key, xy_count] : xy_table) {
        // Since std::array doesn't allocate anything on heap, duj
        XKey x_key;
        std::copy(xy_key.begin(), xy_key.begin() + N, x_key.begin());
        x_table[x_key] += xy_count;
        
        YKey y_key = {xy_key[N]};
        y_table[y_key] += xy_count;
    }

    auto build_end = std::chrono::steady_clock::now();
    total_build_time += (build_end - build_start);

    // From the ankerl README:
    // The map/set has two data structures:
    // * `std::vector<value_type>` which holds all data. map/set iterators are just `std::vector<value_type>::iterator`!
    // * An indexing structure (bucket array), which is a flat array with 8-byte buckets.
    // MISSING ARRAY MEMORY USAGE, NO IDEA ON HOW TO GET THEM
    peak_memory_b = (xy_table.bucket_count() + x_table.bucket_count() + y_table.bucket_count()) * 8;

    auto compute_start = std::chrono::steady_clock::now();
    
    // Compute XY measure
    double pdep_XY = 0.0;
    double shannon_XY = 0.0;
    
    XKey x_key;
    for (const auto& [xy_key, xy_count] : xy_table) {
        std::copy(xy_key.begin(), xy_key.begin() + N, x_key.begin());

        uint32_t x_count = x_table[x_key];        

        pdep_XY += (static_cast<double>(xy_count) * xy_count) / x_count;
        shannon_XY += xy_count * std::log2(static_cast<double>(xy_count) / x_count);
    }

    pdep_XY = pdep_XY / static_cast<double>(num_rows);
    shannon_XY = -1.0 * (shannon_XY / num_rows);
   
    // Auxiliary vectors needed for RFI
    std::vector<uint32_t> x_counts;
    std::vector<uint32_t> y_counts;
    x_counts.reserve(x_table.size());
    y_counts.reserve(x_table.size());
    
    for (const auto& [x_key, x_count] : x_table) {
        x_counts.push_back(x_count);
    }

    // Compute Y measures
    double pdep_Y = 0.0;
    double shannon_Y = 0.0;

    for (const auto& [y_key, y_count] : y_table) {
        pdep_Y += (static_cast<double>(y_count) * y_count);
        shannon_Y += y_count * std::log2(static_cast<double>(y_count) / num_rows);

        y_counts.push_back(y_count);
    }

    pdep_Y = pdep_Y / (static_cast<double>(num_rows) * num_rows);
    shannon_Y = -1.0 * (shannon_Y / num_rows);

    size_t dom_x_size = x_table.size();

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

Results compute_metrics(const ColumnarData& data, const FDSpec& fd, const std::string& hash_algo, size_t est_xy_card) {    
	std::vector<size_t> lhs_indices;
	for (const auto &col_name : fd.lhs_columns) {
		lhs_indices.push_back(data.get_column_index(col_name));
	}

    size_t rhs_idx = data.get_column_index(fd.rhs_column);

    switch (lhs_indices.size()) {
        case 1: return execute<1>(data, lhs_indices, rhs_idx, est_xy_card);
        case 2: return execute<2>(data, lhs_indices, rhs_idx, est_xy_card);
        case 3: return execute<3>(data, lhs_indices, rhs_idx, est_xy_card);
        case 4: return execute<4>(data, lhs_indices, rhs_idx, est_xy_card);
        case 5: return execute<5>(data, lhs_indices, rhs_idx, est_xy_card);
        case 6: return execute<6>(data, lhs_indices, rhs_idx, est_xy_card);
        case 7: return execute<7>(data, lhs_indices, rhs_idx, est_xy_card);
        case 8: return execute<8>(data, lhs_indices, rhs_idx, est_xy_card);
        case 9: return execute<9>(data, lhs_indices, rhs_idx, est_xy_card);
        case 10: return execute<10>(data, lhs_indices, rhs_idx, est_xy_card);
        default: std::cout << "Unsupported number of LHS columns"; 
    }

    return Results();
}

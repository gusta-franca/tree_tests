#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#define XXH_INLINE_ALL 1
#include "ankerl/unordered_dense.h"
#include "auto_relate.h"
#include "metrics.h"
#include "metrics_config.h"
#include "simd_metrics.h"
#include "xxhash.h"

template <size_t N>
struct array_hash {
    using is_avalanching = void;

    [[nodiscard]] uint64_t operator()(const std::array<uint32_t, N> &arr) const noexcept {
        return XXH3_64bits(arr.data(), N * sizeof(uint32_t));
    }
};

// Comprises information about the majority y value (used for auto-relate)
struct MajorityInfo {
    uint32_t count = 0;
    uint32_t y_id = 0;
    bool has_majority = false;
};

template <size_t N>
Results execute(const ColumnarData &data, const std::vector<size_t> &lhs_indices, size_t rhs_idx, size_t est_xy_card, const AutoRelateFDConfig &config, const std::vector<int>& violation_rows) {
    std::chrono::duration<double> total_build_time(0);
    std::chrono::duration<double> total_compute_time(0);
    size_t peak_memory_b = 0;

    Results result = {};

    auto build_start = std::chrono::steady_clock::now();

    // Arrays holding the X and Y columns; there will be one for every row
    using XKey = std::array<uint32_t, N>;
    using YKey = std::array<uint32_t, 1>;
    using XYKey = std::array<uint32_t, N + 1>;

    // Row count will be the size of any entry in data.columns, as it is columnar
    size_t num_rows = data.columns[0].size();

    ankerl::unordered_dense::map<XYKey, uint32_t, array_hash<N + 1>> xy_table;
    xy_table.reserve(est_xy_card);

#if ENABLE_AUTO_RELATE
    std::vector<XKey> row_x_keys(num_rows);
#endif

    for (size_t row = 0; row < num_rows; row++) {
        XYKey xy_key;
        for (size_t i = 0; i < N; i++) {
            xy_key[i] = data.columns[lhs_indices[i]][row];
        }
        xy_key[N] = data.columns[rhs_idx][row];
        xy_table[xy_key]++;

#if ENABLE_AUTO_RELATE
        std::copy(xy_key.begin(), xy_key.begin() + N, row_x_keys[row].begin());
#endif
    }

    // Couting X and Y from XY
    // X or Y will have sizes at max equal to xy_table.size()
    uint64_t max_size = xy_table.size();
    ankerl::unordered_dense::map<XKey, uint32_t, array_hash<N>> x_table;
    ankerl::unordered_dense::map<YKey, uint32_t, array_hash<1>> y_table;
    x_table.reserve(max_size);
    y_table.reserve(max_size);

    for (const auto &[xy_key, xy_count] : xy_table) {
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
    size_t bucket_memory = (xy_table.bucket_count() + x_table.bucket_count() + y_table.bucket_count()) * 8;
    size_t vector_memory = (xy_table.values().capacity() * sizeof(typename decltype(xy_table)::value_type)) +
                           (x_table.values().capacity() * sizeof(typename decltype(x_table)::value_type)) +
                           (y_table.values().capacity() * sizeof(typename decltype(y_table)::value_type));
    size_t object_memory = sizeof(xy_table) + sizeof(x_table) + sizeof(y_table);

#if ENABLE_AUTO_RELATE
    size_t row_x_keys_memory = row_x_keys.capacity() * sizeof(XKey);
    size_t bitset_memory = (num_rows + 7) / 8 + (data.columns.size() + 7) / 8; // is_violation + is_lhs
#else
    size_t row_x_keys_memory = 0;
    size_t bitset_memory = 0;
#endif

    peak_memory_b += bucket_memory + vector_memory + object_memory + row_x_keys_memory + bitset_memory;

    auto compute_start = std::chrono::steady_clock::now();

    // Compute XY measure
    double pdep_XY = 0.0;
    double shannon_XY = 0.0;

#if ENABLE_AUTO_RELATE
    ankerl::unordered_dense::map<XKey, MajorityInfo, array_hash<N>> majority_per_x;
#endif

#if ENABLE_G1
    double g1_violating_pairs = 0.0;
#endif

#if ENABLE_G2
    double g2_sum = 0.0;
#endif

#if ENABLE_G3_PRIME && !ENABLE_AUTO_RELATE
    ankerl::unordered_dense::map<XKey, uint32_t, array_hash<N>> x_group_max;
#endif

    XKey x_key;
    for (const auto &[xy_key, xy_count] : xy_table) {
        std::copy(xy_key.begin(), xy_key.begin() + N, x_key.begin());

        uint32_t x_count = x_table[x_key];

        pdep_XY += (static_cast<double>(xy_count) * xy_count) / x_count;
        shannon_XY += xy_count * std::log2(static_cast<double>(xy_count) / x_count);

#if ENABLE_G1
        g1_violating_pairs += static_cast<double>(xy_count) * static_cast<double>(x_count - xy_count);
#endif

#if ENABLE_G2
        if (x_count > xy_count) {
            g2_sum += static_cast<double>(xy_count) / static_cast<double>(num_rows);
        }
#endif

#if ENABLE_G3_PRIME && !ENABLE_AUTO_RELATE
        auto &cur_max = x_group_max[x_key];
        cur_max = std::max(cur_max, xy_count);
#endif

#if ENABLE_AUTO_RELATE
        bool lhs_has_null = std::any_of(x_key.begin(), x_key.end(), [](uint32_t v) { return v == ColumnarData::NULL_VALUE; });
        uint32_t y_id = xy_key[N];

        if (!lhs_has_null && y_id != ColumnarData::NULL_VALUE) {
            auto &maj = majority_per_x[x_key];
            if (xy_count > maj.count ||
               (xy_count == maj.count &&
                maj.has_majority &&
                data.dicts[rhs_idx].at(y_id) < data.dicts[rhs_idx].at(maj.y_id))) {
                maj.count = xy_count;
                maj.y_id = y_id;
                maj.has_majority = true;
            }
        }
#endif
    }

#if ENABLE_AUTO_RELATE
    size_t majority_bucket_memory = majority_per_x.bucket_count() * 8;
    size_t majority_vector_memory = majority_per_x.values().capacity() * sizeof(typename decltype(majority_per_x)::value_type);
    size_t majority_object_memory = sizeof(majority_per_x);

    peak_memory_b += majority_bucket_memory + majority_vector_memory + majority_object_memory;
#endif
#if ENABLE_G3_PRIME && !ENABLE_AUTO_RELATE
    size_t g3_bucket_memory = x_group_max.bucket_count() * 8;
    size_t g3_vector_memory = x_group_max.values().capacity() * sizeof(typename decltype(x_group_max)::value_type);
    size_t g3_object_memory = sizeof(x_group_max);

    peak_memory_b += g3_bucket_memory + g3_vector_memory + g3_object_memory;
#endif

#if ENABLE_G3_PRIME
    uint64_t g3_min_deletions = 0;
#if ENABLE_AUTO_RELATE
    // reuses majority counting from auto-relate if both g3' and auto-relate are set
    for (const auto &[gx_key, maj] : majority_per_x) {
        g3_min_deletions += x_table.at(gx_key) - maj.count;
    }
#else
    // computes on demand if auto-relate is not set
    for (const auto &[gx_key, max_count] : x_group_max) {
        g3_min_deletions += x_table.at(gx_key) - max_count;
    }
#endif
#endif

    pdep_XY = pdep_XY / static_cast<double>(num_rows);
    shannon_XY = -1.0 * (shannon_XY / num_rows);

    // Auxiliary vectors needed for RFI
    std::vector<uint32_t> x_counts;
    std::vector<uint32_t> y_counts;
    x_counts.reserve(x_table.size());
    y_counts.reserve(y_table.size());

    for (const auto &[x_key, x_count] : x_table) {
        x_counts.push_back(x_count);
    }

#if ENABLE_AUTO_RELATE
    // Auxiliary vectors for auto-relate's stability score
    std::vector<uint32_t> majority_counts;
    std::vector<uint32_t> majority_y_ids;
    majority_counts.reserve(majority_per_x.size());
    majority_y_ids.reserve(majority_per_x.size());

    for (const auto &[xk, maj] : majority_per_x) {
        majority_counts.push_back(maj.count);
        majority_y_ids.push_back(maj.y_id);
    }

    // find_violations()
    std::vector<bool> is_violation(num_rows, false);
    size_t violation_count = 0;

    if (config.dirty_data) {
        for (size_t row = 0; row < num_rows; row++) {
            const XKey &k = row_x_keys[row];
            bool lhs_has_null = std::any_of(k.begin(), k.end(), [](uint32_t v) { return v == ColumnarData::NULL_VALUE; });

            if (lhs_has_null)
                continue;

            auto it = majority_per_x.find(k);

            if (it == majority_per_x.end() || !it->second.has_majority) {
                continue;
            }

            uint32_t right_value = data.columns[rhs_idx][row];

            if (right_value != it->second.y_id) {
                is_violation[row] = true;
                violation_count++;
            }
        }
    }
    else {
        for (int idx : violation_rows) {
            if (idx >= 0 && static_cast<size_t>(idx) < num_rows) {
                is_violation[idx] = true;
                violation_count++;
            }
        }
    }

    double violation_rate = (num_rows > 0) ? static_cast<double>(violation_count) / num_rows : 0.0;

    std::vector<bool> is_lhs(data.columns.size(), false);
    for (size_t idx : lhs_indices) {
        is_lhs[idx] = true;
    }
#endif

    double pdep_Y = 0.0;
    double shannon_Y = 0.0;

    for (const auto &[y_key, y_count] : y_table) {
        pdep_Y += (static_cast<double>(y_count) * y_count);
        shannon_Y += y_count * std::log2(static_cast<double>(y_count) / num_rows);

        y_counts.push_back(y_count);
    }

    pdep_Y = pdep_Y / (static_cast<double>(num_rows) * num_rows);
    shannon_Y = -1.0 * (shannon_Y / num_rows);

    size_t dom_x_size = x_table.size();

    auto compute_end = std::chrono::steady_clock::now();

    // Compute metrics
#if ENABLE_MU_PLUS
    auto mu_start = std::chrono::steady_clock::now();
    double mu = mu_plus(num_rows, dom_x_size, pdep_XY, pdep_Y);
    auto mu_end = std::chrono::steady_clock::now();

    std::chrono::duration<double> mu_time = (mu_end - mu_start);
#else
    double mu = std::numeric_limits<double>::quiet_NaN();

    std::chrono::duration<double> mu_time(0);
#endif

#if ENABLE_RFI_PRIME_PLUS
    auto rfi_start = std::chrono::steady_clock::now();
    double rfi = rfi_prime_plus(num_rows, x_counts, y_counts, shannon_XY, shannon_Y);
    auto rfi_end = std::chrono::steady_clock::now();

    std::chrono::duration<double> rfi_time = (rfi_end - rfi_start);
#else
    double rfi = std::numeric_limits<double>::quiet_NaN();
    
    std::chrono::duration<double> rfi_time(0);
#endif

#if ENABLE_AUTO_RELATE
    auto independence_start = std::chrono::steady_clock::now();
    IndependenceTestResult independence = independence_test(data, is_lhs, rhs_idx, is_violation, violation_count, violation_rate, config);
    auto independence_end = std::chrono::steady_clock::now();

    std::chrono::duration<double> independence_time = (independence_end - independence_start);

    auto auto_relate_start = std::chrono::steady_clock::now();
    AutoRelateResult ar;

    if (independence.rejected) {
        ar.score = 1.0;
    }
    else {
        ar = auto_relate(num_rows, majority_counts, majority_y_ids);
    }

    ar.violation_count = violation_count;
    ar.violation_rate = violation_rate;
    auto auto_relate_end = std::chrono::steady_clock::now();

    std::chrono::duration<double> auto_relate_time = (auto_relate_end - auto_relate_start);

    bool ar_is_reliable = (ar.score <= config.perturbation_threshold);
#else
    IndependenceTestResult independence;
    AutoRelateResult ar;
    ar.score = std::numeric_limits<double>::quiet_NaN();
    bool ar_is_reliable = false;
    std::chrono::duration<double> independence_time(0), auto_relate_time(0);
#endif

#if ENABLE_G1
    auto g1_start = std::chrono::steady_clock::now();
    double g1 = 1.0 - (g1_violating_pairs / (static_cast<double>(num_rows) * num_rows));
    auto g1_end = std::chrono::steady_clock::now();

    std::chrono::duration<double> g1_time = (g1_end - g1_start);
#else
    double g1 = std::numeric_limits<double>::quiet_NaN();
    std::chrono::duration<double> g1_time(0);
#endif

#if ENABLE_G2
    auto g2_start = std::chrono::steady_clock::now();
    double g2 = 1.0 - g2_sum;
    auto g2_end = std::chrono::steady_clock::now();

    std::chrono::duration<double> g2_time = (g2_end - g2_start);
#else
    double g2 = std::numeric_limits<double>::quiet_NaN();
    std::chrono::duration<double> g2_time(0);
#endif

#if ENABLE_G3_PRIME
    auto g3_start = std::chrono::steady_clock::now();
    double g3_r_prime = static_cast<double>(num_rows) - static_cast<double>(g3_min_deletions);
    double g3_denominator = static_cast<double>(num_rows) - static_cast<double>(dom_x_size);
    double g3_prime = (g3_denominator == 0.0)
        ? (g3_r_prime == num_rows ? 1.0 : 0.0)
        : (g3_r_prime - static_cast<double>(dom_x_size)) / g3_denominator;
    auto g3_end = std::chrono::steady_clock::now();
    
    std::chrono::duration<double> g3_time = (g3_end - g3_start);
#else
    double g3_prime = std::numeric_limits<double>::quiet_NaN();
    std::chrono::duration<double> g3_time(0);
#endif

    total_compute_time += (compute_end - compute_start) + mu_time + rfi_time + independence_time + auto_relate_time + g1_time + g2_time + g3_time;

    result.mu_plus = mu;
    result.rfi_prime_plus = rfi;
    result.auto_relate_score = ar.score;
    result.auto_relate_violation_count = ar.violation_count;
    result.auto_relate_violation_rate = ar.violation_rate;
    result.auto_relate_is_reliable = ar_is_reliable;
    result.independence_used = independence.used;
    result.independence_rejected = independence.rejected;
    result.independence_pvalue = independence.pvalue;
    result.g1_score = g1;
    result.g2_score = g2;
    result.g3_prime_score = g3_prime;
    result.build_time_s = total_build_time.count();
    result.compute_time_s = total_compute_time.count();
    result.mu_compute_time_s = mu_time.count();
    result.rfi_compute_time_s = rfi_time.count();
    result.auto_relate_compute_time_s = auto_relate_time.count();
    result.independence_compute_time_s = independence_time.count();
    result.g1_compute_time_s = g1_time.count();
    result.g2_compute_time_s = g2_time.count();
    result.g3_prime_compute_time_s = g3_time.count();
    result.memory_used_mb = peak_memory_b / (1024.0 * 1024.0);

    return result;
}

Results compute_metrics(const ColumnarData &data, const FDSpec &fd, const std::string &hash_algo, size_t est_xy_card, const AutoRelateFDConfig &config, const std::vector<int>& violation_rows) {
    std::vector<size_t> lhs_indices;
    for (const auto &col_name : fd.lhs_columns) {
        size_t idx = data.get_column_index(col_name);

        if (idx == SIZE_MAX) {
            std::cerr << "compute_metrics: LHS column not found: " << col_name << std::endl;
        
            return Results();
        }

        lhs_indices.push_back(idx);
    }

    size_t rhs_idx = data.get_column_index(fd.rhs_column);
    
    if (rhs_idx == SIZE_MAX) {
        std::cerr << "compute_metrics: RHS column not found: " << fd.rhs_column << std::endl;
        
        return Results();
    }

    switch (lhs_indices.size()) {
    case 1:
        return execute<1>(data, lhs_indices, rhs_idx, est_xy_card, config, violation_rows);
    case 2:
        return execute<2>(data, lhs_indices, rhs_idx, est_xy_card, config, violation_rows);
    case 3:
        return execute<3>(data, lhs_indices, rhs_idx, est_xy_card, config, violation_rows);
    case 4:
        return execute<4>(data, lhs_indices, rhs_idx, est_xy_card, config, violation_rows);
    case 5:
        return execute<5>(data, lhs_indices, rhs_idx, est_xy_card, config, violation_rows);
    case 6:
        return execute<6>(data, lhs_indices, rhs_idx, est_xy_card, config, violation_rows);
    case 7:
        return execute<7>(data, lhs_indices, rhs_idx, est_xy_card, config, violation_rows);
    case 8:
        return execute<8>(data, lhs_indices, rhs_idx, est_xy_card, config, violation_rows);
    case 9:
        return execute<9>(data, lhs_indices, rhs_idx, est_xy_card, config, violation_rows);
    case 10:
        return execute<10>(data, lhs_indices, rhs_idx, est_xy_card, config, violation_rows);
    default:
        std::cout << "Unsupported number of LHS columns";
    }

    return Results();
}

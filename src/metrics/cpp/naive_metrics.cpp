#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "naive_metrics.h"

namespace {
    struct VectorHash {
        std::size_t operator()(std::vector<uint32_t> const& vec) const {
            std::size_t seed = vec.size();
            for(auto& i : vec) {
                seed ^= i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            }
            return seed;
        }
    };

    // df.loc[:, lhs].drop_duplicates()
    std::vector<uint32_t> group_counts(const ColumnarData& data, const std::vector<size_t>& col_indices) {
        const size_t n = data.num_rows;
        std::vector<uint32_t> counts;
        std::unordered_map<std::vector<uint32_t>, uint32_t, VectorHash> values;
        std::vector<uint32_t> key(col_indices.size());

        for (size_t row = 0; row < n; row++) {
            for (size_t i = 0; i < col_indices.size(); i++) {
                key[i] = data.columns[col_indices[i]][row];
            }

            values[key]++;
        }

        counts.reserve(values.size());
        for (const auto& [k, count] : values) {
            counts.push_back(count);
        }

        return counts;
    }

} // namespace

double pdep(const ColumnarData& data, const std::vector<size_t>& lhs_indices, size_t rhs_idx) {
    const size_t n = data.num_rows;

    std::unordered_map<std::vector<uint32_t>, uint64_t, VectorHash> xy_counts;
    std::unordered_map<std::vector<uint32_t>, uint64_t, VectorHash> x_counts;

    std::vector<uint32_t> x_key(lhs_indices.size());
    std::vector<uint32_t> xy_key(lhs_indices.size() + 1);

    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < lhs_indices.size(); j++) {
            uint32_t v = data.columns[lhs_indices[j]][i];
            x_key[j] = v;
            xy_key[j] = v;
        }
        
        xy_key[lhs_indices.size()] = data.columns[rhs_idx][i];

        x_counts[x_key]++;
        xy_counts[xy_key]++;
    }

    double sum = 0.0;
    std::vector<uint32_t> lhs(lhs_indices.size());

    for (const auto& [xy, xy_count] : xy_counts) {
        std::copy(xy.begin(), xy.end() - 1, lhs.begin());

        uint64_t x_count = x_counts[lhs];

        sum += (static_cast<double>(xy_count) * xy_count) / static_cast<double>(x_count);
    }

    return sum / static_cast<double>(n);
}

double pdep_self(const ColumnarData& data, size_t rhs_idx) {
    const size_t n = data.num_rows;
    const auto& column = data.columns[rhs_idx];

    std::unordered_map<uint32_t, uint64_t> counts;

    for (size_t i = 0; i < n; i++) {
        counts[column[i]]++;
    }

    double sum = 0.0;
    for (const auto& [value, count] : counts) {
        double p = static_cast<double>(count) / static_cast<double>(n);
        sum += p * p;
    }

    return sum;
}

MuPlusResult mu_plus(const ColumnarData& data, const FDSpec& fd) {
    using clock = std::chrono::steady_clock;

    MuPlusResult result;
    std::vector<size_t> lhs_indices;

    for (const auto& col_name : fd.lhs_columns) {
        size_t idx = data.get_column_index(col_name);
        
        if (idx == SIZE_MAX) {
            std::cerr << "mu_plus_naive: LHS column not found: " << col_name << std::endl;
            return result;
        }

        lhs_indices.push_back(idx);
    }

    size_t rhs_idx = data.get_column_index(fd.rhs_column);

    if (rhs_idx == SIZE_MAX) {
        std::cerr << "mu_plus_naive: RHS column not found: " << fd.rhs_column << std::endl;
    
        return result;
    }

    const size_t n = data.num_rows;

    auto build_start = clock::now();
    double pdep_XY = pdep(data, lhs_indices, rhs_idx);
    double pdep_Y = pdep_self(data, rhs_idx);
    size_t dom_x_size = group_counts(data, lhs_indices).size();
    auto build_end = clock::now();

    result.build_time_s = std::chrono::duration<double>(build_end - build_start).count();

    double lhs_uniqueness = (n > 0) ? static_cast<double>(dom_x_size) / static_cast<double>(n) : 0.0;

    auto compute_start = clock::now();
    if (n == dom_x_size) {
        result.result = 1.0;
        result.is_key = true;
    } else {
        double mu = 1.0
                  - ((1.0 - pdep_XY) / (1.0 - pdep_Y))
                  * (static_cast<double>(n - 1) / static_cast<double>(n - dom_x_size));

        result.result = std::max(0.0, mu);
        result.is_key = false;
        result.lhs_uniqueness = lhs_uniqueness;
        result.lhs_size = lhs_indices.size();
    }
    auto compute_end = clock::now();
    
    result.compute_time_s = std::chrono::duration<double>(compute_end - compute_start).count();

    return result;
}

double expected_mi(size_t num_rows, const std::vector<uint32_t>& x_counts, const std::vector<uint32_t>& y_counts) {
    int n = static_cast<int>(num_rows);
    double m = 0.0;

    std::vector<double> lgamma_cache(n + 1);
    for (int i = 0; i <= n; i++) lgamma_cache[i] = std::lgamma(i + 1);

    for (uint32_t a : x_counts) {
        for (uint32_t b : y_counts) {
            m += expected_mi_pair(n, static_cast<int>(a), static_cast<int>(b), lgamma_cache);
        }
    }

    return m;
}

double shannon(const std::unordered_map<uint32_t, uint64_t>& counts, size_t n) {
    double sum = 0.0;

    for (const auto& [value, count] : counts) {
        double p = static_cast<double>(count) / static_cast<double>(n);
        sum += p * std::log2(p);
    }

    return sum * -1.0;
}

double fraction_of_information(const ColumnarData& data, const std::vector<size_t>& lhs_indices, size_t rhs_idx) {
    const size_t n = data.num_rows;
    const auto& y_col = data.columns[rhs_idx];
    std::unordered_map<uint32_t, uint64_t> y_counts;
    
    for (size_t i = 0; i < n; i++) {
        y_counts[y_col[i]]++;
    }

    double shannon_y = shannon(y_counts, n);

    double shannon_xy = 0.0;
    std::unordered_map<std::vector<uint32_t>, uint64_t, VectorHash> xy_counts;
    std::unordered_map<std::vector<uint32_t>, uint64_t, VectorHash> x_counts;

    std::vector<uint32_t> x_key(lhs_indices.size());
    std::vector<uint32_t> xy_key(lhs_indices.size() + 1);

    for (size_t i = 0; i < n; i++) {
        for (size_t j = 0; j < lhs_indices.size(); j++) {
            uint32_t v = data.columns[lhs_indices[j]][i];
            x_key[j] = v;
            xy_key[j] = v;
        }

        xy_key[lhs_indices.size()] = y_col[i];
        x_counts[x_key]++;
        xy_counts[xy_key]++;
    }

    std::vector<uint32_t> lhs(lhs_indices.size());

    for (const auto& [xy, xy_count] : xy_counts) {
        std::copy(xy.begin(), xy.end() - 1, lhs.begin());
        uint64_t x_count = x_counts[lhs];
        double p_xy = static_cast<double>(xy_count) / static_cast<double>(n);
        shannon_xy += p_xy * std::log2(static_cast<double>(xy_count) / static_cast<double>(x_count));
    }

    shannon_xy *= -1.0;

    return (shannon_y - shannon_xy) / shannon_y;
}

RfiPrimePlusResult rfi_prime_plus(const ColumnarData& data, const FDSpec& fd) {
    using clock = std::chrono::steady_clock;
    RfiPrimePlusResult result;
    std::vector<size_t> lhs_indices;

    for (const auto& col_name : fd.lhs_columns) {
        size_t idx = data.get_column_index(col_name);

        if (idx == SIZE_MAX) {
            std::cerr << "rfi_prime_plus_naive: LHS column not found: " << col_name << std::endl;
            
            return result;
        }

        lhs_indices.push_back(idx);
    }

    size_t rhs_idx = data.get_column_index(fd.rhs_column);

    if (rhs_idx == SIZE_MAX) {
        std::cerr << "rfi_prime_plus_naive: RHS column not found: " << fd.rhs_column << std::endl;
        
        return result;
    }

    const size_t n = data.num_rows;

    auto build_start = clock::now();
    std::vector<uint32_t> x_counts = group_counts(data, lhs_indices);
    std::vector<uint32_t> y_counts = group_counts(data, {rhs_idx});
    auto build_end = clock::now();

    result.build_time_s = std::chrono::duration<double>(build_end - build_start).count();

    auto compute_start = clock::now();
    double m = expected_mi(n, x_counts, y_counts);
    double fi_value = fraction_of_information(data, lhs_indices, rhs_idx);
    std::unordered_map<uint32_t, uint64_t> y_counts_map;
    const auto& y_col = data.columns[rhs_idx];

    for (size_t row = 0; row < n; row++) {
        y_counts_map[y_col[row]]++;
    }

    double bias_estimator = m / shannon(y_counts_map, n);
    double rfi = fi_value - bias_estimator;

    result.fi = fi_value;
    result.bias_estimator = bias_estimator;
    result.result = std::max(rfi / (1.0 - bias_estimator), 0.0);

    auto compute_end = clock::now();
    result.compute_time_s = std::chrono::duration<double>(compute_end - compute_start).count();

    return result;
}

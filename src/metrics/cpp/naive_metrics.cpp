#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "naive_metrics.h"

namespace naive_metrics {

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

    struct FDColumns {
        std::vector<size_t> lhs_indices;
        size_t rhs_idx;
    };

    struct XYGroupInfo {
        // extra x_key vector so g3' can aggregate per x 
        std::vector<uint32_t> x_key;
        uint64_t xy_count;
        uint64_t x_count;
    };

    struct XGroupAgg {
        uint64_t total = 0;
        uint64_t max_sub = 0;
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

    double shannon(const std::unordered_map<uint32_t, uint64_t>& counts, size_t n) {
        double sum = 0.0;

        for (const auto& [value, count] : counts) {
            double p = static_cast<double>(count) / static_cast<double>(n);
            sum += p * std::log2(p);
        }

        return sum * -1.0;
    }

    std::optional<FDColumns> resolve_fd_columns(const ColumnarData& data, const FDSpec& fd, const std::string& metric_name) {
        FDColumns resolved;

        for (const auto& col_name : fd.lhs_columns) {
            size_t idx = data.get_column_index(col_name);

            if (idx == SIZE_MAX) {
                std::cerr << metric_name << ": LHS column not found: " << col_name << std::endl;

                return std::nullopt;
            }

            resolved.lhs_indices.push_back(idx);
        }

        resolved.rhs_idx = data.get_column_index(fd.rhs_column);

        if (resolved.rhs_idx == SIZE_MAX) {
            std::cerr << metric_name << ": RHS column not found: " << fd.rhs_column << std::endl;

            return std::nullopt;
        }

        return resolved;
    }

    std::vector<XYGroupInfo> build_xy_pairs(const ColumnarData& data, const std::vector<size_t>& lhs_indices, size_t rhs_idx) {
        const size_t n = data.num_rows;

        std::unordered_map<std::vector<uint32_t>, uint64_t, VectorHash> xy_counts;
        std::unordered_map<std::vector<uint32_t>, uint64_t, VectorHash> x_counts;

        std::vector<uint32_t> x_key(lhs_indices.size());
        std::vector<uint32_t> xy_key(lhs_indices.size() + 1);

        for (size_t row = 0; row < n; row++) {
            for (size_t i = 0; i < lhs_indices.size(); i++) {
                uint32_t v = data.columns[lhs_indices[i]][row];
                x_key[i] = v;
                xy_key[i] = v;
            }

            xy_key[lhs_indices.size()] = data.columns[rhs_idx][row];

            x_counts[x_key]++;
            xy_counts[xy_key]++;
        }

        std::vector<XYGroupInfo> pairs;
        pairs.reserve(xy_counts.size());

        std::vector<uint32_t> lhs(lhs_indices.size());

        for (const auto& [xy, xy_count] : xy_counts) {
            std::copy(xy.begin(), xy.end() - 1, lhs.begin());
            pairs.push_back({lhs, xy_count, x_counts.at(lhs)});
        }

        return pairs;
    }

    std::vector<XGroupAgg> build_x_groups(const std::vector<XYGroupInfo>& pairs) {
        std::unordered_map<std::vector<uint32_t>, XGroupAgg, VectorHash> groups;

        for (const auto& p : pairs) {
            auto& g = groups[p.x_key];
            g.total += p.xy_count;
            g.max_sub = std::max(g.max_sub, p.xy_count);
        }

        std::vector<XGroupAgg> result;
        result.reserve(groups.size());

        for (const auto& [x, g] : groups) {
            result.push_back(g);
        }

        return result;
    }
} // namespace

double pdep(const ColumnarData& data, const std::vector<size_t>& lhs_indices, size_t rhs_idx) {
    const size_t n = data.num_rows;
    std::vector<XYGroupInfo> pairs = build_xy_pairs(data, lhs_indices, rhs_idx);

    double sum = 0.0;
    for (const auto& p : pairs) {
        sum += (static_cast<double>(p.xy_count) * p.xy_count) / static_cast<double>(p.x_count);
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
    auto resolved = resolve_fd_columns(data, fd, "mu_plus_naive");

    if (!resolved) {
        return result;
    }

    const auto& lhs_indices = resolved->lhs_indices;
    size_t rhs_idx = resolved->rhs_idx;
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

double fraction_of_information(const ColumnarData& data, const std::vector<size_t>& lhs_indices, size_t rhs_idx) {
    const size_t n = data.num_rows;
    const auto& y_col = data.columns[rhs_idx];
    std::unordered_map<uint32_t, uint64_t> y_counts;

    for (size_t i = 0; i < n; i++) {
        y_counts[y_col[i]]++;
    }

    double shannon_y = shannon(y_counts, n);

    std::vector<XYGroupInfo> pairs = build_xy_pairs(data, lhs_indices, rhs_idx);

    double shannon_xy = 0.0;
    
    for (const auto& p : pairs) {
        double p_xy = static_cast<double>(p.xy_count) / static_cast<double>(n);
        shannon_xy += p_xy * std::log2(static_cast<double>(p.xy_count) / static_cast<double>(p.x_count));
    }
    
    shannon_xy *= -1.0;

    return (shannon_y - shannon_xy) / shannon_y;
}

RfiPrimePlusResult rfi_prime_plus(const ColumnarData& data, const FDSpec& fd) {
    using clock = std::chrono::steady_clock;

    RfiPrimePlusResult result;
    auto resolved = resolve_fd_columns(data, fd, "rfi_prime_plus_naive");

    if (!resolved) {
        return result;
    }

    const auto& lhs_indices = resolved->lhs_indices;
    size_t rhs_idx = resolved->rhs_idx;
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

G1Result g1(const ColumnarData& data, const FDSpec& fd) {
    using clock = std::chrono::steady_clock;
    G1Result result;
    auto resolved = resolve_fd_columns(data, fd, "g1_naive");

    if (!resolved) {
        return result;
    }

    const auto& lhs_indices = resolved->lhs_indices;
    size_t rhs_idx = resolved->rhs_idx;
    const size_t n = data.num_rows;

    auto build_start = clock::now();
    std::vector<XYGroupInfo> pairs = build_xy_pairs(data, lhs_indices, rhs_idx);
    auto build_end = clock::now();

    result.build_time_s = std::chrono::duration<double>(build_end - build_start).count();

    auto compute_start = clock::now();
    double violating_tuple_pairs = 0.0;

    for (const auto& p : pairs) {
        violating_tuple_pairs += static_cast<double>(p.xy_count) * static_cast<double>(p.x_count - p.xy_count);
    }

    result.result = 1.0 - (violating_tuple_pairs / (static_cast<double>(n) * n));
    auto compute_end = clock::now();

    result.compute_time_s = std::chrono::duration<double>(compute_end - compute_start).count();

    return result;
}

G2Result g2(const ColumnarData& data, const FDSpec& fd) {
    using clock = std::chrono::steady_clock;
    G2Result result;
    auto resolved = resolve_fd_columns(data, fd, "g2_naive");

    if (!resolved) {
        return result;
    }

    const auto& lhs_indices = resolved->lhs_indices;
    size_t rhs_idx = resolved->rhs_idx;
    const size_t n = data.num_rows;

    auto build_start = clock::now();
    std::vector<XYGroupInfo> pairs = build_xy_pairs(data, lhs_indices, rhs_idx);
    auto build_end = clock::now();

    result.build_time_s = std::chrono::duration<double>(build_end - build_start).count();

    auto compute_start = clock::now();
    double sum = 0.0;

    for (const auto& p : pairs) {
        if (p.x_count > p.xy_count) {
            sum += static_cast<double>(p.xy_count) / static_cast<double>(n);
        }
    }

    result.result = 1.0 - sum;
    auto compute_end = clock::now();

    result.compute_time_s = std::chrono::duration<double>(compute_end - compute_start).count();

    return result;
}

G3PrimeResult g3_prime(const ColumnarData& data, const FDSpec& fd) {
    using clock = std::chrono::steady_clock;
    G3PrimeResult result;
    auto resolved = resolve_fd_columns(data, fd, "g3_prime_naive");

    if (!resolved) {
        return result;
    }

    const auto& lhs_indices = resolved->lhs_indices;
    size_t rhs_idx = resolved->rhs_idx;
    const size_t n = data.num_rows;

    auto build_start = clock::now();
    std::vector<XYGroupInfo> pairs = build_xy_pairs(data, lhs_indices, rhs_idx);
    std::vector<XGroupAgg> groups = build_x_groups(pairs);
    size_t dom_x_size = groups.size();
    auto build_end = clock::now();

    result.build_time_s = std::chrono::duration<double>(build_end - build_start).count();

    auto compute_start = clock::now();

    uint64_t min_deletions = 0;

    for (const auto& g : groups) {
        min_deletions += (g.total - g.max_sub);
    }

    double r_prime_size = static_cast<double>(n) - static_cast<double>(min_deletions);
    double r_size = static_cast<double>(n);
    double denominator = r_size - static_cast<double>(dom_x_size);

    if (denominator == 0.0) {
        result.result = (r_prime_size == r_size) ? 1.0 : 0.0;
    } else {
        result.result = (r_prime_size - static_cast<double>(dom_x_size)) / denominator;
    }

    auto compute_end = clock::now();
    result.compute_time_s = std::chrono::duration<double>(compute_end - compute_start).count();

    return result;
}

} // namespace naive_metrics

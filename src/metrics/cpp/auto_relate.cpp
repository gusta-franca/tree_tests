#include <algorithm>
#include <array>
#include <boost/math/distributions/chi_squared.hpp>
#include <chrono>
#include <cmath>
#include <vector>

#include "auto_relate.h"
#include "ankerl/unordered_dense.h"
#include "chi2_fast.hpp"

// struct to hold a group's values and their countings
struct GroupValues {
    std::vector<uint32_t> distinct_values;
    std::vector<uint64_t> counts;

    void count_values(uint32_t value) {
        for (size_t i = 0; i < distinct_values.size(); ++i) {
            if (distinct_values[i] == value) {
                counts[i]++;

                return;
            }
        }

        distinct_values.push_back(value);
        counts.push_back(1);
    }
};

double independence_pvalue(const ColumnarData& data,
                           const size_t& col_idx, 
                           const std::vector<bool>& is_violation, 
                           size_t n) {
    const auto& column = data.columns[col_idx];
    
    // chi2_fast.h and mostly degrees of freedom need the exact cardinality?
    size_t card = data.get_distinct_count(col_idx);

    // no point in computing for columns with less than 2 unique values
    if (card < 2) {
        return 1.0;
    }

    std::vector<uint32_t> holds(card, 0);
    std::vector<uint32_t> violates(card, 0);

    uint64_t holds_total = 0;
    uint64_t violates_total = 0;
 
    for (size_t i = 0; i < n; i++) {
        uint32_t value = column[i];
 
        if (value == ColumnarData::NULL_VALUE) continue;
 
        if (is_violation[i]) {
            violates[value]++;
            violates_total++;
        } 
        else {
            holds[value]++;
            holds_total++;
        }
    }

    if (holds_total == 0 || violates_total == 0) {
        return 1.0;
    }

    // analogue to scipy's chi2_contingency()
    /// needs more validation, some 
    chi2fast::Result res = chi2fast::chi2_2xN(holds.data(), violates.data(), card);

    return res.pvalue;
}


AutoRelateFDResult compute_auto_relate_fd(
    const ColumnarData& data,
    const std::string& left_col,
    const std::string& right_col,
    const std::vector<int>& violation_rows,
    const AutoRelateFDConfig& config) {

    using clock = std::chrono::steady_clock;

    AutoRelateFDResult result;
    result.left_col = left_col;
    result.right_col = right_col;

    const size_t left_idx = data.get_column_index(left_col);
    const size_t right_idx = data.get_column_index(right_col);

    if (left_idx == SIZE_MAX || right_idx == SIZE_MAX) {
        result.score = 1.0;
        
        return result;
    }

    const auto& left_data = data.columns[left_idx];
    const auto& right_data = data.columns[right_idx];
    const size_t n = data.num_rows;
    std::vector<bool> is_violation(n, false);
    size_t violation_count = 0;
    
    auto build_start = clock::now();

    if (config.dirty_data) {
        // !!find violations
        /// Will be substituted by the xy and x maps
        /// ALso adapt every use of left_* later to support multiple LHS columns
        ankerl::unordered_dense::map<uint32_t, std::vector<uint32_t>> groups_rows;

        /// group by left values
        for (uint32_t i = 0; i < n; i++) {
            uint32_t left_value = left_data[i];

            if (left_value == ColumnarData::NULL_VALUE) { 
                continue;
            }

            groups_rows[left_value].push_back(i);
        }

        /// build value->counts map
        for (const auto& [left_value, rows] : groups_rows) {
            ankerl::unordered_dense::map<uint32_t, uint32_t> value_counts;

            for (uint32_t row : rows) {
                value_counts[right_data[row]]++;
            }

            uint32_t majority_value = 0;
            uint32_t majority_count = 0;

            for (const auto& [value, count] : value_counts) {
                if (count > majority_count) {
                    majority_count = count;
                    majority_value = value;
                }
            }

            for (uint32_t row : rows) {
                if (right_data[row] != majority_value) {
                    is_violation[row] = true;
                    violation_count++;
                }
            }
        }
    }
    else {
        for (int idx : violation_rows) {
                is_violation[idx] = true;
                violation_count++;
            }
    }

    result.violation_count = violation_count;
    result.violation_rate = (n > 0) ? static_cast<double>(violation_count)/n : 0.0;

    auto build_end = clock::now();
    result.build_time_s = std::chrono::duration<double>(build_end - build_start).count();

    auto compute_start = clock::now();
    
    // !!independence test 
    if (config.use_independence_test && violation_count > 0) {
        result.independence_used = true;

        if (result.violation_rate > config.violation_rate_threshold) {
            result.independence_rejected = true;
            result.score = 1.0;
            result.is_reliable = false;

            auto compute_end = clock::now();
            result.compute_time_s = std::chrono::duration<double>(compute_end - compute_start).count();

            return result;
        }
        
        // applies the independence test for every column not in the FD
        for (size_t c = 0; c < data.columns.size(); c++) {
            if (c == left_idx || c == right_idx) continue;

            double pvalue = independence_pvalue(data, c, is_violation, n);
            result.independence_pvalue = pvalue;

            if (pvalue < config.significance_threshold) {
                result.independence_rejected = true;
                result.score = 1.0;
                result.is_reliable = false;

                auto compute_end = clock::now();
                result.compute_time_s = std::chrono::duration<double>(compute_end - compute_start).count();

                return result;
            }

        }
    }

    // !!stability score
    /// drop violating rows and rows with NULL; group surviving k/v pairs and count them (fd_dict_gen)
    ankerl::unordered_dense::map<uint32_t, GroupValues> groups;
    
    for (uint32_t i = 0; i < n; i++) {
        if (is_violation[i]) {
            continue;
        }

        uint32_t left_value = left_data[i];
        uint32_t right_value = right_data[i];

        if (left_value == ColumnarData::NULL_VALUE || right_value == ColumnarData::NULL_VALUE) {
            continue;
        }

        groups[left_value].count_values(right_value);
    }

    /// perturbation test (HT1_FD; score = 1 - HT1_FD())
    ankerl::unordered_dense::map<uint32_t, uint64_t> value_freq;

    uint64_t left_value_count = 0;
    for (const auto& [left_value, group] : groups) {
        if (group.counts.empty()) {
            continue;
        }

        value_freq[group.distinct_values[0]] += group.counts[0];
        left_value_count += group.counts[0];
    }

    double perturbation_score = 0.0;

    if (left_value_count > 0) {
        double sum = 0.0;

        for (const auto& [left_value, group] : groups) {
            if (group.counts.empty()) {
                continue;
            }

            // weird one element range loop
            if (group.counts[0] > 1) {
                sum += (1.0 - 
                        static_cast<double>(value_freq[group.distinct_values[0]]) / 
                        static_cast<double>(left_value_count)) * 
                        group.counts[0];
            }
        }

        perturbation_score = sum / static_cast<double>(left_value_count);
    }

    result.score = 1.0 - perturbation_score;
    result.is_reliable = (result.score <= config.perturbation_threshold);

    auto compute_end = clock::now();
    result.compute_time_s = std::chrono::duration<double>(compute_end - compute_start).count();

    return result;
}

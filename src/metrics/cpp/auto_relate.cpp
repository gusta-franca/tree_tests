#include <algorithm>
#include <array>
#include <boost/math/distributions/chi_squared.hpp>
#include <chrono>
#include <cmath>
#include <sstream>
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

struct ViolationTestResult {
    std::vector<bool> is_violation;
    size_t violation_count = 0;
};

std::vector<uint32_t> build_lhs_key(const ColumnarData& data, const std::vector<size_t>& lhs_idxs) {
    const size_t n = data.num_rows;
    std::vector<uint32_t> lhs_group(n);
    const auto& first = data.columns[lhs_idxs[0]];

    for (size_t i = 0; i < n; i++)
        lhs_group[i] = (first[i] == ColumnarData::NULL_VALUE) ? NULL_GROUP : first[i];

    for (size_t j = 1; j < lhs_idxs.size(); j++) {
        const auto& col = data.columns[lhs_idxs[j]];
        ankerl::unordered_dense::map<uint64_t, uint32_t> remap;

        for (size_t k = 0; k < n; k++) {
            if (lhs_group[k] == NULL_GROUP) {
                continue;
            }

            if (col[k] == ColumnarData::NULL_VALUE) { 
                lhs_group[k] = NULL_GROUP; 
                continue; 
            }

            uint64_t key = (uint64_t(lhs_group[k]) << 32) | col[k];
            lhs_group[k] = remap.try_emplace(key, static_cast<uint32_t>(remap.size())).first->second;
        }
    }
    return lhs_group;
}

std::vector<int> parse_violation_rows(const std::string& rows_str) {
    std::vector<int> rows;

    if (rows_str.empty() || rows_str == "") {
        return rows;
    }

    std::stringstream ss(rows_str);
    std::string r;

    while (std::getline(ss, r, ',')) {
        rows.push_back(stoi(r));
    }

    return rows;
}

double independence_pvalue(const ColumnarData& data,
                           const size_t& col_idx, 
                           const std::vector<bool>& is_violation, 
                           size_t n) {
    const auto& column = data.columns[col_idx];
    
    // chi2_fast.h needs the exact cardinality for degrees of freedom
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

    chi2fast::Result res = chi2fast::chi2_2xN(holds.data(), violates.data(), card);

    return res.pvalue;
}

ViolationTestResult find_violations(
    const ColumnarData& data,
    const std::vector<uint32_t>& lhs_group,
    size_t right_idx,
    const std::vector<int>& violation_rows,
    const AutoRelateFDConfig& config) {

    const auto& right_data = data.columns[right_idx];
    const size_t n = data.num_rows;

    ViolationTestResult result;
    result.is_violation.assign(n, false);

    if (config.dirty_data) {
        /// Will be substituted by the xy and x maps
        /// ALso adapt every use of left_* later to support multiple LHS columns
        ankerl::unordered_dense::map<uint32_t, std::vector<uint32_t>> groups_rows;

        /// group by left values
        for (uint32_t i = 0; i < n; i++) {
            if (lhs_group[i] == NULL_GROUP) {
                continue;
            }

            groups_rows[lhs_group[i]].push_back(i);
        }

        /// build value->counts map
        for (const auto& [left_value, rows] : groups_rows) {
            ankerl::unordered_dense::map<uint32_t, uint32_t> value_counts;

            for (uint32_t row : rows) {
                if (right_data[row] != ColumnarData::NULL_VALUE) {
                    value_counts[right_data[row]]++;
                }
            }

            uint32_t majority_value = UINT32_MAX;
            uint32_t majority_count = 0;

            for (const auto& [value, count] : value_counts) {
                // !!mode()[0]
                if (count > majority_count ||
                    (count == majority_count && data.dicts[right_idx].at(value) < data.dicts[right_idx].at(majority_value))) {
                    majority_count = count;
                    majority_value = value;
                }
            }

            for (uint32_t row : rows) {
                if (right_data[row] != majority_value) {
                    result.is_violation[row] = true;
                    result.violation_count++;
                }
            }
        }
    } 
    else {
        for (int idx : violation_rows) {
            result.is_violation[idx] = true;
            result.violation_count++;
        }
    }

    return result;
}

IndependenceTestResult independence_test(
    const ColumnarData& data,
    const std::vector<bool>& is_lhs,
    size_t right_idx,
    const std::vector<bool>& is_violation,
    size_t violation_count,
    double violation_rate,
    const AutoRelateFDConfig& config) {

    IndependenceTestResult result;

    if (!config.use_independence_test || violation_count == 0) {
        return result;
    }

    result.used = true;

    if (violation_rate > config.violation_rate_threshold) {
        result.rejected = true;
        
        return result;
    }

    const size_t n = data.num_rows;

    // applies the independence test for every column not in the FD
    for (size_t c = 0; c < data.columns.size(); c++) {
        if (is_lhs[c] || c == right_idx) continue;

        double pvalue = independence_pvalue(data, c, is_violation, n);
        result.pvalue = pvalue;

        if (pvalue < config.significance_threshold) {
            result.rejected = true;
            
            return result;
        }
    }

    return result;
}


double stability_test(
    const ColumnarData& data,
    const std::vector<uint32_t>& lhs_group,
    size_t right_idx,
    const std::vector<bool>& is_violation) {

    const auto& right_data = data.columns[right_idx];
    const size_t n = data.num_rows;

    ankerl::unordered_dense::map<uint32_t, GroupValues> groups;

    for (uint32_t i = 0; i < n; i++) {
        if (is_violation[i]) {
            continue;
        }

        uint32_t right_value = right_data[i];

        if (lhs_group[i] == NULL_GROUP || right_value == ColumnarData::NULL_VALUE) {
            continue;
        }

        groups[lhs_group[i]].count_values(right_value);
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

    return 1.0 - perturbation_score;
}


AutoRelateFDResult compute_auto_relate_fd(
    const ColumnarData& data,
    const std::vector<std::string>& left_cols,
    const std::string& right_col,
    const std::vector<int>& violation_rows,
    const AutoRelateFDConfig& config) {

    using clock = std::chrono::steady_clock;

    AutoRelateFDResult result;
    result.left_cols = left_cols;
    result.right_col = right_col;

    std::vector<size_t> left_idxs;
    std::vector<bool> is_lhs(data.columns.size(), false);

    for (const auto& name : left_cols) {
        size_t idx = data.get_column_index(name);

        if (idx == SIZE_MAX) {
            result.score = 1.0; 
            
            return result; 
        }

        left_idxs.push_back(idx);
        is_lhs[idx] = true;
    }

    const size_t right_idx = data.get_column_index(right_col);

    if (left_idxs.empty() || right_idx == SIZE_MAX) {
        result.score = 1.0;

        return result;
    }

    const size_t n = data.num_rows;

    auto build_start = clock::now();

    std::vector<uint32_t> lhs_group = build_lhs_key(data, left_idxs);
    ViolationTestResult violations = find_violations(data, lhs_group, right_idx, violation_rows, config);
    
    auto build_end = clock::now();

    result.violation_count = violations.violation_count;
    result.violation_rate = (n > 0) ? static_cast<double>(violations.violation_count) / n : 0.0;
    result.build_time_s = std::chrono::duration<double>(build_end - build_start).count();

    auto compute_start = clock::now();

    IndependenceTestResult independence = independence_test(data, is_lhs, right_idx, violations.is_violation,
                                                            violations.violation_count, result.violation_rate, config);

    result.independence_used = independence.used;
    result.independence_rejected = independence.rejected;
    result.independence_pvalue = independence.pvalue;

    if (independence.rejected) {
        result.score = 1.0;
        result.is_reliable = false;

        auto compute_end = clock::now();
        result.compute_time_s = std::chrono::duration<double>(compute_end - compute_start).count();

        return result;
    }

    result.score = stability_test(data, lhs_group, right_idx, violations.is_violation);
    result.is_reliable = (result.score <= config.perturbation_threshold);

    auto compute_end = clock::now();
    result.compute_time_s = std::chrono::duration<double>(compute_end - compute_start).count();

    return result;
}

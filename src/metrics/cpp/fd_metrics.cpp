// fd_metrics.cpp
// Implementation of fast FD metric computation

#include "fd_metrics.h"
#include "ankerl/unordered_dense.h"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <chrono>

namespace {

    // Hash for vector<uint32_t> to use as a group key
    struct VecHash {
        size_t operator()(const std::vector<uint32_t>& v) const noexcept {
            uint64_t h = 1469598103934665603ull; // FNV offset basis
            for (uint32_t x : v) {
                h ^= static_cast<uint64_t>(x);
                h *= 1099511628211ull; // FNV prime
            }
            return static_cast<size_t>(h);
        }
    };

    struct VecEq {
        bool operator()(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) const noexcept {
            return a == b;
        }
    };

    // Compute pdep_self(Y) = sum((count_i / n)^2)
    // This is just the sum of squared probabilities for each Y value
    double compute_pdep_self(
        const CSVIndex& index,
        const std::string& rhs_column,
        size_t r_size) {
        
        const ColumnIndex* rhs = index.get_column(rhs_column);
        if (!rhs) {
            std::cerr << "Error: RHS column not found: " << rhs_column << std::endl;
            return 0.0;
        }
        
        double sum = 0.0;
        for (const auto& [value, bitmap] : rhs->value_to_tuples) {
            double count = static_cast<double>(bitmap.cardinality());
            double prob = count / static_cast<double>(r_size);
            sum += prob * prob;
        }
        
        return sum;
    }

    // Compute pdep(X,Y) = (1/n) * sum(count(x,y)^2 / count(x))
    // This requires:
    // 1. Group by X to get count(x)
    // 2. Group by (X,Y) to get count(x,y)
    // 3. Join and compute the sum
    double compute_pdep(
        const CSVIndex& index,
        const std::vector<std::string>& lhs_columns,
        const std::string& rhs_column,
        size_t& dom_x_size,  // Output: number of distinct X values
        double& lhs_uniqueness,  // Output: LHS uniqueness
        bool verbose) {
        
        using namespace std::chrono;
        auto t_start = steady_clock::now();
        
        // Get column indices for row-wise access
        std::vector<size_t> lhs_indices;
        lhs_indices.reserve(lhs_columns.size());
        for (const auto& name : lhs_columns) {
            const auto* col = index.get_column(name);
            if (!col) {
                std::cerr << "Error: LHS column not found: " << name << std::endl;
                return 0.0;
            }
            lhs_indices.push_back(col->index);
        }
        
        const ColumnIndex* rhs = index.get_column(rhs_column);
        if (!rhs) {
            std::cerr << "Error: RHS column not found: " << rhs_column << std::endl;
            return 0.0;
        }
        size_t rhs_index = rhs->index;
        
        // Access row-wise values
        const auto& values = index.values();
        
        // Choose anchor: LHS column with smallest distinct count (same strategy as fd_checker)
        const ColumnIndex* anchor = index.get_column(lhs_columns[0]);
        size_t min_distinct = anchor->distinct_count();
        
        for (size_t i = 1; i < lhs_columns.size(); ++i) {
            const auto* col = index.get_column(lhs_columns[i]);
            size_t d = col->distinct_count();
            if (d < min_distinct) {
                min_distinct = d;
                anchor = col;
            }
        }
        
        if (verbose) {
            std::cout << "    Anchor column: " << anchor->name 
                      << " (distinct: " << min_distinct << ")" << std::endl;
        }
        
        // Maps for grouping
        // x_counts: X -> count(x)
        // xy_counts: (X,Y) -> count(x,y)
        using XYMap = ankerl::unordered_dense::map<std::vector<uint32_t>, uint64_t, VecHash, VecEq>;
        using XMap = ankerl::unordered_dense::map<std::vector<uint32_t>, uint64_t, VecHash, VecEq>;
        
        XMap x_counts;
        XYMap xy_counts;
        
        // Reserve space based on anchor cardinality
        x_counts.reserve(min_distinct);
        xy_counts.reserve(min_distinct * 2); // rough estimate
        
        std::vector<uint32_t> x_key;
        x_key.reserve(lhs_indices.size());
        
        std::vector<uint32_t> xy_key;
        xy_key.reserve(lhs_indices.size() + 1);
        
        auto t_group_start = steady_clock::now();
        
        // Single pass: build both x_counts and xy_counts
        // Iterate over anchor column's distinct values to reduce search space
        for (const auto& [anchor_val, bitmap] : anchor->value_to_tuples) {
            for (uint32_t tid : bitmap) {
                // Build X key
                x_key.clear();
                for (size_t col_idx : lhs_indices) {
                    x_key.push_back(values[col_idx][tid]);
                }
                
                // Build XY key
                xy_key = x_key;  // copy X
                xy_key.push_back(values[rhs_index][tid]);
                
                // Increment counts
                x_counts[x_key]++;
                xy_counts[xy_key]++;
            }
        }
        
        auto t_group_end = steady_clock::now();
        
        dom_x_size = x_counts.size();
        size_t r_size = index.tuple_count();
        lhs_uniqueness = static_cast<double>(dom_x_size) / static_cast<double>(r_size);
        
        if (verbose) {
            auto group_ms = duration_cast<milliseconds>(t_group_end - t_group_start).count();
            std::cout << "    Grouping took: " << group_ms << " ms" << std::endl;
            std::cout << "    Distinct X groups: " << dom_x_size << std::endl;
            std::cout << "    Distinct (X,Y) groups: " << xy_counts.size() << std::endl;
            std::cout << "    LHS uniqueness: " << lhs_uniqueness << std::endl;
        }
        
        // Compute pdep(X,Y) = (1/n) * sum(count(x,y)^2 / count(x))
        double sum = 0.0;
        
        for (const auto& [xy_key_val, xy_count] : xy_counts) {
            // Extract X part from XY key (all but last element)
            std::vector<uint32_t> x_key_val(xy_key_val.begin(), xy_key_val.end() - 1);
            
            auto it = x_counts.find(x_key_val);
            if (it == x_counts.end()) {
                std::cerr << "Error: X key not found for XY key" << std::endl;
                continue;
            }
            
            uint64_t x_count = it->second;
            double term = static_cast<double>(xy_count * xy_count) / static_cast<double>(x_count);
            sum += term;
        }
        
        double pdep_xy = sum / static_cast<double>(r_size);
        
        if (verbose) {
            auto t_end = steady_clock::now();
            auto total_ms = duration_cast<milliseconds>(t_end - t_start).count();
            std::cout << "    pdep(X,Y) computation took: " << total_ms << " ms" << std::endl;
        }
        
        return pdep_xy;
    }

} // anonymous namespace


// Helper functions
std::string metric_type_to_string(MetricType type) {
    switch (type) {
        case MetricType::MU_PLUS: return "mu_plus";
        case MetricType::MU: return "mu";
        default: return "unknown";
    }
}

MetricType string_to_metric_type(const std::string& str) {
    if (str == "mu_plus" || str == "MU_PLUS") return MetricType::MU_PLUS;
    if (str == "mu" || str == "MU") return MetricType::MU;
    return MetricType::MU_PLUS; // default
}


FDMetricResult compute_single_fd_metric(
    const CSVIndex& index,
    const FDSpec& fd,
    MetricType metric_type,
    bool verbose) {
    
    FDMetricResult result;
    result.fd = fd;
    result.metric_type = metric_type;
    result.r_size = index.tuple_count();
    result.lhs_size = fd.lhs_columns.size();
    
    if (verbose) {
        std::cout << "\n  Computing " << metric_type_to_string(metric_type) << " for FD: ";
        fd.print();
    }
    
    // Compute pdep(X,Y) - includes LHS uniqueness computation
    if (verbose) std::cout << "  Computing pdep(X,Y)..." << std::endl;
    result.pdep_xy = compute_pdep(index, fd.lhs_columns, fd.rhs_column, 
                                   result.dom_x_size, result.lhs_uniqueness, verbose);
    
    // Compute pdep_self(Y)
    if (verbose) std::cout << "  Computing pdep_self(Y)..." << std::endl;
    result.pdep_y = compute_pdep_self(index, fd.rhs_column, result.r_size);
    
    // Check if LHS is a key
    if (result.lhs_uniqueness == 1.0) {
        result.is_key = true;
        result.metric_value = 1.0;
        
        if (verbose) {
            std::cout << "  LHS is a key -> " << metric_type_to_string(metric_type) << " = 1.0" << std::endl;
        }
    } else {
        result.is_key = false;
        
        // Compute mu = 1 - ((1 - pdep_xy) / (1 - pdep_y)) * ((r_size - 1) / (r_size - dom_x_size))
        double numerator = 1.0 - result.pdep_xy;
        double denominator = 1.0 - result.pdep_y;
        double factor = static_cast<double>(result.r_size - 1) / 
                       static_cast<double>(result.r_size - result.dom_x_size);
        
        double mu = 1.0 - (numerator / denominator) * factor;
        
        // Set the result based on requested metric type
        if (metric_type == MetricType::MU) {
            result.metric_value = mu;
        } else { // MU_PLUS
            result.metric_value = std::max(mu, 0.0);
        }
        
        if (verbose) {
            std::cout << "  pdep(Y) = " << result.pdep_y << std::endl;
            std::cout << "  pdep(X,Y) = " << result.pdep_xy << std::endl;
            std::cout << "  dom(X) = " << result.dom_x_size << std::endl;
            std::cout << "  LHS uniqueness = " << result.lhs_uniqueness << std::endl;
            std::cout << "  mu = " << mu << std::endl;
            std::cout << "  " << metric_type_to_string(metric_type) << " = " << result.metric_value << std::endl;
        }
    }
    
    return result;
}

// Compute metrics for all FDs
std::vector<FDMetricResult> compute_fd_metrics(
    const CSVIndex& index, 
    const std::vector<FDSpec>& fds,
    MetricType metric_type,
    bool verbose) {
    
    using namespace std::chrono;
    auto t_start = steady_clock::now();
    
    std::cout << "\n=== Computing FD Metrics ===" << std::endl;
    std::cout << "Number of FDs: " << fds.size() << std::endl;
    std::cout << "Metric: " << metric_type_to_string(metric_type) << std::endl;
    
    std::vector<FDMetricResult> results;
    results.reserve(fds.size());
    
    for (size_t i = 0; i < fds.size(); ++i) {
        if (!verbose) {
            std::cout << "Processing FD " << (i + 1) << "/" << fds.size() << "... ";
            std::cout.flush();
        }
        
        auto t_fd_start = steady_clock::now();
        FDMetricResult result = compute_single_fd_metric(index, fds[i], metric_type, verbose);
        auto t_fd_end = steady_clock::now();
        
        if (!verbose) {
            auto fd_ms = duration_cast<milliseconds>(t_fd_end - t_fd_start).count();
            std::cout << "done (" << fd_ms << " ms, " 
                      << metric_type_to_string(metric_type) << "=" << result.metric_value << ")" << std::endl;
        }
        
        results.push_back(result);
    }
    
    auto t_end = steady_clock::now();
    auto total_ms = duration_cast<milliseconds>(t_end - t_start).count();
    
    std::cout << "\n=== Metrics Computation Complete ===" << std::endl;
    std::cout << "Total time: " << total_ms << " ms" << std::endl;
    std::cout << "Average per FD: " << (total_ms / fds.size()) << " ms" << std::endl;
    
    return results;
}


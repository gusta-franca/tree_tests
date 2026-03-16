// fd_metrics_opt.cpp
// Optimized implementation: no upfront indexing, bitmap-based computation

#include "fd_metrics_opt.h"
#include "fd_input.h"
#include "ankerl/unordered_dense.h"
#include "roaring.hh"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cmath>
#include <sys/resource.h>

// Helper functions
std::string metric_type_to_string(MetricType type) {
    return (type == MetricType::MU_PLUS) ? "mu_plus" : "mu";
}

MetricType string_to_metric_type(const std::string& str) {
    if (str == "mu_plus" || str == "MU_PLUS") return MetricType::MU_PLUS;
    if (str == "mu" || str == "MU") return MetricType::MU;
    return MetricType::MU_PLUS;
}

// ColumnarData implementation
size_t ColumnarData::get_column_index(const std::string& name) const {
    for (size_t i = 0; i < column_names.size(); ++i) {
        if (column_names[i] == name) return i;
    }
    return SIZE_MAX;
}

size_t ColumnarData::get_distinct_count(size_t col_idx) const {
    if (col_idx >= columns.size()) return 0;
    
    // Use cached value if available
    if (col_idx < distinct_counts.size() && distinct_counts[col_idx] > 0) {
        return distinct_counts[col_idx];
    }
    
    // Compute on-demand using a set
    ankerl::unordered_dense::set<uint32_t> unique_vals;
    for (uint32_t val : columns[col_idx]) {
        unique_vals.insert(val);
    }
    
    // Cache the result
    if (distinct_counts.size() <= col_idx) {
        distinct_counts.resize(col_idx + 1, 0);
    }
    distinct_counts[col_idx] = unique_vals.size();
    
    return unique_vals.size();
}

// Load CSV without building indexes
bool load_csv_columnar(const std::string& filename, ColumnarData& data, bool verbose) {
    using clock = std::chrono::steady_clock;
    auto t_start = clock::now();
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file: " << filename << std::endl;
        return false;
    }
    
    // Parse header
    std::string header_line;
    if (!std::getline(file, header_line)) {
        std::cerr << "Error: Empty file" << std::endl;
        return false;
    }
    
    std::stringstream ss(header_line);
    std::string col_name;
    while (std::getline(ss, col_name, ',')) {
        // Trim whitespace
        size_t start = col_name.find_first_not_of(" \t\r\n");
        size_t end = col_name.find_last_not_of(" \t\r\n");
        if (start != std::string::npos) {
            col_name = col_name.substr(start, end - start + 1);
        }
        data.column_names.push_back(col_name);
    }
    
    data.columns.resize(data.column_names.size());
    
    if (verbose) {
        std::cout << "Columns: ";
        for (size_t i = 0; i < data.column_names.size(); ++i) {
            std::cout << data.column_names[i];
            if (i + 1 < data.column_names.size()) std::cout << ", ";
        }
        std::cout << std::endl;
    }
    
    // Parse data rows
    std::string line;
    size_t row_count = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::stringstream row_ss(line);
        std::string cell;
        size_t col_idx = 0;
        
        while (std::getline(row_ss, cell, ',') && col_idx < data.columns.size()) {
            uint32_t value = 0;
            try {
                value = std::stoul(cell);
            } catch (...) {
                value = 0;
            }
            data.columns[col_idx].push_back(value);
            ++col_idx;
        }
        
        // Pad short rows
        while (col_idx < data.columns.size()) {
            data.columns[col_idx].push_back(0);
            ++col_idx;
        }
        
        ++row_count;
    }
    
    data.num_rows = row_count;
    
    auto t_end = clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    
    if (verbose) {
        std::cout << "Loaded " << row_count << " rows in " << elapsed_ms << " ms" << std::endl;
        std::cout << "No indexes built - pure columnar storage" << std::endl;
    }
    
    return true;
}

namespace {
    // Compute pdep_self using on-the-fly grouping (no index needed)
    double compute_pdep_self_opt(const ColumnarData& data, size_t rhs_idx) {
        ankerl::unordered_dense::map<uint32_t, uint64_t> value_counts;
        
        const auto& rhs_col = data.columns[rhs_idx];
        for (uint32_t val : rhs_col) {
            value_counts[val]++;
        }
        
        double sum = 0.0;
        double n = static_cast<double>(data.num_rows);
        for (const auto& [val, count] : value_counts) {
            double prob = static_cast<double>(count) / n;
            sum += prob * prob;
        }
        
        return sum;
    }
    
    // Hash for composite keys (inline small keys optimization)
    struct CompactKeyHash {
        size_t operator()(const std::vector<uint32_t>& v) const noexcept {
            // Optimized hash for small keys
            if (v.size() == 1) {
                return std::hash<uint32_t>{}(v[0]);
            } else if (v.size() == 2) {
                return std::hash<uint64_t>{}(
                    (static_cast<uint64_t>(v[0]) << 32) | v[1]);
            } else {
                // FNV-1a for larger keys
                uint64_t h = 14695981039346656037ull;
                for (uint32_t x : v) {
                    h ^= x;
                    h *= 1099511628211ull;
                }
                return static_cast<size_t>(h);
            }
        }
    };
    
    // Bitmap-based computation for low-cardinality cases
    double compute_pdep_bitmap(
        const ColumnarData& data,
        const std::vector<size_t>& lhs_indices,
        size_t rhs_idx,
        size_t& dom_x_size,
        double& lhs_uniqueness) {
        
        // Build value->bitmap maps for LHS columns
        std::vector<ankerl::unordered_dense::map<uint32_t, roaring::Roaring>> lhs_bitmaps(lhs_indices.size());
        
        for (size_t i = 0; i < lhs_indices.size(); ++i) {
            const auto& col = data.columns[lhs_indices[i]];
            for (uint32_t tid = 0; tid < col.size(); ++tid) {
                lhs_bitmaps[i][col[tid]].add(tid);
            }
        }
        
        // Build RHS bitmap
        ankerl::unordered_dense::map<uint32_t, roaring::Roaring> rhs_bitmap;
        const auto& rhs_col = data.columns[rhs_idx];
        for (uint32_t tid = 0; tid < rhs_col.size(); ++tid) {
            rhs_bitmap[rhs_col[tid]].add(tid);
        }
        
        // Compute using bitmap intersections
        ankerl::unordered_dense::map<std::vector<uint32_t>, uint64_t, CompactKeyHash> x_counts;
        double sum = 0.0;
        
        // Iterate over Cartesian product of LHS value combinations
        std::function<void(size_t, std::vector<uint32_t>&, roaring::Roaring&)> iterate_combinations;
        iterate_combinations = [&](size_t depth, std::vector<uint32_t>& key, roaring::Roaring& intersection) {
            if (depth == lhs_indices.size()) {
                // Have complete X key, now intersect with all Y values
                uint64_t x_count = intersection.cardinality();
                if (x_count == 0) return;
                
                x_counts[key] = x_count;
                
                for (const auto& [y_val, y_bitmap] : rhs_bitmap) {
                    roaring::Roaring xy_intersection = intersection & y_bitmap;
                    uint64_t xy_count = xy_intersection.cardinality();
                    if (xy_count > 0) {
                        double term = static_cast<double>(xy_count * xy_count) / static_cast<double>(x_count);
                        sum += term;
                    }
                }
                return;
            }
            
            // Recurse through this dimension's values
            for (const auto& [val, bitmap] : lhs_bitmaps[depth]) {
                key.push_back(val);
                roaring::Roaring new_intersection = (depth == 0) ? bitmap : (intersection & bitmap);
                
                if (new_intersection.cardinality() > 0) {
                    iterate_combinations(depth + 1, key, new_intersection);
                }
                
                key.pop_back();
            }
        };
        
        std::vector<uint32_t> key;
        roaring::Roaring empty_bitmap;
        iterate_combinations(0, key, empty_bitmap);
        
        dom_x_size = x_counts.size();
        lhs_uniqueness = static_cast<double>(dom_x_size) / static_cast<double>(data.num_rows);
        
        return sum / static_cast<double>(data.num_rows);
    }
    
    // Hash-based computation (similar to original but optimized)
    double compute_pdep_hash(
        const ColumnarData& data,
        const std::vector<size_t>& lhs_indices,
        size_t rhs_idx,
        size_t& dom_x_size,
        double& lhs_uniqueness) {
        
        using XMap = ankerl::unordered_dense::map<std::vector<uint32_t>, uint64_t, CompactKeyHash>;
        using XYMap = ankerl::unordered_dense::map<std::vector<uint32_t>, uint64_t, CompactKeyHash>;
        
        XMap x_counts;
        XYMap xy_counts;
        
        // Reserve based on heuristic
        size_t est_groups = std::min(data.num_rows, size_t(10000));
        x_counts.reserve(est_groups);
        xy_counts.reserve(est_groups * 2);
        
        std::vector<uint32_t> x_key;
        std::vector<uint32_t> xy_key;
        x_key.reserve(lhs_indices.size());
        xy_key.reserve(lhs_indices.size() + 1);
        
        // Single pass through data
        for (size_t tid = 0; tid < data.num_rows; ++tid) {
            x_key.clear();
            for (size_t lhs_idx : lhs_indices) {
                x_key.push_back(data.columns[lhs_idx][tid]);
            }
            
            xy_key = x_key;
            xy_key.push_back(data.columns[rhs_idx][tid]);
            
            x_counts[x_key]++;
            xy_counts[xy_key]++;
        }
        
        dom_x_size = x_counts.size();
        lhs_uniqueness = static_cast<double>(dom_x_size) / static_cast<double>(data.num_rows);
        
        // Early termination: perfect FD detection
        if (xy_counts.size() == x_counts.size()) {
            return 1.0;  // pdep_xy = 1.0 means perfect dependency
        }
        
        // Compute pdep
        double sum = 0.0;
        for (const auto& [xy_key_val, xy_count] : xy_counts) {
            std::vector<uint32_t> x_key_val(xy_key_val.begin(), xy_key_val.end() - 1);
            uint64_t x_count = x_counts[x_key_val];
            double term = static_cast<double>(xy_count * xy_count) / static_cast<double>(x_count);
            sum += term;
        }
        
        return sum / static_cast<double>(data.num_rows);
    }


// void radix_sort_64(std::vector<uint64_t>& data) {
//     size_t n = data.size();
//     std::vector<uint64_t> temp(n);
    
//     for (int shift = 0; shift < 64; shift += 8) {
//         uint32_t counts[256] = {0};
        
//         for (size_t i = 0; i < n; ++i) {
//             counts[(data[i] >> shift) & 0xFF]++;
//         }
        
//         uint32_t prefix[256] = {0};
//         for (int i = 1; i < 256; ++i) {
//             prefix[i] = prefix[i - 1] + counts[i - 1];
//         }
        
//         for (size_t i = 0; i < n; ++i) {
//             uint8_t byte_val = (data[i] >> shift) & 0xFF;
//             temp[prefix[byte_val]++] = data[i];
//         }
        
//         data.swap(temp);
//     }
// }

    
} // anonymous namespace

long get_memory_usage() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss;
}

// Main computation function with strategy selection
FDMetricResult compute_single_fd_metric_opt(
    const ColumnarData& data,
    const FDSpec& fd,
    MetricType metric_type,
    bool verbose,
    const std::string& algo) {
    
    FDMetricResult result;
    result.fd = fd;
    result.metric_type = metric_type;
    result.r_size = data.num_rows;
    result.lhs_size = fd.lhs_columns.size();
    
    if (verbose) {
        std::cout << "\n  Computing " << metric_type_to_string(metric_type) << " for FD: ";
        fd.print();
    }

    long start_memory = get_memory_usage();
    auto start_time = std::chrono::steady_clock::now();
    
    // Get column indices
    std::vector<size_t> lhs_indices;
    for (const auto& col_name : fd.lhs_columns) {
        size_t idx = data.get_column_index(col_name);
        if (idx == SIZE_MAX) {
            std::cerr << "Error: Column not found: " << col_name << std::endl;
            return result;
        }
        lhs_indices.push_back(idx);
    }
    
    size_t rhs_idx = data.get_column_index(fd.rhs_column);
    if (rhs_idx == SIZE_MAX) {
        std::cerr << "Error: Column not found: " << fd.rhs_column << std::endl;
        return result;
    }
    
    // Strategy selection based on cardinality
    size_t total_lhs_card = 1;
    for (size_t idx : lhs_indices) {
        size_t card = data.get_distinct_count(idx);
        total_lhs_card *= card;
        if (total_lhs_card > 100000) break;  // Overflow guard
    }
    size_t rhs_card = data.get_distinct_count(rhs_idx);
    
    // Use bitmap only for very small cardinality products
    // The bitmap approach iterates through Cartesian products which is expensive
    bool use_bitmap = false;
    
    if (verbose) {
        std::cout << "  Strategy: " << (use_bitmap ? "Bitmap-based" : "Hash-based") << std::endl;
    }
    
    // Compute pdep(X,Y)
    auto t1 = std::chrono::steady_clock::now();

    if (algo == "bitmap") 
        use_bitmap = true;
    else if (algo == "hash") 
        use_bitmap = false;
    else 
        use_bitmap = (total_lhs_card * rhs_card < 1000) && (lhs_indices.size() == 1);
    
    if (use_bitmap) {
        result.pdep_xy = compute_pdep_bitmap(data, lhs_indices, rhs_idx, 
                                             result.dom_x_size, result.lhs_uniqueness);
    } else {
        result.pdep_xy = compute_pdep_hash(data, lhs_indices, rhs_idx,
                                           result.dom_x_size, result.lhs_uniqueness);
    }

    auto t2 = std::chrono::steady_clock::now();
    
    // Compute pdep(Y)
    result.pdep_y = compute_pdep_self_opt(data, rhs_idx);
    auto t3 = std::chrono::steady_clock::now();
    
    // Check if LHS is a key
    result.is_key = (result.lhs_uniqueness == 1.0);
    
    if (result.is_key) {
        result.metric_value = 1.0;
    } else {
        double numerator = 1.0 - result.pdep_xy;
        double denominator = 1.0 - result.pdep_y;
        double factor = static_cast<double>(result.r_size - 1) / 
                       static_cast<double>(result.r_size - result.dom_x_size);
        
        double mu = 1.0 - (numerator / denominator) * factor;
        
        if (metric_type == MetricType::MU) {
            result.metric_value = mu;
        } else {
            result.metric_value = std::max(mu, 0.0);
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    long end_memory = get_memory_usage();

    std::chrono::duration<double> duration = end_time - start_time;
    double memory_used = (end_memory - start_memory) / 1024.0;

    // Print results
    std::cout << result.metric_value << "," << duration.count() << "," << memory_used << std::endl;
    
    if (verbose) {
        auto pdep_xy_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        auto pdep_y_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();
        std::cout << "  pdep(X,Y): " << pdep_xy_ms << "ms, pdep(Y): " << pdep_y_ms << "ms" << std::endl;
        std::cout << "  Result: " << result.metric_value << std::endl;
    }
    
    return result;
}

std::vector<FDMetricResult> compute_fd_metrics_opt(
    const ColumnarData& data,
    const std::vector<FDSpec>& fds,
    MetricType metric_type,
    bool verbose) {
    
    using namespace std::chrono;
    auto t_start = steady_clock::now();
    
    std::cout << "\n=== Computing FD Metrics (OPTIMIZED) ===" << std::endl;
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
        FDMetricResult result = compute_single_fd_metric_opt(data, fds[i], metric_type, verbose);
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

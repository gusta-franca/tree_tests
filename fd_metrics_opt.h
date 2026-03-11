// fd_metrics_opt.h
// Optimized FD metric computation with no upfront indexing

#ifndef FD_METRICS_OPT_H
#define FD_METRICS_OPT_H

#include "fd_input.h"
#include <string>
#include <vector>
#include <cstdint>

enum class MetricType {
    MU_PLUS,
    MU
};

struct FDMetricResult {
    FDSpec fd;
    MetricType metric_type;
    double metric_value;
    bool is_key;
    size_t lhs_size;
    double lhs_uniqueness;
    double pdep_xy;
    double pdep_y;
    size_t dom_x_size;
    size_t r_size;
};

// Simple columnar data structure - no indexes!
struct ColumnarData {
    std::vector<std::string> column_names;
    std::vector<std::vector<uint32_t>> columns;  // column-major data
    size_t num_rows;
    
    // Statistics computed on-demand
    mutable std::vector<size_t> distinct_counts;  // cached distinct counts
    
    ColumnarData() : num_rows(0) {}
    
    size_t get_column_index(const std::string& name) const;
    size_t get_distinct_count(size_t col_idx) const;
};

// Load CSV directly into columnar format (no indexing)
bool load_csv_columnar(const std::string& filename, ColumnarData& data, bool verbose = false);

// Optimized metric computation
FDMetricResult compute_single_fd_metric_opt(
    const ColumnarData& data,
    const FDSpec& fd,
    MetricType metric_type,
    bool verbose = false,
    const std::string& algo = "auto");

std::vector<FDMetricResult> compute_fd_metrics_opt(
    const ColumnarData& data,
    const std::vector<FDSpec>& fds,
    MetricType metric_type,
    bool verbose = false);

// Helper functions
std::string metric_type_to_string(MetricType type);
MetricType string_to_metric_type(const std::string& str);

#endif // FD_METRICS_OPT_H

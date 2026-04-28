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

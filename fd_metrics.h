// fd_metrics.h
// Fast metric computation for functional dependencies using optimized groupby operations

#ifndef FD_METRICS_H
#define FD_METRICS_H

#include <string>
#include <vector>
#include <cstdint>
#include "csv_index.h"
#include "fd_input.h"

// Supported metric types
enum class MetricType {
    MU_PLUS,    // max(mu, 0) - default
    MU          // base mu metric
};

// Result of computing metrics for a single FD
struct FDMetricResult {
    FDSpec fd;                  // The FD that was measured
    MetricType metric_type;     // Which metric was computed
    double metric_value;        // The computed metric value
    bool is_key;                // True if LHS is a key (domX_size == r_size)
    double lhs_uniqueness;      // domX_size / r_size
    size_t lhs_size;            // Number of columns in LHS
    
    // Intermediate values (useful for debugging/analysis)
    double pdep_xy;             // pdep(X,Y) value
    double pdep_y;              // pdep(Y) value  
    size_t dom_x_size;          // Number of distinct LHS values
    size_t r_size;              // Total number of rows
};

// Compute metrics for a batch of FDs
// Returns vector of results, one per FD
std::vector<FDMetricResult> compute_fd_metrics(
    const CSVIndex& index,
    const std::vector<FDSpec>& fds,
    MetricType metric_type = MetricType::MU_PLUS,
    bool verbose = false);

// Compute metrics for a single FD (called internally by batch version)
FDMetricResult compute_single_fd_metric(
    const CSVIndex& index,
    const FDSpec& fd,
    MetricType metric_type = MetricType::MU_PLUS,
    bool verbose = false);

// Helper to convert MetricType to string
std::string metric_type_to_string(MetricType type);

// Helper to parse string to MetricType
MetricType string_to_metric_type(const std::string& str);

#endif // FD_METRICS_H

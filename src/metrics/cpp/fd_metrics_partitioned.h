// fd_metrics_anchor.h
#ifndef FD_METRICS_CHECKER_H
#define FD_METRICS_CHECKER_H

#include <cstdint>
#include <vector>

#include "csv_index.h" 
#include "fd_input.h"


struct PdepResult {
    double metric_value;
    double build_time_s;
    double compute_time_s;
    double memory_used_mb;
};

// Still using CSVIndex for inverted index
PdepResult compute_pdep(const CSVIndex& data, const FDSpec& fd);

#endif // FD_METRICS_ANCHOR_H

#pragma once

#include <string>
#include "csv_index.h"

struct AutoRelateFDConfig {
    // independence test by default due to default mode being dirty
    bool use_independence_test = true;

    // matches the paper's default alpha = 0.05
    double significance_threshold = 0.05;

    // matches the original_violation_rate check in ht_afd.py 
    double violation_rate_threshold = 0.5;

    // matches the paper's default perturbation thrsehold = 0.5
    double perturbation_threshold = 0.5;
};

struct AutoRelateFDResult {
    std::string left_col;
    std::string right_col;

    // reliability score
    double score = 1.0;

    size_t violation_count = 0;
    double violation_rate = 0.0;

    bool is_reliable = false;

    double build_time_s = 0.0;   // accuracy test?
    double compute_time_s = 0.0; // independence test + score?
};

// compute Auto-Relate's FD reliability score for one candidate (only FDs in the format left_col -> right_col)
AutoRelateFDResult compute_auto_relate_fd(
    const ColumnarData& data,
    const std::string& left_col,
    const std::string& right_col,
    const AutoRelateFDConfig& config = AutoRelateFDConfig());

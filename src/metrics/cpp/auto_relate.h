#pragma once

#include <string>
#include "csv_index.h"

struct AutoRelateFDConfig {
    bool dirty_data = true;
    
    // independence test by default due to default mode being dirty
    bool use_independence_test = true;

    // matches the original ht2_threshdhol = 0.0001
    double significance_threshold = 0.0001;

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

    double independence_pvalue = -1.0;
    bool independence_used = false;
    bool independence_rejected = false;

    bool is_reliable = false;

    double build_time_s = 0.0;   // accuracy test?
    double compute_time_s = 0.0; // independence test + stability score?
};

// compute Auto-Relate's FD reliability score for one candidate (only FDs in the format left_col -> right_col)
AutoRelateFDResult compute_auto_relate_fd(
    const ColumnarData& data,
    const std::string& left_col,
    const std::string& right_col,
    const std::vector<int>& violation_rows,
    const AutoRelateFDConfig& config = AutoRelateFDConfig());

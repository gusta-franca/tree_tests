#pragma once

#include "fd_input.h"


struct PdepResult {
    double metric_value;
    double build_time_s;
    double compute_time_s;
    double memory_used_mb;
};


PdepResult compute_pdep(const ColumnarData& index, const FDSpec& fd, const std::string& hash_algo, size_t est_xy_card);

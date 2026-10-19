#pragma once

#include "core/fd_input.h"
#include "core/metrics.h"

Results compute_metrics(const ColumnarData& index, const FDSpec& fd, const std::string& hash_algo, size_t est_xy_card);

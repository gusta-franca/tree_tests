#pragma once

#include "auto_relate.h"
#include "fd_input.h"
#include "metrics.h"

Results compute_metrics(const ColumnarData& data, const FDSpec& fd, const std::string& hash_algo, size_t est_xy_card, const AutoRelateFDConfig& config);

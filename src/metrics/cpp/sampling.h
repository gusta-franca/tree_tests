#pragma once
#include <optional>
#include <vector>
#include "csv_index.h"

struct SampleResult {
    ColumnarData data;
    std::vector<int> violation_rows;
};

SampleResult sample_columnar_data(const ColumnarData& data,
                                   const std::vector<int>& violation_rows,
                                   size_t sample_size,
                                   std::optional<uint64_t> seed);

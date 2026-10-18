#include <algorithm>
#include <random>
#include <ranges>
#include "sampling.h"

// build sampled ColumnarData and violation_rows
SampleResult sample_columnar_data(const ColumnarData& data,
                                   const std::vector<int>& violation_rows,
                                   size_t sample_size,
                                   std::optional<uint64_t> seed) {
    SampleResult result;

    if (data.num_rows <= sample_size) {
        result.data = data;
        result.violation_rows = violation_rows;

        return result;
    }

    // build is_violation
    std::vector<bool> is_violation(data.num_rows, false);
    for (int idx : violation_rows) {
        if (idx >= 0 && static_cast<size_t>(idx) < data.num_rows) {
            is_violation[idx] = true;
        }
    }

    // use std::sample, maybe search for a more elaborate sampling tool?
    auto indices = std::views::iota(size_t(0), data.num_rows);
    std::vector<size_t> sampled_indices(sample_size);

    std::mt19937_64 rng(seed.has_value() ? seed.value() : std::random_device{}());
    std::sample(indices.begin(), indices.end(), sampled_indices.begin(), sample_size, rng);

    result.data.column_names = data.column_names;
    result.data.dicts = data.dicts;
    result.data.num_rows = sampled_indices.size();
    result.data.columns.resize(data.columns.size());

    for (auto& col : result.data.columns) {
        col.reserve(sampled_indices.size());
    }

    // build sampled ColumnarData and sampled violation_rows
    for (size_t new_idx = 0; new_idx < sampled_indices.size(); new_idx++) {
        size_t og_idx = sampled_indices[new_idx];

        for (size_t col = 0; col < data.columns.size(); col++) {
            result.data.columns[col].push_back(data.columns[col][og_idx]);
        }

        if (is_violation[og_idx]) {
            result.violation_rows.push_back(static_cast<int>(new_idx));
        }
    }

    return result;
}

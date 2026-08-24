#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <vector>

#include "auto_relate.h"
#include "ankerl/unordered_dense.h"

struct GroupInfo {
    std::vector<uint32_t> distinct_values;
    std::vector<uint64_t> counts;
 
    void observe(uint32_t value) {
        for (size_t i = 0; i < distinct_values.size(); ++i) {
            if (distinct_values[i] == value) {
                ++counts[i];
                return;
            }
        }
        distinct_values.push_back(value);
        counts.push_back(1);
    }
};


AutoRelateFDResult compute_auto_relate_fd(
    const ColumnarData& data,
    const std::string& left_col,
    const std::string& right_col,
    const AutoRelateFDConfig& config) {

    using clock = std::chrono::steady_clock;

    AutoRelateFDResult result;
    result.left_col = left_col;
    result.right_col = right_col;

    const size_t left_idx = data.get_column_index(left_col);
    const size_t right_idx = data.get_column_index(right_col);

    if (left_idx == SIZE_MAX || right_idx == SIZE_MAX) {
        result.score = 1.0;
        return result;
    }

    const auto& left_data = data.columns[left_idx];
    const auto& right_data = data.columns[right_idx];
    const size_t n = data.num_rows;

    // TODO
}

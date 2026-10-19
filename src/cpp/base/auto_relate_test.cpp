#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

#include "auto_relate.h"
#include "core/csv_index.h"
#include "core/metrics.h"
#include "core/metrics_config.h"
#include "core/sampling.h"

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <csv_file> <left_cols> <right_col> <dirty|clean>" << std::endl;
        return 1;
    }

    std::string csv_file = argv[1];
    std::string lhs_str = argv[2];
    std::string right_col = argv[3];
    std::string mode = (argc > 4) ? argv[4] : "dirty";

    std::vector<std::string> left_cols;
    std::stringstream lhs_ss(lhs_str);
    std::string col;
    while (std::getline(lhs_ss, col, '|')) {
        left_cols.push_back(col);
    }

    std::string v_rows_str;
    std::getline(std::cin, v_rows_str);
    std::vector<int> violation_rows = parse_violation_rows(v_rows_str);

    AutoRelateFDConfig config;
    config.dirty_data = (mode == "dirty");
    config.use_independence_test = config.dirty_data;

    std::chrono::duration<double> load_time_s(0);
    auto load_start = std::chrono::steady_clock::now();

    ColumnarData data;
    if (!load_csv_columnar(csv_file, data, false)) {
        std::cerr << "Error: Failed to load CSV file" << std::endl;
        return 1;
    }

#if ENABLE_SAMPLING
    if (data.num_rows > SAMPLE_THRESHOLD) {
        std::optional<uint64_t> seed;

        if (const char* s = std::getenv("SAMPLE_SEED")) {
            seed = std::stoull(s);
        }

        SampleResult res = sample_columnar_data(data, violation_rows, SAMPLE_SIZE, seed);
        data = std::move(res.data);
        violation_rows = std::move(res.violation_rows);
    }
#endif

    auto load_end = std::chrono::steady_clock::now();
    load_time_s = (load_end - load_start);

    AutoRelateFDResult result = compute_auto_relate_fd(data, left_cols, right_col, violation_rows, config);
    

    std::string left_cols_str;
    for (size_t i = 0; i < result.left_cols.size(); i++) {
        if (i > 0) {
            left_cols_str += ",";
        }
        
        left_cols_str += result.left_cols[i];
    }

    metric("LeftCols", left_cols_str);
    metric("RightCol", result.right_col);
    metric("Score", result.score);
    metric("IndependencePvalue", result.independence_pvalue);
    metric("IndependenceUsed", result.independence_used ? "true" : "false");
    metric("IndependenceRejected", result.independence_rejected ? "true" : "false");
    metric("IsReliable", result.is_reliable ? "true" : "false");
    metric("ViolationCount", result.violation_count);
    metric("ViolationRate", result.violation_rate);
    metric("LoadTimeSeconds", load_time_s.count());
    metric("BuildTimeSeconds", result.build_time_s);
    metric("ComputeTimeSeconds", result.compute_time_s);
    metric("InternalMemoryEstimateMB", result.memory_used_mb);

    return 0;
}

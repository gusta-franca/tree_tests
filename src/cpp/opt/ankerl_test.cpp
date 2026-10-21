#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>  
#include <string>

#include "ankerl_metrics.h"
#include "auto_relate.h"
#include "csv_index.h"
#include "fd_input.h"
#include "metrics.h"
#include "metrics_config.h"
#include "sampling.h"


int main(int argc, char* argv[]) {
    if (argc < 4) {
        return 1;
    }

    std::string csv_file = argv[1];
    std::string lhs_str = argv[2];
    std::string rhs_str = argv[3];
    std::string algo = (argc > 4) ? argv[4] : "auto";
    std::string mode = (argc > 5) ? argv[5] : "dirty";

    std::string v_rows_str;
    std::getline(std::cin, v_rows_str);
    std::vector<int> violation_rows = parse_violation_rows(v_rows_str);

    AutoRelateFDConfig config;
    config.dirty_data = (mode == "dirty");
    config.use_independence_test = config.dirty_data;

    std::chrono::duration<double> load_time_s(0);
    auto load_start = std::chrono::steady_clock::now();

    FDSpec fd;
    fd.rhs_column = rhs_str;
    std::stringstream ss(lhs_str);
    std::string col;
    while (std::getline(ss, col, '|')) {
        fd.lhs_columns.push_back(col);
    }

    ColumnarData data;
    size_t est_xy_card = 0;
    if (!load_csv_columnar(csv_file, data, fd, est_xy_card, false)) {
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

    Results result = compute_metrics(data, fd, algo, est_xy_card, config, violation_rows);

    print_metric("MuPlus", result.mu_plus);
    print_metric("RfiPrimePlus", result.rfi_prime_plus);
    print_metric("AutoRelateScore", result.auto_relate_score);
    print_metric("AutoRelateViolationCount", result.auto_relate_violation_count);
    print_metric("AutoRelateViolationRate", result.auto_relate_violation_rate);
    print_metric("AutoRelateIsReliable", result.auto_relate_is_reliable ? "true" : "false");
    print_metric("IndependenceUsed", result.independence_used ? "true" : "false");
    print_metric("IndependenceRejected", result.independence_rejected ? "true" : "false");
    print_metric("IndependencePvalue", result.independence_pvalue);
    print_metric("G1Score", result.g1_score);
    print_metric("G2Score", result.g2_score);
    print_metric("G3PrimeScore", result.g3_prime_score);
    print_metric("LoadTimeSeconds", load_time_s.count());
    print_metric("BuildTimeSeconds", result.build_time_s);
    print_metric("ComputeTimeSeconds", result.compute_time_s);
    print_metric("MuTimeSeconds", result.mu_compute_time_s);
    print_metric("RfiTimeSeconds", result.rfi_compute_time_s);
    print_metric("AutoRelateTimeSeconds", result.auto_relate_compute_time_s);
    print_metric("IndependenceTimeSeconds", result.independence_compute_time_s);
    print_metric("G1TimeSeconds", result.g1_compute_time_s);
    print_metric("G2TimeSeconds", result.g2_compute_time_s);
    print_metric("G3PrimeTimeSeconds", result.g3_prime_compute_time_s);
    print_metric("InternalMemoryEstimateMB", result.memory_used_mb);

    return 0;
}

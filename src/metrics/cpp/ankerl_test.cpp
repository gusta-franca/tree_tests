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
    std::vector<int> violation_rows = parse_violation_rows(v_rows_str);  // reuse from auto_relate_test.cpp

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

    auto load_end = std::chrono::steady_clock::now();
    load_time_s = (load_end - load_start);

    Results result = compute_metrics(data, fd, algo, est_xy_card, config, violation_rows);

    std::cout << "RESULT_JSON: {"
          << "\"mu_plus\": " << result.mu_plus << ", "
          << "\"rfi_prime_plus\": " << result.rfi_prime_plus << ", "
          << "\"auto_relate_score\": " << result.auto_relate_score << ", "
          << "\"auto_relate_violation_count\": " << result.auto_relate_violation_count << ", "
          << "\"auto_relate_violation_rate\": " << result.auto_relate_violation_rate << ", "
          << "\"auto_relate_is_reliable\": " << (result.auto_relate_is_reliable ? "true" : "false") << ", "
          << "\"independence_used\": " << (result.independence_used ? "true" : "false") << ", "
          << "\"independence_rejected\": " << (result.independence_rejected ? "true" : "false") << ", "
          << "\"independence_pvalue\": " << result.independence_pvalue << ", "
          << "\"g1_score\": " << result.g1_score << ", "
          << "\"g2_score\": " << result.g2_score << ", "
          << "\"g3_prime_score\": " << result.g3_prime_score << ", "
          << "\"load_time_s\": " << load_time_s.count() << ", "
          << "\"build_time_s\": " << result.build_time_s << ", "
          << "\"compute_time_s\": " << result.compute_time_s << ", "
          << "\"mu_time_s\": " << result.mu_compute_time_s << ", "
          << "\"rfi_time_s\": " << result.rfi_compute_time_s << ", "
          << "\"auto_relate_time_s\": " << result.auto_relate_compute_time_s << ", "
          << "\"independence_time_s\": " << result.independence_compute_time_s << ", "
          << "\"g1_time_s\": " << result.g1_compute_time_s << ", "
          << "\"g2_time_s\": " << result.g2_compute_time_s << ", "
          << "\"g3_prime_time_s\": " << result.g3_prime_compute_time_s << ", "
          << "\"memory_used_mb\": " << result.memory_used_mb
          << "}" << std::endl;

    return 0;
}

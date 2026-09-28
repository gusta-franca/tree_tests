#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>  
#include <string>

#include "csv_index.h"
#include "fd_input.h"
#include "ankerl_metrics.h"


int main(int argc, char* argv[]) {
    if (argc < 4) {
        return 1;
    }

    std::string csv_file = argv[1];
    std::string lhs_str = argv[2];
    std::string rhs_str = argv[3];
    std::string algo = "auto";
    
    if (argc > 4) {
        algo = argv[4]; 
    }

    std::chrono::duration<double> load_time_s(0);
    auto load_start = std::chrono::steady_clock::now();

    FDSpec fd;
    fd.rhs_column = rhs_str;
    std::stringstream ss(lhs_str);
    std::string col;
    while (std::getline(ss, col, ',')) {
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

    Results result = compute_metrics(data, fd, algo, est_xy_card, AutoRelateFDConfig());

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
          << "\"load_time_s\": " << load_time_s.count() << ", "
          << "\"build_time_s\": " << result.build_time_s << ", "
          << "\"compute_time_s\": " << result.compute_time_s << ", "
          << "\"mu_time_s\": " << result.mu_compute_time_s << ", "
          << "\"rfi_time_s\": " << result.rfi_compute_time_s << ", "
          << "\"auto_relate_time_s\": " << result.auto_relate_compute_time_s << ", "
          << "\"independence_time_s\": " << result.independence_compute_time_s << ", "
          << "\"memory_used_mb\": " << result.memory_used_mb
          << "}" << std::endl;

    return 0;
}

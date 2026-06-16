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

    Results result = compute_metrics(data, fd, algo, est_xy_card);

    std::cout << "RESULT_JSON: {"
            << "\"mu_plus\": " << result.mu_plus << ", "
            << "\"rfi_prime_plus\": " << result.rfi_prime_plus << ", "
            << "\"load_time_s\": " << load_time_s.count() << ", "
            << "\"build_time_s\": " << result.build_time_s << ", "
            << "\"compute_time_s\": " << result.compute_time_s << ", "
            << "\"memory_used_mb\": " << result.memory_used_mb
            << "}" << std::endl;

    return 0;
}

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <sstream>

#include "fd_input.h"
#include "fd_metrics_partitioned.h"


int main(int argc, char* argv[]) {
    if (argc < 4) {
        return 1;
    }

    std::string csv_file = argv[1];
    std::string lhs_str = argv[2];
    std::string rhs_str = argv[3];

    FDSpec fd;
    fd.rhs_column = rhs_str;
    
    std::stringstream ss(lhs_str);
    std::string column;
    while (std::getline(ss, column, ',')) {
        fd.lhs_columns.push_back(column);
    }

    CSVIndex index;
    // bool parallel_build = true;

    if (!load_csv_index(csv_file, index)) {
        std::cerr << "Error: Failed to load CSV file" << std::endl;
        return 1;
    }

    PdepResult result = compute_pdep(index, fd);

    std::cout << result.metric_value << "," 
              << result.build_time_s << "," 
              << result.compute_time_s << "," 
              << result.memory_used_mb << std::endl;

    return 0;
}

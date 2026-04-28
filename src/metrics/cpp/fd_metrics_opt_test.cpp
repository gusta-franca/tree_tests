// fd_metrics_opt_test.cpp
// Test program for optimized FD metrics computation

#include <iomanip>
#include <iostream>
#include <fstream>
#include <sys/resource.h>
#include "fd_metrics_opt.h"
#include "csv_index.h"

void print_usage(const char* prog_name) {
    std::cerr << "Usage: " << prog_name << " <csv_file> <fd_file> [-o output_file] [-m metric] [-v]" << std::endl;
    std::cerr << "  metric: mu_plus (default) or mu" << std::endl;
    std::cerr << "  -v: verbose output" << std::endl;
}


int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string csv_file = argv[1];
    std::string lhs_str = argv[2];
    std::string rhs_str = argv[3];

    std::string algo = "auto";
    if (argc > 4) {
        algo = argv[4]; 
    }
    
    MetricType metric_type = MetricType::MU_PLUS;
    bool verbose = false;

    FDSpec fd;
    fd.rhs_column = rhs_str;
    std::stringstream ss(lhs_str);
    std::string col;
    while (std::getline(ss, col, ',')) {
        fd.lhs_columns.push_back(col);
    }
    
    // Load CSV data (no indexes!)
    ColumnarData data;
    if (!load_csv_columnar(csv_file, data, verbose)) {
        std::cerr << "Error: Failed to load CSV file" << std::endl;
        return 1;
    }

    FDMetricResult result = compute_single_fd_metric_opt(data, fd, metric_type, verbose, algo);
    
    return 0;
}

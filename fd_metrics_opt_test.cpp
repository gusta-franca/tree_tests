// fd_metrics_opt_test.cpp
// Test program for optimized FD metrics computation

#include "fd_metrics_opt.h"
#include <iostream>
#include <fstream>
#include <iomanip>

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
    std::string fd_file = argv[2];
    std::string output_file;
    std::string metric_str = "mu_plus";
    bool verbose = false;
    
    // Parse optional arguments
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "-m" && i + 1 < argc) {
            metric_str = argv[++i];
        } else if (arg == "-v") {
            verbose = true;
        }
    }
    
    MetricType metric = string_to_metric_type(metric_str);
    
    std::cout << "=== Optimized FD Metrics Computation ===" << std::endl;
    std::cout << "CSV file: " << csv_file << std::endl;
    std::cout << "FD file: " << fd_file << std::endl;
    std::cout << "Metric: " << metric_type_to_string(metric) << std::endl;
    std::cout << std::endl;
    
    // Load FD specifications
    std::vector<FDSpec> fds;
    if (!load_fds_file(fd_file, fds)) {
        std::cerr << "Error: Failed to load FDs from file" << std::endl;
        return 1;
    }
    if (fds.empty()) {
        std::cerr << "Error: No FDs loaded from file" << std::endl;
        return 1;
    }
    std::cout << "Loaded " << fds.size() << " FD specifications" << std::endl;
    
    // Load CSV data (no indexes!)
    ColumnarData data;
    if (!load_csv_columnar(csv_file, data, verbose)) {
        std::cerr << "Error: Failed to load CSV file" << std::endl;
        return 1;
    }
    
    // Compute metrics
    auto results = compute_fd_metrics_opt(data, fds, metric, verbose);
    
    // Write results
    if (!output_file.empty()) {
        std::ofstream out(output_file);
        if (!out.is_open()) {
            std::cerr << "Error: Cannot write to " << output_file << std::endl;
            return 1;
        }
        
        // Header
        out << "lhs,rhs," << metric_type_to_string(metric) << ",is_key,lhs_size,"
            << "lhs_uniqueness,pdep_xy,pdep_y,dom_x_size,r_size" << std::endl;
        
        // Data
        for (const auto& result : results) {
            out << "\"";
            for (size_t i = 0; i < result.fd.lhs_columns.size(); ++i) {
                out << result.fd.lhs_columns[i];
                if (i + 1 < result.fd.lhs_columns.size()) out << ",";
            }
            out << "\",\"" << result.fd.rhs_column << "\","
                << std::fixed << std::setprecision(6) << result.metric_value << ","
                << (result.is_key ? "true" : "false") << ","
                << result.lhs_size << ","
                << result.lhs_uniqueness << ","
                << result.pdep_xy << ","
                << result.pdep_y << ","
                << result.dom_x_size << ","
                << result.r_size << std::endl;
        }
        
        std::cout << "\nResults written to: " << output_file << std::endl;
    }
    
    // Summary
    std::cout << "\n=== Results Summary ===" << std::endl;
    for (const auto& result : results) {
        std::cout << "[";
        for (size_t i = 0; i < result.fd.lhs_columns.size(); ++i) {
            std::cout << result.fd.lhs_columns[i];
            if (i + 1 < result.fd.lhs_columns.size()) std::cout << ",";
        }
        std::cout << "] -> " << result.fd.rhs_column << ": "
                  << metric_type_to_string(result.metric_type) << " = "
                  << std::fixed << std::setprecision(3) << result.metric_value
                  << (result.is_key ? " (KEY)" : "") << std::endl;
    }
    
    return 0;
}

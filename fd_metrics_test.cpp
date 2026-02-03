// fd_metrics_test.cpp
// Test program for computing FD metrics and saving results to CSV

#include <iostream>
#include <fstream>
#include <iomanip>
#include <chrono>
#include "csv_index.h"
#include "fd_input.h"
#include "fd_metrics.h"

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " <csv_file> <fds_file> [options]" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  -o <output_file>  Output CSV file (default: metrics_output.csv)" << std::endl;
    std::cout << "  -m <metric>       Metric to compute: mu_plus (default) or mu" << std::endl;
    std::cout << "  -p                Use parallel CSV loading" << std::endl;
    std::cout << "  -v                Verbose output" << std::endl;
    std::cout << "\nExample:" << std::endl;
    std::cout << "  " << prog_name << " ./data/itax_small.csv ./data/itax.fds -o results.csv -m mu_plus" << std::endl;
}

bool write_results_to_csv(
    const std::string& output_file,
    const std::vector<FDMetricResult>& results,
    const std::string& dataset_name,
    MetricType metric_type) {
    
    std::ofstream out(output_file);
    if (!out.is_open()) {
        std::cerr << "Error: Cannot open output file: " << output_file << std::endl;
        return false;
    }
    
    std::string metric_name = metric_type_to_string(metric_type);
    
    // Write header
    out << "dataset,fd," << metric_name << ",is_key,lhs_size,lhs_uniqueness,pdep_xy,pdep_y,dom_x_size,r_size\n";
    
    // Write results
    out << std::fixed << std::setprecision(10);
    for (const auto& result : results) {
        // Build FD string: ["col1","col2"]->rhs
        out << dataset_name << ",\"[";
        for (size_t i = 0; i < result.fd.lhs_columns.size(); ++i) {
            out << "\"\"" << result.fd.lhs_columns[i] << "\"\"";
            if (i < result.fd.lhs_columns.size() - 1) {
                out << ",";
            }
        }
        out << "]->" << result.fd.rhs_column << "\"";
        
        // Write metrics
        out << "," << result.metric_value;
        out << "," << (result.is_key ? "True" : "False");
        out << "," << result.lhs_size;
        out << "," << result.lhs_uniqueness;
        out << "," << result.pdep_xy;
        out << "," << result.pdep_y;
        out << "," << result.dom_x_size;
        out << "," << result.r_size;
        out << "\n";
    }
    
    out.close();
    return true;
}

int main(int argc, char* argv[]) {
    using namespace std::chrono;
    
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string csv_file = argv[1];
    std::string fds_file = argv[2];
    std::string output_file = "metrics_output.csv";
    MetricType metric_type = MetricType::MU_PLUS;  // default
    bool parallel = false;
    bool verbose = false;
    
    // Parse options
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "-m" && i + 1 < argc) {
            metric_type = string_to_metric_type(argv[++i]);
        } else if (arg == "-p") {
            parallel = true;
        } else if (arg == "-v") {
            verbose = true;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }
    
    std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║     FD Metrics Computation Tool       ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;
    
    auto t_total_start = steady_clock::now();
    
    // Load CSV and build index
    CSVIndex index;
    if (!load_csv_index(csv_file, index, parallel)) {
        return 1;
    }
    
    // Load FD specifications
    std::vector<FDSpec> fds;
    if (!load_fds_file(fds_file, fds)) {
        return 1;
    }
    
    if (fds.empty()) {
        std::cerr << "Error: No FDs loaded" << std::endl;
        return 1;
    }
    
    // Compute metrics
    auto results = compute_fd_metrics(index, fds, metric_type, verbose);
    
    // Extract dataset name from CSV file
    std::string dataset_name = csv_file;
    size_t last_slash = dataset_name.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        dataset_name = dataset_name.substr(last_slash + 1);
    }
    size_t dot_pos = dataset_name.find(".csv");
    if (dot_pos != std::string::npos) {
        dataset_name = dataset_name.substr(0, dot_pos);
    }
    
    // Write results to CSV
    std::cout << "\n=== Writing Results ===" << std::endl;
    if (write_results_to_csv(output_file, results, dataset_name, metric_type)) {
        std::cout << "✓ Results saved to: " << output_file << std::endl;
    } else {
        return 1;
    }
    
    auto t_total_end = steady_clock::now();
    auto total_s = duration_cast<seconds>(t_total_end - t_total_start).count();
    
    std::cout << "\n=== Summary ===" << std::endl;
    std::cout << "Total FDs processed: " << results.size() << std::endl;
    std::cout << "Metric computed: " << metric_type_to_string(metric_type) << std::endl;
    std::cout << "Total time: " << total_s << " seconds" << std::endl;
    std::cout << "Output file: " << output_file << std::endl;
    
    // Print sample results
    if (!results.empty()) {
        std::cout << "\nSample results (first 3 FDs):" << std::endl;
        for (size_t i = 0; i < std::min(size_t(3), results.size()); ++i) {
            const auto& r = results[i];
            std::cout << "  FD " << (i + 1) << ": ";
            r.fd.print();
            std::cout << "    " << metric_type_to_string(metric_type) << " = " << r.metric_value 
                      << ", is_key = " << (r.is_key ? "true" : "false") << std::endl;
        }
    }
    
    std::cout << "\n✓ Done!" << std::endl;
    
    return 0;
}

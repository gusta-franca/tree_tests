#include <chrono>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>

#include "csv_index.h"
#include "fd_input.h"
#include "metrics.h"
#include "metrics_config.h"
#include "naive_metrics.h"
#include "sampling.h"

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <csv_file> <left_cols> <right_col>" << std::endl;
        return 1;
    }

    std::string csv_file = argv[1];
    std::string lhs_str = argv[2];
    std::string rhs_str = argv[3];

    FDSpec fd;
    fd.rhs_column = rhs_str;
    std::stringstream ss(lhs_str);
    std::string col;
    while (std::getline(ss, col, '|')) {
        fd.lhs_columns.push_back(col);
    }

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

        SampleResult res = sample_columnar_data(data, {}, SAMPLE_SIZE, seed);
        data = std::move(res.data);
    }
#endif

    auto load_end = std::chrono::steady_clock::now();
    load_time_s = (load_end - load_start);

    naive_metrics::G2Result result = naive_metrics::g2(data, fd);

    print_metric("LoadTimeSeconds", load_time_s.count());
    print_metric("G2Score", result.result);
    print_metric("BuildTimeSeconds", result.build_time_s);
    print_metric("ComputeTimeSeconds", result.compute_time_s);

    return 0;
}

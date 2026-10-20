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

    auto load_end = std::chrono::steady_clock::now();
    load_time_s = (load_end - load_start);

#if ENABLE_MU_PLUS
    naive_metrics::MuPlusResult mu_result = naive_metrics::mu_plus(data, fd);
#else
    naive_metrics::MuPlusResult mu_result;
    mu_result.result = std::numeric_limits<double>::quiet_NaN();
#endif

#if ENABLE_RFI_PRIME_PLUS
    naive_metrics::RfiPrimePlusResult rfi_result = naive_metrics::rfi_prime_plus(data, fd);
#else
    naive_metrics::RfiPrimePlusResult rfi_result;
    rfi_result.result = std::numeric_limits<double>::quiet_NaN();
#endif

#if ENABLE_G1
    naive_metrics::G1Result g1_result = naive_metrics::g1(data, fd);
#else
    naive_metrics::G1Result g1_result;
    g1_result.result = std::numeric_limits<double>::quiet_NaN();
#endif

#if ENABLE_G2
    naive_metrics::G2Result g2_result = naive_metrics::g2(data, fd);
#else
    naive_metrics::G2Result g2_result;
    g2_result.result = std::numeric_limits<double>::quiet_NaN();
#endif

#if ENABLE_G3_PRIME
    naive_metrics::G3PrimeResult g3_result = naive_metrics::g3_prime(data, fd);
#else
    naive_metrics::G3PrimeResult g3_result;
    g3_result.result = std::numeric_limits<double>::quiet_NaN();
#endif
    
    print_metric("LoadTimeSeconds", load_time_s.count());
    
    print_metric("MuPlus", mu_result.result);
    print_metric("MuPlusIsKey", mu_result.is_key ? "true" : "false");
    print_metric("MuPlusLhsUniqueness", mu_result.lhs_uniqueness);
    print_metric("MuPlusLhsSize", mu_result.lhs_size);
    print_metric("MuBuildTimeSeconds", mu_result.build_time_s);
    print_metric("MuComputeTimeSeconds", mu_result.compute_time_s);

    print_metric("RfiPrimePlus", rfi_result.result);
    print_metric("RfiFi", rfi_result.fi);
    print_metric("RfiBiasEstimator", rfi_result.bias_estimator);
    print_metric("RfiBuildTimeSeconds", rfi_result.build_time_s);
    print_metric("RfiComputeTimeSeconds", rfi_result.compute_time_s);

    print_metric("G1Score", g1_result.result);
    print_metric("G1BuildTimeSeconds", g1_result.build_time_s);
    print_metric("G1ComputeTimeSeconds", g1_result.compute_time_s);

    print_metric("G2Score", g2_result.result);
    print_metric("G2BuildTimeSeconds", g2_result.build_time_s);
    print_metric("G2ComputeTimeSeconds", g2_result.compute_time_s);

    print_metric("G3PrimeScore", g3_result.result);
    print_metric("G3PrimeBuildTimeSeconds", g3_result.build_time_s);
    print_metric("G3PrimeComputeTimeSeconds", g3_result.compute_time_s);

    return 0;
}

#pragma once

#include <vector>

#include "csv_index.h"
#include "fd_input.h"
#include "metrics.h"

struct MuPlusResult {
    double result = 0.0;
    bool is_key = false;
    double lhs_uniqueness = 0.0;
    size_t lhs_size = 0;

    double build_time_s = 0.0;
    double compute_time_s = 0.0;
};

struct RfiPrimePlusResult {
    double result = 0.0;
    double fi = 0.0;
    double bias_estimator = 0.0;
    double build_time_s = 0.0;
    double compute_time_s = 0.0;
};


double pdep(const ColumnarData& data, const std::vector<size_t>& lhs_indices, size_t rhs_idx);

double pdep_self(const ColumnarData& data, size_t rhs_idx);

MuPlusResult mu_plus(const ColumnarData& data, const FDSpec& fd);


double expected_mi(size_t num_rows, const std::vector<uint32_t>& x_counts, const std::vector<uint32_t>& y_counts);

double fraction_of_information(const ColumnarData& data, const std::vector<size_t>& lhs_indices, size_t rhs_idx);

RfiPrimePlusResult rfi_prime_plus(const ColumnarData& data, const FDSpec& fd);

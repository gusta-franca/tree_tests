#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

struct Results {
    double mu_plus;
    double rfi_prime_plus;
    double build_time_s;
    double compute_time_s;
    double memory_used_mb;
};

double mu_plus(size_t num_rows, size_t dom_x_size, double pdep_XY, double pdep_Y);

double rfi_prime_plus(size_t num_rows, const std::vector<uint32_t>& x_counts, const std::vector<uint32_t>& y_counts, double shannon_XY, double shannon_Y);

double expected_mi(size_t total_rows, const std::vector<uint32_t>& x_counts, const std::vector<uint32_t>& y_counts);
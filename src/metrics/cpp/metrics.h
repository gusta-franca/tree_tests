#pragma once
#include <vector>
#include <cstdint>
#include <cstddef>

struct Results {
    double mu_plus;

    double rfi_prime_plus;

    double auto_relate_score;
    size_t auto_relate_violation_count;
    double auto_relate_violation_rate;
    bool auto_relate_is_reliable;
    bool independence_used;
    bool independence_rejected;
    double independence_pvalue;

    double g1_score;
    double g2_score;
    double g3_prime_score;

    double build_time_s;
    double compute_time_s;
    double mu_compute_time_s;
    double rfi_compute_time_s;
    double auto_relate_compute_time_s;
    double independence_compute_time_s;
    double g1_compute_time_s;
    double g2_compute_time_s;
    double g3_prime_compute_time_s;
    double memory_used_mb;
};

struct AutoRelateResult {
    double score = 1.0;
    size_t violation_count = 0;
    double violation_rate = 0.0;
};

double mu_plus(size_t num_rows, size_t dom_x_size, double pdep_XY, double pdep_Y);

// e_mi for a single (a, b) pair; exposed so naive metrics can use it oo
double expected_mi_pair(int n, int a, int b, const std::vector<double>& lgamma_cache);

double rfi_prime_plus(size_t num_rows, const std::vector<uint32_t>& x_counts, const std::vector<uint32_t>& y_counts, double shannon_XY, double shannon_Y);

AutoRelateResult auto_relate(size_t num_rows, const std::vector<uint32_t>& majority_counts, const std::vector<uint32_t>& majority_y_ids);

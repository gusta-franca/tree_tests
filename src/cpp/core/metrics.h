#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

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

template <typename T>
void metric(const std::string& name, const T& value);

double mu_plus(size_t num_rows, size_t dom_x_size, double pdep_XY, double pdep_Y);

// e_mi for a single (a, b) pair; exposed so naive metrics can use it oo
template <typename lgammaT>
double expected_mi_pair(int n, int a, int b, lgammaT&& lgamma_func) {
    double log_comb_n_a = lgamma_func   (n) - lgamma_func(a) - lgamma_func(n - a);

    int k0 = std::max(0, a + b - n);
    int k_max = std::min(a, b);

    double log_p0 = (lgamma_func(b) - lgamma_func(k0) - lgamma_func(b - k0))
                  + (lgamma_func(n - b) - lgamma_func(a - k0) - lgamma_func(n - a - b + k0))
                  - log_comb_n_a;

    double p0 = std::exp(log_p0);
    double e_mi = 0.0;

    if (k0 != 0) {
        double k0_d = static_cast<double>(k0);
        e_mi += p0 * (k0_d / n) * std::log2((k0_d * n) / (static_cast<double>(a) * b));
    }

    for (int k1 = k0 + 1; k1 <= k_max; k1++) {
        double k0_d = static_cast<double>(k0);
        double k1_d = static_cast<double>(k1);

        p0 *= ((static_cast<double>(a) - k0_d) * (static_cast<double>(b) - k0_d)) /
              (k1_d * (static_cast<double>(n) - a - b + k1_d));

        e_mi += p0 * (k1_d / n) * std::log2((k1_d * n) / (static_cast<double>(a) * b));
        k0 = k1;
    }

    return e_mi;
}

double rfi_prime_plus(size_t num_rows, const std::vector<uint32_t>& x_counts, const std::vector<uint32_t>& y_counts, double shannon_XY, double shannon_Y);

AutoRelateResult auto_relate(size_t num_rows, const std::vector<uint32_t>& majority_counts, const std::vector<uint32_t>& majority_y_ids);

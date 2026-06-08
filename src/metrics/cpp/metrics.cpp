#include <algorithm>
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <vector>
#include "metrics.h"

double mu_plus(size_t num_rows, size_t dom_x_size, double pdep_XY, double pdep_Y) {
    double mu = 0.0;
    
    if (num_rows == dom_x_size) {
        mu = 1.0;
    }
    else {
        mu = 1.0 
           - ((1.0 - pdep_XY) / (1.0 - pdep_Y)) 
           * (static_cast<double>(num_rows - 1) / static_cast<double>(num_rows - dom_x_size));
    }

    return std::max(0.0, mu);
}

double expected_mi(size_t num_rows, const std::vector<uint32_t>& x_counts, const std::vector<uint32_t>& y_counts) {
    int n = static_cast<int>(num_rows);
    double m = 0.0;

    std::vector<double> lgamma_cache(n + 1);
    for (int i = 0; i <= n; i++) {
        lgamma_cache[i] = std::lgamma(i + 1);
    }

    // compress the zipf long tail
    std::unordered_map<int, uint64_t> x_freq;
    for (int a : x_counts) {
        x_freq[a]++;
    }

    std::unordered_map<int, uint64_t> y_freq;
    for (int b : y_counts) {
        y_freq[b]++;
    }

    // iterate over unique sizes
    for (const auto& x_pair : x_freq) {
        int a = x_pair.first;
        uint64_t a_freq = x_pair.second;
        
        double log_comb_n_a = lgamma_cache[n] - lgamma_cache[a] - lgamma_cache[n - a];

        for (const auto& y_pair : y_freq) {
            int b = y_pair.first;
            uint64_t freq_b = y_pair.second;

            int k0 = std::max(0, a + b - n);
            int k_max = std::min(a, b);

            double log_p0 = (lgamma_cache[b] - lgamma_cache[k0] - lgamma_cache[b - k0])
                          + (lgamma_cache[n - b] - lgamma_cache[a - k0] - lgamma_cache[n - a - b + k0])
                          - log_comb_n_a;
            
            double p0 = std::exp(log_p0);
            double e_mi = 0.0; // mi for just one 'a' and 'b' pair

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

            m += e_mi * static_cast<double>(a_freq) * static_cast<double>(freq_b);
        }
    }

    return m;
}

double rfi_prime_plus(size_t num_rows, const std::vector<uint32_t>& x_counts, const std::vector<uint32_t>& y_counts, double shannon_XY, double shannon_Y) {
    double rfi_prime = 0.0;

    double mi = shannon_Y - shannon_XY;
    double e_mi = expected_mi(num_rows, x_counts, y_counts);

    if (shannon_Y > e_mi) { 
        rfi_prime = (mi-e_mi) / (shannon_Y-e_mi);
    }

    return std::max(0.0, rfi_prime);
}

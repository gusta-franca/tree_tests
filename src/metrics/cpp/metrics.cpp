#include <algorithm>
#include <cstdint>
#include <cmath>
#include <unordered_map>
#include <vector>
#include "metrics.h"

// "opt" means the counting is made considering the data structures used for counting values in the ankerl/simd implementations;
// the other implementations are the closest 1:1 implementation from python
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

double expected_mi_pair(int n, int a, int b, const std::vector<double>& lgamma_cache) {
    double log_comb_n_a = lgamma_cache[n] - lgamma_cache[a] - lgamma_cache[n - a];

    int k0 = std::max(0, a + b - n);
    int k_max = std::min(a, b);

    double log_p0 = (lgamma_cache[b] - lgamma_cache[k0] - lgamma_cache[b - k0])
                  + (lgamma_cache[n - b] - lgamma_cache[a - k0] - lgamma_cache[n - a - b + k0])
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

double expected_mi(size_t num_rows, const std::vector<uint32_t>& x_counts, const std::vector<uint32_t>& y_counts) {
    int n = static_cast<int>(num_rows);
    double m = 0.0;

    std::vector<double> lgamma_cache(n + 1);
    for (int i = 0; i <= n; i++) {
        lgamma_cache[i] = std::lgamma(i + 1);
    }

    // compress the zipf long tail
    std::unordered_map<int, uint64_t> x_c;
    for (int a : x_counts) {
        x_c[a]++;
    }

    std::unordered_map<int, uint64_t> y_freq;
    for (int b : y_counts) {
        y_freq[b]++;
    }

    std::vector<std::pair<int, uint64_t>> x_freq(x_c.begin(), x_c.end());
    size_t x_freq_size = x_freq.size();

    // iterate over unique sizes
    // omp: indicates OpenMP
    // parallel: spawn threads (?)
    // for: divide x_freq[0..x_freq_size] evenly for all threads
    // reduction(+:m): stitch results together in m (?)
    /// is the overhead time taken being considered on the results? 

    // compare FD throughput between inter-parallelism and intra-prallelism
    // implement microsoft metric; test with their datasets
    // register every feature implemented as a journal
    // test if order of LHS attributes matter for computing the metrics/building the hash tables
    // compare HLL usage vs. counting HLL usage, when is one better than the other?

    #pragma omp parallel for reduction(+:m)
    for (size_t i = 0; i < x_freq_size; i++) {
        int a = x_freq[i].first;
        uint64_t a_freq = x_freq[i].second;
        
        double log_comb_n_a = lgamma_cache[n] - lgamma_cache[a] - lgamma_cache[n - a];

        // maybe omp for y too? is creating a vector and iterating over it (like with x) better? needs testing
        for (const auto& y_pair : y_freq) {
            int b = y_pair.first;
            uint64_t freq_b = y_pair.second;

            m += expected_mi_pair(n, a, b, lgamma_cache);
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

AutoRelateResult auto_relate(size_t num_rows, const std::vector<uint32_t>& majority_counts, const std::vector<uint32_t>& majority_y_ids) {
    AutoRelateResult result;
    if (majority_counts.empty()) { 
        return result;
    }

    std::unordered_map<uint32_t, uint64_t> value_freq;
    uint64_t lv_total = 0;

    for (size_t i = 0; i < majority_counts.size(); i++) {
        value_freq[majority_y_ids[i]] += majority_counts[i];
        lv_total += majority_counts[i];
    }

    double sum = 0.0;
    for (size_t i = 0; i < majority_counts.size(); i++) {
        if (majority_counts[i] > 1) {
            double marginal_freq = static_cast<double>(value_freq[majority_y_ids[i]]) / static_cast<double>(lv_total);
            sum += (1.0 - marginal_freq) * majority_counts[i];
        }
    }

    result.score = 1.0 - (lv_total > 0 ? sum / static_cast<double>(lv_total) : 0.0);
    result.violation_count = (num_rows > lv_total) ? (num_rows - lv_total) : 0;
    result.violation_rate = (num_rows > 0) ? static_cast<double>(result.violation_count) / num_rows : 0.0;

    return result;
}

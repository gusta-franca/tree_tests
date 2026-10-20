#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

namespace hll {
    class HyperLogLog {
    public:
        HyperLogLog(uint8_t precision = 4) : p(precision), m(1 << precision), M(m, 0) {
            double alpha = 0;
            switch (m){
            case 16:
                alpha = 0.697;
                break;
            case 32:
                alpha = 0.697;
                break;
            case 64:
                alpha = 0.709;
                break;
            default:
                alpha = 0.7213 / (1.0 + 1.079 / m);
                break;
            }

            alpha_m = alpha * m * m;
        }

        void add (uint64_t hash) {
            uint32_t b_idx = hash >> (64 - p);
            uint64_t r_idx = (hash << p) | (1ULL << (p - 1)); // (1 << (p - 1)) prevents couting zeros from b_idx
            uint8_t rank = static_cast<uint8_t>(std::countl_zero(r_idx)) + 1; // + 1 for HLL rank definition; use __builtni_clzll() if std::countl_zero() doesn't work
            
            if (rank > M[b_idx]) {
                M[b_idx] = rank;
            }
        }

        double estimate() const {
            double estimate = 0.0;
            double sum = 0.0;
            uint32_t zeros_buckets = 0;

            for (uint32_t i = 0; i < m; i++) {
                sum += 1.0 / (1ULL << M[i]);

                if (M[i] == 0) {
                    zeros_buckets++;
                }
            }

            estimate = alpha_m / sum;

            // Linear correction
            if (estimate <= 2.5 * m && zeros_buckets > 0) {
                estimate = m * std::log(static_cast<double>(m) / zeros_buckets);
            }

            return estimate;
        }

    private:
        uint8_t p; // precision
        uint32_t m; // register size
        double alpha_m;
        std::vector<uint8_t> M;

        // static inline uint8_t count_leading_zeros(uint64_t x) {
        //     uint8_t zeros_count = 0;

        //     for (int i = 63; i >= 0; i--) {
        //         if ((x & (1 << i)) == 0) {
        //             zeros_count++;
        //         }
        //         else  {
        //             break;
        //         }
        //     }

        //     return zeros_count;
        // }
    };
}

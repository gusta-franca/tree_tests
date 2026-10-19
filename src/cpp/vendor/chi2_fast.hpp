#pragma once

#include <boost/math/distributions/chi_squared.hpp>
#include <boost/math/distributions/complement.hpp>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#define CHI2_RESTRICT __restrict__
#else
#define CHI2_RESTRICT
#endif

namespace chi2fast {

struct Result {
    double statistic{};
    double pvalue{};
    int dof{};
};

struct SparseCell {
    std::uint32_t row{};
    std::uint32_t col{};
    std::uint32_t count{};
};

inline double pvalue_from_statistic(double statistic, int dof) {
    if (dof <= 0) {
        throw std::invalid_argument("degrees of freedom must be positive");
    }
    if (!(statistic >= 0.0) || !std::isfinite(statistic)) {
        throw std::invalid_argument("chi-square statistic must be finite and nonnegative");
    }

    // Cheap closed forms for two common cases.
    if (dof == 1) {
        return std::erfc(std::sqrt(0.5 * statistic));
    }
    if (dof == 2) {
        return std::exp(-0.5 * statistic);
    }

    const boost::math::chi_squared dist(static_cast<double>(dof));
    return boost::math::cdf(boost::math::complement(dist, statistic));
}

inline double critical_value(double alpha, int dof) {
    if (!(alpha > 0.0 && alpha < 1.0)) {
        throw std::invalid_argument("alpha must lie in (0,1)");
    }
    if (dof <= 0) {
        throw std::invalid_argument("degrees of freedom must be positive");
    }
    const boost::math::chi_squared dist(static_cast<double>(dof));
    return boost::math::quantile(boost::math::complement(dist, alpha));
}

// Reference Pearson chi-square implementation corresponding to
// scipy.stats.chi2_contingency(observed, correction=False) for a 2-D table.
// The table must have positive row and column marginals.
inline Result chi2_reference(const std::uint32_t* observed,
                             std::size_t rows,
                             std::size_t cols) {
    if (observed == nullptr || rows < 2 || cols < 2) {
        throw std::invalid_argument("observed must be a non-null table with at least 2 rows and 2 columns");
    }

    std::vector<double> row_sum(rows, 0.0);
    std::vector<double> col_sum(cols, 0.0);
    double total = 0.0;

    for (std::size_t i = 0; i < rows; ++i) {
        const auto* row = observed + i * cols;
        for (std::size_t j = 0; j < cols; ++j) {
            const double x = static_cast<double>(row[j]);
            row_sum[i] += x;
            col_sum[j] += x;
            total += x;
        }
    }

    if (!(total > 0.0)) {
        throw std::invalid_argument("table total must be positive");
    }
    for (double x : row_sum) {
        if (!(x > 0.0)) throw std::invalid_argument("zero row marginal is not supported");
    }
    for (double x : col_sum) {
        if (!(x > 0.0)) throw std::invalid_argument("zero column marginal is not supported");
    }

    double statistic = 0.0;
    for (std::size_t i = 0; i < rows; ++i) {
        const auto* row = observed + i * cols;
        for (std::size_t j = 0; j < cols; ++j) {
            const double expected = row_sum[i] * col_sum[j] / total;
            const double diff = static_cast<double>(row[j]) - expected;
            statistic += diff * diff / expected;
        }
    }

    const int dof = static_cast<int>((rows - 1) * (cols - 1));
    return {statistic, pvalue_from_statistic(statistic, dof), dof};
}


// Straightforward statistic-only baseline for fair microbenchmarks.
inline double chi2_reference_statistic_only(const std::uint32_t* observed,
                                             std::size_t rows,
                                             std::size_t cols) {
    if (observed == nullptr || rows < 2 || cols < 2) {
        throw std::invalid_argument("invalid table");
    }
    std::vector<double> row_sum(rows, 0.0);
    std::vector<double> col_sum(cols, 0.0);
    double total = 0.0;
    for (std::size_t i = 0; i < rows; ++i) {
        const auto* row = observed + i * cols;
        for (std::size_t j = 0; j < cols; ++j) {
            const double x = static_cast<double>(row[j]);
            row_sum[i] += x;
            col_sum[j] += x;
            total += x;
        }
    }
    if (!(total > 0.0)) throw std::invalid_argument("zero table total");
    for (double x : row_sum) if (!(x > 0.0)) throw std::invalid_argument("zero row marginal");
    for (double x : col_sum) if (!(x > 0.0)) throw std::invalid_argument("zero column marginal");

    double statistic = 0.0;
    for (std::size_t i = 0; i < rows; ++i) {
        const auto* row = observed + i * cols;
        for (std::size_t j = 0; j < cols; ++j) {
            const double expected = row_sum[i] * col_sum[j] / total;
            const double diff = static_cast<double>(row[j]) - expected;
            statistic += diff * diff / expected;
        }
    }
    return statistic;
}

// Dense optimized form:
// chi2 = N * sum_i (1/R_i) * sum_j O_ij^2/C_j - N
// This avoids constructing expected frequencies and moves reciprocals out of
// the hot inner loop. Workspace can be reused across calls to avoid allocations.
struct DenseWorkspace {
    std::vector<double> row_sum;
    std::vector<double> inv_col;

    void resize(std::size_t rows, std::size_t cols) {
        row_sum.resize(rows);
        inv_col.resize(cols);
    }
 };

inline double chi2_reference_reuse_statistic_only(const std::uint32_t* CHI2_RESTRICT observed,
                                                   std::size_t rows,
                                                   std::size_t cols,
                                                   DenseWorkspace& ws) {
    if (observed == nullptr || rows < 2 || cols < 2) throw std::invalid_argument("invalid table");
    ws.resize(rows, cols);
    std::fill(ws.row_sum.begin(), ws.row_sum.end(), 0.0);
    std::fill(ws.inv_col.begin(), ws.inv_col.end(), 0.0); // holds col sums here
    double total = 0.0;
    for (std::size_t i = 0; i < rows; ++i) {
        const auto* CHI2_RESTRICT row = observed + i * cols;
        double rs = 0.0;
        for (std::size_t j = 0; j < cols; ++j) {
            const double x = static_cast<double>(row[j]);
            rs += x;
            ws.inv_col[j] += x;
        }
        if (!(rs > 0.0)) throw std::invalid_argument("zero row marginal");
        ws.row_sum[i] = rs;
        total += rs;
    }
    for (double x : ws.inv_col) if (!(x > 0.0)) throw std::invalid_argument("zero column marginal");
    double statistic = 0.0;
    for (std::size_t i = 0; i < rows; ++i) {
        const auto* CHI2_RESTRICT row = observed + i * cols;
        for (std::size_t j = 0; j < cols; ++j) {
            const double expected = ws.row_sum[i] * ws.inv_col[j] / total;
            const double diff = static_cast<double>(row[j]) - expected;
            statistic += diff * diff / expected;
        }
    }
    return statistic;
}

inline Result chi2_dense_fast(const std::uint32_t* CHI2_RESTRICT observed,
                              std::size_t rows,
                              std::size_t cols,
                              DenseWorkspace& ws) {
    if (observed == nullptr || rows < 2 || cols < 2) {
        throw std::invalid_argument("observed must be a non-null table with at least 2 rows and 2 columns");
    }

    ws.resize(rows, cols);
    std::fill(ws.row_sum.begin(), ws.row_sum.end(), 0.0);
    std::fill(ws.inv_col.begin(), ws.inv_col.end(), 0.0);

    double total = 0.0;

    // First pass: marginals. inv_col temporarily stores column sums.
    for (std::size_t i = 0; i < rows; ++i) {
        const auto* CHI2_RESTRICT row = observed + i * cols;
        double rs = 0.0;
        for (std::size_t j = 0; j < cols; ++j) {
            const double x = static_cast<double>(row[j]);
            rs += x;
            ws.inv_col[j] += x;
        }
        ws.row_sum[i] = rs;
        total += rs;
    }

    if (!(total > 0.0)) {
        throw std::invalid_argument("table total must be positive");
    }
    for (double x : ws.row_sum) {
        if (!(x > 0.0)) throw std::invalid_argument("zero row marginal is not supported");
    }
    for (double& x : ws.inv_col) {
        if (!(x > 0.0)) throw std::invalid_argument("zero column marginal is not supported");
        x = 1.0 / x;
    }

    double weighted_sum = 0.0;

    for (std::size_t i = 0; i < rows; ++i) {
        const auto* CHI2_RESTRICT row = observed + i * cols;
        double local = 0.0;

        // This reduction is intentionally simple so -O3 can auto-vectorize it.
        // With GCC, -fopenmp-simd can additionally honor this pragma without
        // enabling OpenMP threading.
        #if defined(_OPENMP)
        #pragma omp simd reduction(+:local)
        #endif
        for (std::size_t j = 0; j < cols; ++j) {
            const double o = static_cast<double>(row[j]);
            local += o * o * ws.inv_col[j];
        }
        weighted_sum += local / ws.row_sum[i];
    }

    double statistic = total * weighted_sum - total;
    // Small negative values are possible from floating point cancellation.
    if (statistic < 0.0 && statistic > -1e-12 * std::max(1.0, total)) {
        statistic = 0.0;
    }

    const int dof = static_cast<int>((rows - 1) * (cols - 1));
    return {statistic, pvalue_from_statistic(statistic, dof), dof};
}

// Statistic-only version for hot ranking/filtering paths.
inline double chi2_dense_statistic_only(const std::uint32_t* CHI2_RESTRICT observed,
                                        std::size_t rows,
                                        std::size_t cols,
                                        DenseWorkspace& ws) {
    if (observed == nullptr || rows < 2 || cols < 2) {
        throw std::invalid_argument("invalid table");
    }

    ws.resize(rows, cols);
    std::fill(ws.row_sum.begin(), ws.row_sum.end(), 0.0);
    std::fill(ws.inv_col.begin(), ws.inv_col.end(), 0.0);

    double total = 0.0;
    for (std::size_t i = 0; i < rows; ++i) {
        const auto* CHI2_RESTRICT row = observed + i * cols;
        double rs = 0.0;
        for (std::size_t j = 0; j < cols; ++j) {
            const double x = static_cast<double>(row[j]);
            rs += x;
            ws.inv_col[j] += x;
        }
        if (!(rs > 0.0)) throw std::invalid_argument("zero row marginal");
        ws.row_sum[i] = rs;
        total += rs;
    }
    for (double& x : ws.inv_col) {
        if (!(x > 0.0)) throw std::invalid_argument("zero column marginal");
        x = 1.0 / x;
    }

    double weighted_sum = 0.0;
    for (std::size_t i = 0; i < rows; ++i) {
        const auto* CHI2_RESTRICT row = observed + i * cols;
        double local = 0.0;
        #if defined(_OPENMP)
        #pragma omp simd reduction(+:local)
        #endif
        for (std::size_t j = 0; j < cols; ++j) {
            const double o = static_cast<double>(row[j]);
            local += o * o * ws.inv_col[j];
        }
        weighted_sum += local / ws.row_sum[i];
    }

    const double statistic = total * weighted_sum - total;
    return statistic > 0.0 ? statistic : 0.0;
}

// Sparse O(nnz) variant. The caller supplies positive row/column marginals,
// and cells contains only nonzero observed counts.
inline Result chi2_sparse(const SparseCell* CHI2_RESTRICT cells,
                          std::size_t nnz,
                          const double* CHI2_RESTRICT row_sum,
                          const double* CHI2_RESTRICT col_sum,
                          std::size_t rows,
                          std::size_t cols) {
    if (cells == nullptr || row_sum == nullptr || col_sum == nullptr || rows < 2 || cols < 2) {
        throw std::invalid_argument("invalid sparse inputs");
    }

    std::vector<double> inv_row(rows);
    std::vector<double> inv_col(cols);
    double total = 0.0;

    for (std::size_t i = 0; i < rows; ++i) {
        if (!(row_sum[i] > 0.0)) throw std::invalid_argument("zero row marginal");
        inv_row[i] = 1.0 / row_sum[i];
        total += row_sum[i];
    }
    for (std::size_t j = 0; j < cols; ++j) {
        if (!(col_sum[j] > 0.0)) throw std::invalid_argument("zero column marginal");
        inv_col[j] = 1.0 / col_sum[j];
    }

    double score = 0.0;
    for (std::size_t k = 0; k < nnz; ++k) {
        const auto& cell = cells[k];
        if (cell.row >= rows || cell.col >= cols || cell.count == 0) {
            throw std::invalid_argument("invalid sparse cell");
        }
        const double o = static_cast<double>(cell.count);
        score += o * o * inv_row[cell.row] * inv_col[cell.col];
    }

    double statistic = total * score - total;
    if (statistic < 0.0 && statistic > -1e-12 * std::max(1.0, total)) statistic = 0.0;

    const int dof = static_cast<int>((rows - 1) * (cols - 1));
    return {statistic, pvalue_from_statistic(statistic, dof), dof};
}


inline double chi2_2xN_statistic_only(const std::uint32_t* CHI2_RESTRICT a,
                                      const std::uint32_t* CHI2_RESTRICT b,
                                      std::size_t cols) {
    if (a == nullptr || b == nullptr || cols < 2) throw std::invalid_argument("invalid 2xN table");
    double A = 0.0, B = 0.0;
    for (std::size_t j = 0; j < cols; ++j) {
        A += static_cast<double>(a[j]);
        B += static_cast<double>(b[j]);
    }
    if (!(A > 0.0 && B > 0.0)) throw std::invalid_argument("zero row marginal");
    double sum = 0.0;
    for (std::size_t j = 0; j < cols; ++j) {
        const double aj = static_cast<double>(a[j]);
        const double bj = static_cast<double>(b[j]);
        const double c = aj + bj;
        if (!(c > 0.0)) throw std::invalid_argument("zero column marginal");
        const double d = aj * B - bj * A;
        sum += d * d / c;
    }
    return sum / (A * B);
}

// Specialized 2 x N statistic. This is useful when one categorical variable
// is binary. It computes marginals in a first pass and then uses
// chi2 = sum_j (a_j*B - b_j*A)^2 / (A*B*(a_j+b_j)).
inline Result chi2_2xN(const std::uint32_t* CHI2_RESTRICT a,
                       const std::uint32_t* CHI2_RESTRICT b,
                       std::size_t cols) {
    if (a == nullptr || b == nullptr || cols < 2) {
        throw std::invalid_argument("invalid 2xN table");
    }

    double A = 0.0;
    double B = 0.0;
    for (std::size_t j = 0; j < cols; ++j) {
        A += static_cast<double>(a[j]);
        B += static_cast<double>(b[j]);
    }
    if (!(A > 0.0 && B > 0.0)) throw std::invalid_argument("zero row marginal");

    double sum = 0.0;
    for (std::size_t j = 0; j < cols; ++j) {
        const double aj = static_cast<double>(a[j]);
        const double bj = static_cast<double>(b[j]);
        const double c = aj + bj;
        if (!(c > 0.0)) throw std::invalid_argument("zero column marginal");
        const double d = aj * B - bj * A;
        sum += d * d / c;
    }

    const double statistic = sum / (A * B);
    const int dof = static_cast<int>(cols - 1);
    return {statistic, pvalue_from_statistic(statistic, dof), dof};
}

} // namespace chi2fast


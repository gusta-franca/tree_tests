// fd_checker.cpp
#include "fd_checker.h"
#include "prefix_tree_fd_checker.h"
#include "ankerl/unordered_dense.h"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <thread>
#include <vector>

namespace {

// Hash for vector<uint32_t> to use as a group key
struct VecHash {
    size_t operator()(const std::vector<uint32_t>& v) const noexcept {
        uint64_t h = 1469598103934665603ull; // FNV offset basis
        for (uint32_t x : v) {
            h ^= static_cast<uint64_t>(x);
            h *= 1099511628211ull; // FNV prime
        }
        return static_cast<size_t>(h);
    }
};

struct VecEq {
    bool operator()(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) const noexcept {
        return a == b;
    }
};

}

// Check a single FD using anchor column selection and row-wise grouping
static bool check_single_fd(
    const CSVIndex& index,
    const FDSpec& fd,
    std::string& violation_reason) {

    if (fd.lhs_columns.empty()) {
        violation_reason = "LHS is empty";
        return false;
    }

    // Resolve LHS columns
    std::vector<const ColumnIndex*> lhs;
    lhs.reserve(fd.lhs_columns.size());
    for (const auto& name : fd.lhs_columns) {
        auto* c = index.get_column(name);
        if (!c) {
            violation_reason = "Column not found: " + name;
            return false;
        }
        lhs.push_back(c);
    }

    // Resolve RHS column
    const ColumnIndex* rhs = index.get_column(fd.rhs_column);
    if (!rhs) {
        violation_reason = "RHS column not found: " + fd.rhs_column;
        return false;
    }

    // Choose anchor: LHS column with smallest distinct count
    size_t anchor_pos = 0;
    size_t min_distinct = lhs[0]->distinct_count();
    for (size_t i = 1; i < lhs.size(); ++i) {
        size_t d = lhs[i]->distinct_count();
        if (d < min_distinct) {
            min_distinct = d;
            anchor_pos = i;
        }
    }

    const ColumnIndex* anchor = lhs[anchor_pos];

    // Pre-compute column indices for row-wise value lookup
    std::vector<size_t> lhs_col_indices;
    lhs_col_indices.reserve(lhs.size());
    for (auto* c : lhs) {
        lhs_col_indices.push_back(c->index);
    }
    size_t rhs_index = rhs->index;

    // Get row-wise values
    const auto& values = index.values();
    if (values.empty()) {
        violation_reason = "Row-wise values not available";
        return false;
    }

    using GroupMap = ankerl::unordered_dense::map<std::vector<uint32_t>, uint32_t, VecHash, VecEq>;

    // For each distinct value in anchor column, group by full LHS and check RHS uniqueness
    for (const auto& [anchor_val, bm] : anchor->value_to_tuples) {
        GroupMap groups;
        groups.reserve(bm.cardinality());

        for (uint32_t tid : bm) {
            // Build LHS key for this tuple
            std::vector<uint32_t> key;
            key.reserve(lhs_col_indices.size());
            for (size_t col_idx : lhs_col_indices) {
                if (col_idx >= values.size() || tid >= values[col_idx].size()) {
                    std::ostringstream oss;
                    oss << "Malformed row at tuple " << tid << ", column " << col_idx;
                    violation_reason = oss.str();
                    return false;
                }
                key.push_back(values[col_idx][tid]);
            }

            uint32_t rhs_val = values[rhs_index][tid];

            auto it = groups.find(key);
            if (it == groups.end()) {
                groups.emplace(std::move(key), rhs_val);
            } else {
                if (it->second != rhs_val) {
                    std::ostringstream oss;
                    oss << "Violation: LHS maps to multiple RHS values ("
                        << it->second << " vs " << rhs_val << ")";
                    violation_reason = oss.str();
                    return false;
                }
            }
        }
    }

    return true;
}

std::vector<FDCheckResult> BasicFDChecker::check_fds(
    const CSVIndex& index,
    const std::vector<FDSpec>& fds,
    bool parallel) {

    // Pre-allocate results vector to avoid race conditions
    std::vector<FDCheckResult> results(fds.size());

    if (parallel && fds.size() > 1) {
        
        // TODO: Optimize thread pool usage for better performance


        // Parallel execution using worker threads
        // Each FD is checked independently in a separate thread
        const size_t num_threads = std::min(
            static_cast<size_t>(std::thread::hardware_concurrency()),
            fds.size()
        );
        
        auto worker = [&](size_t start, size_t end) {
            for (size_t i = start; i < end; ++i) {
                std::string reason;
                bool holds = check_single_fd(index, fds[i], reason);
                results[i] = FDCheckResult{fds[i], holds, holds ? std::string() : reason};
            }
        };

        std::vector<std::thread> threads;
        threads.reserve(num_threads);
        
        const size_t chunk_size = (fds.size() + num_threads - 1) / num_threads;
        
        for (size_t t = 0; t < num_threads; ++t) {
            size_t start = t * chunk_size;
            size_t end = std::min(start + chunk_size, fds.size());
            if (start < end) {
                threads.emplace_back(worker, start, end);
            }
        }

        // Wait for all threads to complete
        for (auto& thread : threads) {
            thread.join();
        }
    } else {
        // Sequential execution: simpler and more efficient for small workloads
        for (size_t i = 0; i < fds.size(); ++i) {
            std::string reason;
            bool holds = check_single_fd(index, fds[i], reason);
            results[i] = FDCheckResult{fds[i], holds, holds ? std::string() : reason};
        }
    }

    return results;
}

// Factory function implementation
std::unique_ptr<FDChecker> create_fd_checker(const std::string& impl_name) {
    if (impl_name == "basic") {
        return std::make_unique<BasicFDChecker>();
    } else if (impl_name == "prefix-tree") {
        return std::make_unique<PrefixTreeFDChecker>();
    }
    return nullptr;
}

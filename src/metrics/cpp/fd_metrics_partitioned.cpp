// fd_metrics_checker.cpp
#include <algorithm>
#include <iostream>
#include <vector>

#include "ankerl/unordered_dense.h"
#include "fd_metrics_partitioned.h"

namespace {
    struct VecHash {
        size_t operator()(const std::vector<uint32_t> &v) const noexcept {
            uint64_t h = 1469598103934665603ull; // FNV offset basis
            for (uint32_t x : v) {
                h ^= static_cast<uint64_t>(x);
                h *= 1099511628211ull; // FNV prime
            }
            return static_cast<size_t>(h);
        }
    };

    struct VecEq {
        bool operator()(const std::vector<uint32_t> &a, const std::vector<uint32_t> &b) const noexcept {
            return a == b;
        }
    };
}

// PS: CSVIndex is responsible for creating the inverted index (ColumnIndex)
PdepResult compute_pdep(const CSVIndex& index, const FDSpec& fd) {

    std::chrono::duration<double> total_build_time(0);
    std::chrono::duration<double> total_compute_time(0);
    size_t peak_memory_b = 0;
    
    PdepResult result = {0.0, 0.0, 0.0, 0.0};

    auto resolve_start = std::chrono::steady_clock::now();
    
    // Resolve LHS columns
	std::vector<uint32_t> lhs_indices;
	lhs_indices.reserve(fd.lhs_columns.size());
	for (const auto &name : fd.lhs_columns) {
		auto *c = index.get_column(name);
		if (!c) {
			return result;
		}
		lhs_indices.push_back(c->index);
	}

    if (lhs_indices.empty()) {
        return result;
    }

	// Resolve RHS column
	const ColumnIndex *rhs = index.get_column(fd.rhs_column);
	if (!rhs) {
		return result;
    }

    uint32_t rhs_index = rhs->index; 

    // Choose anchor as the attribute with the biggest distinct count.
    // This way there will be a high number of "chunks", one for each unique value in the anchor.
    // For each chunk, a temporary hashtable will be built. If the distinct count is high, the average size of the hashtable will be small.
    size_t anchor_pos = 0;
    size_t max_distinct = 0;
    
    for (size_t i = 0; i < lhs_indices.size(); ++i) {
        const ColumnIndex* c = index.get_column(lhs_indices[i]); 
        size_t d = c->distinct_count();
        if (d > max_distinct) {
            max_distinct = d;
            anchor_pos = i;
        }
    }

    const ColumnIndex* anchor = index.get_column(lhs_indices[anchor_pos]);

    const auto& values = index.values();
    if (values.empty()) {
        return result;
    }
    
    size_t total_rows = values[rhs_index].size();
    if (total_rows == 0) return result;

    using GroupMap = ankerl::unordered_dense::map<std::vector<uint32_t>, uint32_t, VecHash, VecEq>;

    double global_sum = 0.0;
    size_t dom_x_size = 0;
    
    std::vector<uint32_t> x_key;
    std::vector<uint32_t> xy_key;

    // Reserving to avoid reallocations 
    x_key.reserve(lhs_indices.size());
    xy_key.reserve(lhs_indices.size() + 1);

    auto resolve_end = std::chrono::steady_clock::now();
    total_build_time += resolve_end - resolve_start;

    // Multiple tiny hashtables for each unique value in the anchor attribute
    for (const auto& [anchor_val, bm] : anchor->value_to_tuples) {
        auto build_start = std::chrono::steady_clock::now();
       
        GroupMap x_counts;
        GroupMap xy_counts;
        
        // REserving to avoid rehashing
        x_counts.reserve(bm.cardinality());
        xy_counts.reserve(bm.cardinality());

        for (uint32_t tid : bm) {
            x_key.clear();
            
            for (size_t col_idx : lhs_indices) {
                x_key.push_back(values[col_idx][tid]);
            }
            
            xy_key = x_key; 
            uint32_t rhs_val = values[rhs_index][tid];
            xy_key.push_back(rhs_val);

            x_counts[x_key]++;
            xy_counts[xy_key]++;
        }

        auto build_end = std::chrono::steady_clock::now();
        total_build_time += (build_end - build_start);

        size_t x_bytes = x_counts.size() * sizeof(GroupMap::value_type);
        size_t xy_bytes = xy_counts.size() * sizeof(GroupMap::value_type);        
        size_t control_bytes = (x_counts.bucket_count() + xy_counts.bucket_count()) * 8; 
        
        size_t current_memory = x_bytes + xy_bytes + control_bytes;
        if (current_memory > peak_memory_b) {
            peak_memory_b = current_memory;
        }

        auto compute_start = std::chrono::steady_clock::now();

        dom_x_size += x_counts.size();

        // Compute xy_count^2 / x_count for this iteration and sum to global_sum
        for (const auto& [xy_k, xy_c] : xy_counts) {
            std::vector<uint32_t> x_k(xy_k.begin(), xy_k.end() - 1);

            uint32_t x_c = x_counts[x_k];

            double term = (xy_c*xy_c) / static_cast<double>(x_c);
            global_sum += term;
        }

        auto compute_end = std::chrono::steady_clock::now();
        total_compute_time += (compute_end - compute_start);        
    }

    auto compute_start = std::chrono::steady_clock::now();

    double pdep_XY = global_sum / static_cast<double>(total_rows);
    double pdep_Y = 0.0;
    
    // Compute pdep Y
    for (const auto& [value, bm] : rhs->value_to_tuples) {
        double count = static_cast<double>(bm.cardinality());
        pdep_Y += (count * count);
    }

    pdep_Y /= static_cast<double>(total_rows * total_rows);

    // If it's a key
    if (total_rows == dom_x_size) {
        result.metric_value = 1.0;
    }
    else {
        double numerator = 1.0 - pdep_XY;
        double denominator = 1.0 - pdep_Y;
        double factor = static_cast<double>(total_rows - 1) / 
                       static_cast<double>(total_rows - dom_x_size);
        
        double mu = 1.0 - (numerator / denominator) * factor;

        result.metric_value = std::max(0.0, mu);
    }

    auto compute_end = std::chrono::steady_clock::now();
    total_compute_time += (compute_end - compute_start);        

    result.build_time_s = total_build_time.count();
    result.compute_time_s = total_compute_time.count();
    result.memory_used_mb = peak_memory_b / (1024.0 * 1024.0);

    return result;
}

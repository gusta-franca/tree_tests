// prefix_tree_fd_checker.cpp
#include "prefix_tree_fd_checker.h"
#include <iostream>
#include <sstream>

// Build prefix tree: organize FDs by shared LHS column prefixes
std::unique_ptr<PrefixTreeFDChecker::TreeNode> PrefixTreeFDChecker::build_tree(
    const CSVIndex& index,
    const std::vector<FDSpec>& fds) {
    
    auto root = std::make_unique<TreeNode>();
    
    for (size_t fd_idx = 0; fd_idx < fds.size(); ++fd_idx) {
        const auto& fd = fds[fd_idx];
        TreeNode* current = root.get();
        
        // Build path through LHS columns
        for (const std::string& col_name : fd.lhs_columns) {
            const ColumnIndex* col = index.get_column(col_name);
            if (!col) {
                std::cerr << "Warning: Column " << col_name << " not found for FD " << fd_idx << std::endl;
                break;
            }
            
            size_t col_idx = col->index;
            auto it = current->children.find(col_idx);
            if (it == current->children.end()) {
                it = current->children.emplace(col_idx, std::make_unique<TreeNode>(col_idx)).first;
            }
            current = it->second.get();
        }
        
        // Register FD at this leaf (end of LHS path)
        current->fd_indices.push_back(fd_idx);
    }
    
    return root;
}

// Partition tuples by their value in given column
// Returns: value -> roaring bitmap of tuple IDs with that value
std::map<uint32_t, roaring::Roaring> PrefixTreeFDChecker::partition_by_column(
    const roaring::Roaring& tuples,
    size_t column_index,
    const CSVIndex& index) const {
    
    const auto& col_values = index.values()[column_index];
    std::map<uint32_t, roaring::Roaring> partitions;
    
    for (uint32_t tid : tuples) {
        uint32_t value = col_values[tid];
        partitions[value].add(tid);
    }
    
    return partitions;
}

// Recursive refinement: filter tuples down tree paths, check FDs at leaves
void PrefixTreeFDChecker::refine_and_check(
    TreeNode* node,
    const roaring::Roaring& tuples,
    const CSVIndex& index,
    const std::vector<FDSpec>& fds,
    std::vector<FDCheckResult>& results) {
    
    // Check FDs that terminate at this node
    for (size_t fd_idx : node->fd_indices) {
        const FDSpec& fd = fds[fd_idx];
        
        // Resolve RHS column
        const ColumnIndex* rhs_col = index.get_column(fd.rhs_column);
        if (!rhs_col) {
            results[fd_idx] = FDCheckResult{fd, false, "RHS column not found"};
            continue;
        }
        
        const auto& rhs_values = index.values()[rhs_col->index];
        bool holds = true;
        std::ostringstream reason;
        
        // For each tuple in current set, check if all have same RHS value
        // (They should, because we've already partitioned by all LHS cols)
        if (!tuples.isEmpty()) {
            auto it = tuples.begin();
            uint32_t first_tid = *it;
            uint32_t expected_rhs = rhs_values[first_tid];
            
            ++it;
            for (; it != tuples.end(); ++it) {
                uint32_t tid = *it;
                uint32_t rhs_val = rhs_values[tid];
                if (rhs_val != expected_rhs) {
                    reason << "Tuples " << first_tid << " and " << tid 
                           << " have same LHS but different RHS (" 
                           << expected_rhs << " vs " << rhs_val << ")";
                    holds = false;
                    break;
                }
            }
        }
        
        results[fd_idx] = FDCheckResult{fd, holds, reason.str()};
    }
    
    // Recurse into children: partition by next column, then refine each partition
    for (const auto& [col_idx, child] : node->children) {
        auto partitions = partition_by_column(tuples, col_idx, index);
        
        // For each value partition, recurse
        for (const auto& [value, partition_tuples] : partitions) {
            if (!partition_tuples.isEmpty()) {
                refine_and_check(child.get(), partition_tuples, index, fds, results);
            }
        }
    }
}

std::vector<FDCheckResult> PrefixTreeFDChecker::check_fds(
    const CSVIndex& index,
    const std::vector<FDSpec>& fds,
    bool parallel) {
    
    (void)parallel; // TODO: parallelize at tree depth 1
    
    std::vector<FDCheckResult> results(fds.size());
    if (fds.empty()) return results;
    
    // Build tree
    auto root = build_tree(index, fds);
    
    // Start refinement with all tuples
    roaring::Roaring all_tuples;
    all_tuples.addRange(0, index.tuple_count());
    
    refine_and_check(root.get(), all_tuples, index, fds, results);
    
    return results;
}

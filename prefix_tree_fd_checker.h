// prefix_tree_fd_checker.h
#ifndef PREFIX_TREE_FD_CHECKER_H
#define PREFIX_TREE_FD_CHECKER_H

#include "fd_checker.h"
#include "roaring.hh"
#include <map>
#include <memory>

// Prefix tree FD checker using refinement strategy (inspired by MissDC)
// Strategy: Start with all tuples, progressively filter by LHS column equality
// At leaf nodes, check if remaining tuples agree on RHS value
class PrefixTreeFDChecker : public FDChecker {
public:
    std::vector<FDCheckResult> check_fds(
        const CSVIndex& index,
        const std::vector<FDSpec>& fds,
        bool parallel = false) override;

private:
    // Tree node representing one column in the LHS determinant
    struct TreeNode {
        size_t column_index;  // Index into CSVIndex columns
        std::map<size_t, std::unique_ptr<TreeNode>> children; // col_idx -> child
        std::vector<size_t> fd_indices; // FDs that terminate at this node (check RHS here)
        
        TreeNode() : column_index(SIZE_MAX) {}
        explicit TreeNode(size_t col_idx) : column_index(col_idx) {}
        
        bool is_leaf() const { return children.empty(); }
    };

    // Build prefix tree from FD list (group by shared LHS prefixes)
    std::unique_ptr<TreeNode> build_tree(const CSVIndex& index, const std::vector<FDSpec>& fds);
    
    // Refine tuples by equality on given column, partitioning into value groups
    // Returns map: column_value -> roaring bitmap of tuple IDs
    std::map<uint32_t, roaring::Roaring> partition_by_column(
        const roaring::Roaring& tuples,
        size_t column_index,
        const CSVIndex& index) const;
    
    // Recursive refinement: filter tuples down the tree, check FDs at leaves
    void refine_and_check(
        TreeNode* node,
        const roaring::Roaring& tuples,
        const CSVIndex& index,
        const std::vector<FDSpec>& fds,
        std::vector<FDCheckResult>& results);
};

#endif // PREFIX_TREE_FD_CHECKER_H

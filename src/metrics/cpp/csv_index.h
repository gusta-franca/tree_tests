// csv_index.h
#ifndef CSV_INDEX_H
#define CSV_INDEX_H

#include <string>
#include <vector>
#include <cstdint>
#include "ankerl/unordered_dense.h"
#include "roaring.hh"

// Simple columnar data structure - no indexes!
struct ColumnarData {
    std::vector<std::string> column_names;
    std::vector<std::vector<uint32_t>> columns;  // column-major data
    size_t num_rows;
    
    // Statistics computed on-demand
    mutable std::vector<size_t> distinct_counts;  // cached distinct counts
    
    ColumnarData() : num_rows(0) {}
    
    size_t get_column_index(const std::string& name) const;
    size_t get_distinct_count(size_t col_idx) const;
};

// Load CSV directly into columnar format (no indexing)
bool load_csv_columnar(const std::string& filename, ColumnarData& data, bool verbose = false);


// Represents an indexed column with value -> roaring bitmap mapping
struct ColumnIndex {
    // --- Data Members ---
    size_t index;
    std::string name;
    ankerl::unordered_dense::map<uint32_t, roaring::Roaring> value_to_tuples;
    // Co-occurrence tracking: this_value -> (other_column_index -> set of co-occurring values)
    ankerl::unordered_dense::map<uint32_t, ankerl::unordered_dense::map<size_t, ankerl::unordered_dense::set<uint32_t>>> co_occurrences;

    // --- Constructors ---
    ColumnIndex(size_t col_index, const std::string& col_name);

    // --- Indexing Methods ---
    void add(uint32_t value, uint32_t tuple_id);
    const roaring::Roaring* get_tuples(uint32_t value) const;
    size_t distinct_count() const;

    // --- Co-occurrence ---
    bool has_cooccurrence_tracking() const;

    // --- Printing & Debug (always at bottom) ---
    void print() const;
    void print_cooccurrences() const;
};

// Main structure holding all column indexes
class CSVIndex {
public:
    // --- Constructors ---
    CSVIndex();

    // --- Index Building ---
    bool load_from_csv(const std::string& filename);
    bool load_from_csv(const std::string& filename, bool parallel_build);

    // --- Column Access ---
    const ColumnIndex* get_column(size_t col_idx) const { return (col_idx < column_indexes.size()) ? &column_indexes[col_idx] : nullptr; }
    const ColumnIndex* get_column(const std::string& name) const { for (const auto& col : column_indexes) { if (col.name == name) return &col; } return nullptr; }
    size_t num_columns() const { return column_indexes.size(); }
    size_t tuple_count() const { return num_tuples; }

    // --- Co-occurrence ---
    void build_cooccurrence(size_t col_idx, const std::vector<size_t>& tracking_columns);
    void print_cooccurrence(size_t col_idx) const;

    // --- Memory & Internals ---
    size_t get_memory_usage() const;
    const std::vector<std::vector<uint32_t>>& values() const;
    // --- Timing Accessors ---
    double last_row_build_ms() const { return row_build_ms; }
    double last_index_build_ms() const { return index_build_ms; }

    // --- Printing & Debug (always at bottom) ---
    void print_stats() const;
    void print_column(size_t col_idx) const;
    void print_column(const std::string& col_name) const;


private:
    std::vector<ColumnIndex> column_indexes;
    size_t num_tuples;
    //rowi-wise representation of table
    std::vector<std::vector<uint32_t>> column_values;
    // Timing of phases (row ingestion, index build)
    double row_build_ms = 0.0;
    double index_build_ms = 0.0;
};

#endif

// csv_index.cpp

#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <future>
#include <thread>
#include <sys/resource.h>
#define XXH_INLINE_ALL 1
#include "csv.h"
#include "csv_index.h"
#include "fd_input.h"
#include "hyperloglog.hpp"
#include "xxhash.h"

// -- ColumnarData Implementationm ---

size_t ColumnarData::get_column_index(const std::string& name) const {
    for (size_t i = 0; i < column_names.size(); ++i) {
        if (column_names[i] == name) return i;
    }
    return SIZE_MAX;
}

size_t ColumnarData::get_distinct_count(size_t col_idx) const {
    if (col_idx >= columns.size()) return 0;
    
    // Use cached value if available
    if (col_idx < distinct_counts.size() && distinct_counts[col_idx] > 0) {
        return distinct_counts[col_idx];
    }
    
    // Compute on-demand using a set
    ankerl::unordered_dense::set<uint32_t> unique_vals;
    for (uint32_t val : columns[col_idx]) {
        unique_vals.insert(val);
    }
    
    // Cache the result
    if (distinct_counts.size() <= col_idx) {
        distinct_counts.resize(col_idx + 1, 0);
    }
    distinct_counts[col_idx] = unique_vals.size();
    
    return unique_vals.size();
}

// Load CSV without building indexes
bool load_csv_columnar(const std::string& filename, ColumnarData& data, bool verbose) {
    using clock = std::chrono::steady_clock;
    auto t_start = clock::now();
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file: " << filename << std::endl;
        return false;
    }
    
    // Parse header
    std::string header_line;
    if (!std::getline(file, header_line)) {
        std::cerr << "Error: Empty file" << std::endl;
        return false;
    }
    
    std::stringstream ss(header_line);
    std::string col_name;
    while (std::getline(ss, col_name, ',')) {
        // Trim whitespace
        size_t start = col_name.find_first_not_of(" \t\r\n");
        size_t end = col_name.find_last_not_of(" \t\r\n");
        if (start != std::string::npos) {
            col_name = col_name.substr(start, end - start + 1);
        }
        data.column_names.push_back(col_name);
    }
    
    data.columns.resize(data.column_names.size());
    
    if (verbose) {
        std::cout << "Columns: ";
        for (size_t i = 0; i < data.column_names.size(); ++i) {
            std::cout << data.column_names[i];
            if (i + 1 < data.column_names.size()) std::cout << ", ";
        }
        std::cout << std::endl;
    }
    
    // Parse data rows
    std::string line;
    size_t row_count = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        std::stringstream row_ss(line);
        std::string cell;
        size_t col_idx = 0;
        
        while (std::getline(row_ss, cell, ',') && col_idx < data.columns.size()) {
            uint32_t value = 0;
            try {
                value = std::stoul(cell);
            } catch (...) {
                value = 0;
            }
            data.columns[col_idx].push_back(value);
            ++col_idx;
        }
        
        // Pad short rows
        while (col_idx < data.columns.size()) {
            data.columns[col_idx].push_back(0);
            ++col_idx;
        }
        
        ++row_count;
    }
    
    data.num_rows = row_count;
    
    auto t_end = clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    
    if (verbose) {
        std::cout << "Loaded " << row_count << " rows in " << elapsed_ms << " ms" << std::endl;
        std::cout << "No indexes built - pure columnar storage" << std::endl;
    }
    
    return true;
}

bool load_csv_columnar(const std::string& filename, ColumnarData& data, const FDSpec& fd, size_t& est_xy_card, bool verbose) {
    // std::chrono::duration<double> hll_build_time(0);
    // std::chrono::duration<double> hll_est_time(0);    
    
    using clock = std::chrono::steady_clock;
    auto t_start = clock::now();
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Cannot open file: " << filename << std::endl;
        return false;
    }
    
    // Parse header
    std::string header_line;
    if (!std::getline(file, header_line)) {
        std::cerr << "Error: Empty file" << std::endl;
        return false;
    }
    
    std::stringstream ss(header_line);
    std::string col_name;
    while (std::getline(ss, col_name, ',')) {
        // Trim whitespace
        size_t start = col_name.find_first_not_of(" \t\r\n");
        size_t end = col_name.find_last_not_of(" \t\r\n");
        if (start != std::string::npos) {
            col_name = col_name.substr(start, end - start + 1);
        }
        data.column_names.push_back(col_name);
    }

    size_t num_cols = data.column_names.size();
    data.columns.resize(num_cols);
    
    // Resolve LHS/RHS
    std::vector<size_t> lhs_indices;
    for (const auto& name : fd.lhs_columns) {
        lhs_indices.push_back(data.get_column_index(name));
    }
    size_t rhs_idx = data.get_column_index(fd.rhs_column);
    
    if (verbose) {
        std::cout << "Columns: ";
        for (size_t i = 0; i < num_cols; ++i) {
            std::cout << data.column_names[i];
            if (i + 1 < num_cols) std::cout << ", ";
        }
        std::cout << std::endl;
    }
    
    // auto hll_build_start = clock::now();
    // fd_row = every attribute in the FD; current_row = every attribute in a row
    // Right now, fd_row == current_row at all times 
    size_t lhs_size = lhs_indices.size();
    std::vector<uint32_t> fd_row(lhs_size + 1);
    std::vector<uint32_t> current_row(num_cols);    
    std::stringstream row_ss;
    std::string cell;

    hll::HyperLogLog hll_xy(14);                                                // HLL used for estimating XY cardinality
    std::vector<hll::HyperLogLog> hll_col(num_cols, hll::HyperLogLog(14));     // Vector with every attribute's HLL
    std::chrono::duration<double> hll_xy_time(0);
    std::chrono::duration<double> hll_col_time(0);
    
    // Parse data rows
    std::string line;
    size_t row_count = 0;
    while (std::getline(file, line)) {
        if (line.empty()) continue;

        row_ss.clear();
        row_ss.str(line);
         
        size_t col_idx = 0;
        
        // operate direclty in the char buffer inside getline
        // if found the separator (in this case, a comma) and the separator points to a bigger address than buf, stores buf's content into value (std::from_chars())
        // write the value on data.columns and current_row in the current col_idx
        // make buf point to the next address after separator

        size_t col_idx = 0;
        const char* buf = line.data();
        const char* len = buf + line.size();
        
        while (buf < len && col_idx < num_cols) {
            const char* separator = buf;
            while (separator < len && *separator != ',') {
                ++separator;
            }
            
            uint32_t value = 0;
            if (separator > buf) {
                std::from_chars(buf, separator, value);
            }
            
            data.columns[col_idx].push_back(value);
            current_row[col_idx] = value;
            ++col_idx;
            
            buf = separator + 1;
        }
        
        // Pad short rows
        while (col_idx < num_cols) {
            data.columns[col_idx].push_back(0);
            current_row[col_idx] = 0;

            // Measure time taken to build individual sketches
            auto col_hll_start = clock::now();
            uint64_t hash = XXH3_64bits(&current_row[col_idx], sizeof(uint32_t));
            hll_col[col_idx].add(hash);
            hll_col_time += (clock::now() - col_hll_start);

            ++col_idx;
        }

        // Hash and add to HLL
        auto col_hll_start = clock::now();
        for (size_t i = 0; i < lhs_size; i++) {
            fd_row[i] = current_row[lhs_indices[i]];
        }
        fd_row[lhs_size] = current_row[rhs_idx];
        
        uint64_t hash = XXH3_64bits(fd_row.data(), fd_row.size()*sizeof(uint32_t));
        hll_xy.add(hash);
        hll_xy_time += (clock::now() - col_hll_start);
        
        ++row_count;
    }
    
    // auto hll_build_end = clock::now();
    // hll_build_time += (hll_build_end - hll_build_start);
    
    data.num_rows = row_count;

    // auto hll_est_start = clock::now();
    est_xy_card = hll_xy.estimate();
    // auto hll_est_end = clock::now();
    // hll_est_time += (hll_est_end - hll_est_start);

    auto t_end = clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
    
    if (verbose) {
        std::cout << "Loaded " << row_count << " rows in " << elapsed_ms << " ms" << std::endl;
        std::cout << "No indexes built - pure columnar storage" << std::endl;
    }

    std::cout << "HLL_JSON: {"
              << "\"hll_col_time_s\": " << hll_col_time.count() << ", "
              << "\"hll_xy_time_s\": " << hll_xy_time.count()
              << "}" << std::endl;
    
    return true;
}


// --- ColumnIndex Implementation ---

ColumnIndex::ColumnIndex(size_t col_index, const std::string& col_name)
    : index(col_index), name(col_name) {}

void ColumnIndex::add(uint32_t value, uint32_t tuple_id) {
    this->value_to_tuples[value].add(tuple_id);
}

const roaring::Roaring* ColumnIndex::get_tuples(uint32_t value) const {
    auto it = this->value_to_tuples.find(value);
    return (it != this->value_to_tuples.end()) ? &it->second : nullptr;
}

size_t ColumnIndex::distinct_count() const { return this->value_to_tuples.size(); }
bool ColumnIndex::has_cooccurrence_tracking() const { return !this->co_occurrences.empty(); }

// --- CSVIndex Implementation ---

CSVIndex::CSVIndex() : num_tuples(0) {}

// --- Index Building ---
bool CSVIndex::load_from_csv(const std::string& filename) {
    return load_from_csv(filename, false);
}

bool CSVIndex::load_from_csv(const std::string& filename, bool parallel_build) {
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();

    // Reset previous state
    column_indexes.clear();
    column_values.clear();
    num_tuples = 0;

    io::LineReader lr(filename.c_str());
    char* header = lr.next_line();
    if (!header) {
        std::cerr << "Error: Empty file" << std::endl;
        return false;
    }

    // Parse header
    {
        size_t col_idx = 0;
        char* p = header;
        while (*p) {
            char* start = p;
            while (*p && *p != ',') ++p;
            std::string name(start, p - start);
            size_t a = name.find_first_not_of(" \t\r\n");
            size_t b = name.find_last_not_of(" \t\r\n");
            if (a == std::string::npos) name.clear(); else name = name.substr(a, b - a + 1);
            if (name.empty()) name = "col" + std::to_string(col_idx + 1);
            column_indexes.emplace_back(col_idx++, name);
            if (*p == ',') ++p; else break;
        }
    }

    if (column_indexes.empty()) {
        std::cerr << "Error: No columns found in header" << std::endl;
        return false;
    }

    std::cout << "Found " << column_indexes.size() << " columns: ";
    for (size_t i = 0; i < column_indexes.size(); ++i) {
        std::cout << column_indexes[i].name << (i + 1 < column_indexes.size() ? ", " : "\n");
    }

    column_values.resize(column_indexes.size());
    const size_t num_cols = column_indexes.size();

    // Row ingestion phase (only build row vectors)
    char* line = nullptr;
    uint32_t row_id = 0;
    while ((line = lr.next_line())) {
        if (*line == '\0') continue; // skip empty
        char* p = line;
        size_t col = 0;
        while (col < num_cols) {
            char* start = p;
            while (*p && *p != ',') ++p;
            uint32_t value = 0;
            for (char* d = start; d < p; ++d) {
                if (*d >= '0' && *d <= '9') value = value * 10 + static_cast<uint32_t>(*d - '0');
                else if (*d == ' ' || *d == '\t') continue; // ignore whitespace
                else { value = 0; break; }
            }
            column_values[col].push_back(value);
            ++col;
            if (*p == ',') { ++p; } else break;
        }
        while (col < num_cols) { // pad short lines
            column_values[col].push_back(0);
            ++col;
        }
        ++row_id;
    }
    num_tuples = row_id;

    auto t1 = clock::now();
    row_build_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    // Index build phase (always after ingestion)
    auto build_one = [this](size_t cidx) {
        ColumnIndex& col = column_indexes[cidx];
        col.value_to_tuples.clear();
        const auto& vals = column_values[cidx];
        for (uint32_t tid = 0; tid < vals.size(); ++tid) {
            col.value_to_tuples[vals[tid]].add(tid);
        }
    };

    if (parallel_build) {
        std::vector<std::future<void>> tasks;
        tasks.reserve(column_indexes.size());
        for (size_t i = 0; i < column_indexes.size(); ++i) {
            tasks.emplace_back(std::async(std::launch::async, build_one, i));
        }
        for (auto& f : tasks) f.get();
    } else {
        for (size_t i = 0; i < column_indexes.size(); ++i) build_one(i);
    }

    auto t2 = clock::now();
    index_build_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();

    std::cout << "Loaded " << num_tuples << " tuples" << std::endl;
    std::cout << "Row phase: " << row_build_ms << " ms, Index phase: " << index_build_ms << " ms" << std::endl;
    return true;
}

// --- Memory & Internals ---
size_t CSVIndex::get_memory_usage() const {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    // ru_maxrss is in kilobytes on Linux, bytes on macOS
#ifdef __APPLE__
    return usage.ru_maxrss;  // bytes on macOS
#else
    return usage.ru_maxrss * 1024;  // convert KB to bytes on Linux
#endif
}

// --- Co-occurrence ---
void CSVIndex::build_cooccurrence(size_t col_idx, const std::vector<size_t>& tracking_columns) {
    if (col_idx >= column_indexes.size()) {
        std::cerr << "Error: Column index " << col_idx << " out of range" << std::endl;
        return;
    }
    
    ColumnIndex& col = column_indexes[col_idx];
    
    // Clear any existing co-occurrence data
    col.co_occurrences.clear();
    
    std::cout << "Building co-occurrence for Column " << col.index << " (" << col.name 
              << ") with columns: ";
    for (size_t i = 0; i < tracking_columns.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << tracking_columns[i];
    }
    std::cout << std::endl;
    
    // Fast path using row-wise values to avoid bitmap cross-products
    for (const auto& [value, bitmap] : col.value_to_tuples) {
        auto& value_cooccur = col.co_occurrences[value];
        for (uint32_t tuple_id : bitmap) {
            for (size_t other_col_idx : tracking_columns) {
                if (other_col_idx >= column_indexes.size() || other_col_idx == col_idx) continue;
                // Guard against malformed rows
                if (other_col_idx >= column_values.size() || tuple_id >= column_values[other_col_idx].size()) continue;
                uint32_t other_val = column_values[other_col_idx][tuple_id];
                value_cooccur[other_col_idx].insert(other_val);
            }
        }
    }
    
    std::cout << "Co-occurrence tracking built successfully" << std::endl;
}

// --- Printing & Debug (bottom) ---
void CSVIndex::print_stats() const {
    std::cout << "\n=== CSV Index Statistics ===" << std::endl;
    std::cout << "Total tuples: " << num_tuples << std::endl;
    std::cout << "Total columns: " << column_indexes.size() << std::endl;
    std::cout << "\nPer-column statistics:" << std::endl;
    for (size_t i = 0; i < column_indexes.size(); ++i) {
        const auto& col = column_indexes[i];
        std::cout << "  Column " << col.index << " (" << col.name << "):" << std::endl;
        std::cout << "    Distinct values: " << col.distinct_count() << std::endl;
        size_t total_cardinality = 0;
        for (const auto& [value, bitmap] : col.value_to_tuples) total_cardinality += bitmap.cardinality();
        std::cout << "    Total bitmap cardinality: " << total_cardinality << std::endl;
        if (!col.value_to_tuples.empty()) {
            std::cout << "    Sample values: ";
            int count = 0;
            for (const auto& [value, bitmap] : col.value_to_tuples) {
                if (count++ >= 5) break;
                std::cout << value << " (" << bitmap.cardinality() << " tuples)";
                if (count < 5 && static_cast<std::size_t>(count) < col.value_to_tuples.size()) std::cout << ", ";
            }
            std::cout << std::endl;
        }
    }
}

void CSVIndex::print_column(size_t col_idx) const {
    const ColumnIndex* col = get_column(col_idx);
    if (col) col->print();
    else std::cerr << "Error: Column index " << col_idx << " out of range" << std::endl;
}

void CSVIndex::print_column(const std::string& col_name) const {
    const ColumnIndex* col = get_column(col_name);
    if (col) col->print();
    else std::cerr << "Error: Column '" << col_name << "' not found" << std::endl;
}

void CSVIndex::print_cooccurrence(size_t col_idx) const {
    const ColumnIndex* col = get_column(col_idx);
    if (col) col->print_cooccurrences();
    else std::cerr << "Error: Column index " << col_idx << " out of range" << std::endl;
}

void ColumnIndex::print() const {
    std::cout << "\n=== Column " << index << " (" << name << ") ===" << std::endl;
    std::cout << "Distinct values: " << distinct_count() << std::endl;
    std::cout << "\nValue -> Tuple IDs:" << std::endl;
    for (const auto& [value, bitmap] : this->value_to_tuples) {
        std::cout << "  " << value << " -> [";
        bool first = true;
        for (uint32_t tuple_id : bitmap) {
            if (!first) std::cout << ", ";
            std::cout << tuple_id;
            first = false;
        }
        std::cout << "]" << std::endl;
    }
}

void ColumnIndex::print_cooccurrences() const {
    if (this->co_occurrences.empty()) { std::cout << "No co-occurrence tracking for this column" << std::endl; return; }
    std::cout << "\n=== Co-occurrence for Column " << index << " (" << name << ") ===" << std::endl;
    for (const auto& [value, col_map] : co_occurrences) {
        std::cout << "Value " << value << ":" << std::endl;
        for (const auto& [other_col_idx, value_set] : col_map) {
            std::cout << "  With Column " << other_col_idx << ": {";
            bool first = true;
            for (uint32_t co_val : value_set) { if (!first) std::cout << ", "; std::cout << co_val; first = false; }
            std::cout << "}" << std::endl;
        }
    }
}

// --- Sorting & Output ---

const std::vector<std::vector<uint32_t>>& CSVIndex::values() const { return column_values; }


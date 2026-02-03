// fd_input.h
// Simple, focused code for loading CSV index and parsing FD specifications

#ifndef FD_INPUT_H
#define FD_INPUT_H

#include <string>
#include <vector>
#include "csv_index.h"

// Simple structure to hold a functional dependency
struct FDSpec {
    std::vector<std::string> lhs_columns;  // Left-hand side column names
    std::string rhs_column;                // Right-hand side column name
    
    // For debugging
    void print() const;
};

// Load CSV and build index
bool load_csv_index(const std::string& csv_file, CSVIndex& index, bool parallel = false);

// Parse .fds file into array of FD specifications
bool load_fds_file(const std::string& fds_file, std::vector<FDSpec>& fds);

#endif

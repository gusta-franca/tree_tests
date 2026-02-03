// fd_input.cpp
// Implementation of CSV and FDS file loading

#include "fd_input.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>
#include <chrono>

// --- FDSpec methods ---

void FDSpec::print() const {
    std::cout << "  FD: [";
    for (size_t i = 0; i < lhs_columns.size(); ++i) {
        std::cout << "\"" << lhs_columns[i] << "\"";
        if (i < lhs_columns.size() - 1) std::cout << ", ";
    }
    std::cout << "] -> \"" << rhs_column << "\"" << std::endl;
}

// --- CSV Index Loading ---

bool load_csv_index(const std::string& csv_file, CSVIndex& index, bool parallel) {
    std::cout << "\n=== Loading CSV Index ===" << std::endl;
    std::cout << "CSV file: " << csv_file << std::endl;
    std::cout << "Build mode: " << (parallel ? "parallel" : "sequential") << std::endl;
    
    if (!index.load_from_csv(csv_file, parallel)) {
        std::cerr << "ERROR: Failed to load CSV file: " << csv_file << std::endl;
        return false;
    }
    
    std::cout << "✓ Index built successfully" << std::endl;
    std::cout << "  Columns: " << index.num_columns() << std::endl;
    std::cout << "  Tuples: " << index.tuple_count() << std::endl;
    
    // Print column info
    std::cout << "\nColumn Information:" << std::endl;
    for (size_t i = 0; i < index.num_columns(); ++i) {
        const auto* col = index.get_column(i);
        if (col) {
            std::cout << "  [" << i << "] " << col->name 
                      << " - " << col->distinct_count() << " distinct values" << std::endl;
        }
    }
    
    return true;
}

// --- FDS File Parsing ---

namespace {
    // Trim whitespace
    inline void trim(std::string& s) {
        auto notspace = [](int ch) { return !std::isspace(ch); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), notspace));
        s.erase(std::find_if(s.rbegin(), s.rend(), notspace).base(), s.end());
    }

    // Parse a single FD line like: (["col1", "col2"], "col3")
    bool parse_fd_line(const std::string& line, FDSpec& fd) {
        // Regex to match: (["col1","col2",...], "rhs")
        static const std::regex outer_re(R"FD(^\(\s*\[(.*)\]\s*,\s*"([^"]+)"\s*\)\s*$)FD");
        std::smatch m;
        
        if (!std::regex_match(line, m, outer_re)) {
            return false;
        }
        
        std::string lhs_inner = m[1].str();
        fd.rhs_column = m[2].str();
        
        // Extract quoted column names from LHS
        static const std::regex tok_re(R"FD("([^"]+)")FD");
        for (std::sregex_iterator it(lhs_inner.begin(), lhs_inner.end(), tok_re), end; 
             it != end; ++it) {
            fd.lhs_columns.push_back((*it)[1].str());
        }
        
        return !fd.lhs_columns.empty() && !fd.rhs_column.empty();
    }
}

bool load_fds_file(const std::string& fds_file, std::vector<FDSpec>& fds) {
    std::cout << "\n=== Loading FD Specifications ===" << std::endl;
    std::cout << "FDS file: " << fds_file << std::endl;
    
    std::ifstream in(fds_file);
    if (!in.is_open()) {
        std::cerr << "ERROR: Cannot open FDS file: " << fds_file << std::endl;
        return false;
    }
    
    fds.clear();
    size_t line_num = 0;
    size_t parse_errors = 0;
    std::string line;
    
    while (std::getline(in, line)) {
        ++line_num;
        trim(line);
        
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        FDSpec fd;
        if (!parse_fd_line(line, fd)) {
            std::cerr << "  WARNING: Failed to parse line " << line_num << ": " << line << std::endl;
            ++parse_errors;
            continue;
        }
        
        fds.push_back(fd);
    }
    
    std::cout << "✓ Loaded " << fds.size() << " FD specifications" << std::endl;
    if (parse_errors > 0) {
        std::cout << "  ⚠ Parse errors: " << parse_errors << std::endl;
    }
    
    // // Print all loaded FDs
    // std::cout << "\nFunctional Dependencies:" << std::endl;
    // for (size_t i = 0; i < fds.size(); ++i) {
    //     std::cout << "[" << (i + 1) << "] ";
    //     fds[i].print();
    // }
    
    return true;
}

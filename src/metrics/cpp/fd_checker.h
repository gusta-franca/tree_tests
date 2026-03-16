// fd_checker.h
#ifndef FD_CHECKER_H
#define FD_CHECKER_H

#include <string>
#include <vector>
#include <memory>

#include "csv_index.h"
#include "fd_input.h"

// Forward declarations
class FDChecker;
class BasicFDChecker;
class PrefixTreeFDChecker;

// Factory function for creating FD checker instances
std::unique_ptr<FDChecker> create_fd_checker(const std::string& impl_name);

// Result of checking a single FD
struct FDCheckResult {
    FDSpec fd;           // The FD that was checked
    bool holds;          // True if FD holds
    std::string reason;  // Non-empty only when holds == false
};

// Abstract interface for FD checking implementations
class FDChecker {
public:
    virtual ~FDChecker() = default;

    // Check a batch of FDs and return results
    // @param index The CSV index to check against
    // @param fds The FD specifications to verify
    // @param parallel If true, check FDs in parallel using multiple threads
    virtual std::vector<FDCheckResult> check_fds(
        const CSVIndex& index,
        const std::vector<FDSpec>& fds,
        bool parallel = false) = 0;
};

// Basic implementation using anchor column selection with row-wise grouping
// Supports parallel checking using std::execution policies when parallel=true
class BasicFDChecker : public FDChecker {
public:
    std::vector<FDCheckResult> check_fds(
        const CSVIndex& index,
        const std::vector<FDSpec>& fds,
        bool parallel = false) override;
};

#endif // FD_CHECKER_H

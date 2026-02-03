// fd_checker_test.cpp
// Test program for refactored FD checker

#include "fd_checker.h"
#include "fd_input.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>

void print_usage(const char *prog) {
	std::cerr << "Usage: " << prog << " <csv_file> <fds_file> [options]" << std::endl;
	std::cerr << std::endl;
	std::cerr << "Options:" << std::endl;
	std::cerr << "  --parallel, -p         Build index in parallel" << std::endl;
	std::cerr << "  --parallel-check, -pc  Check FDs in parallel" << std::endl;
	std::cerr << "  --impl <name>          Implementation: basic (default) or prefix-tree" << std::endl;
}

int main(int argc, char *argv[]) {
	if (argc < 3) {
		print_usage(argv[0]);
		return 1;
	}

	std::string csv_file = argv[1];
	std::string fds_file = argv[2];
	bool parallel = false;
	bool parallel_check = false;
	std::string impl = "basic";

	// Parse optional arguments
	for (int i = 3; i < argc; ++i) {
		if (std::strcmp(argv[i], "--parallel") == 0 || std::strcmp(argv[i], "-p") == 0) {
			parallel = true;
		} else if (std::strcmp(argv[i], "--parallel-check") == 0 || std::strcmp(argv[i], "-pc") == 0) {
			parallel_check = true;
		} else if (std::strcmp(argv[i], "--impl") == 0 && i + 1 < argc) {
			impl = argv[++i];
		}
	}

	std::cout << "========================================" << std::endl;
	std::cout << "FD Checker Test Program" << std::endl;
	std::cout << "========================================" << std::endl;
	std::cout << std::endl;

	// Step 1: Load CSV and build index
	auto index_start = std::chrono::high_resolution_clock::now();
	CSVIndex index;
	if (!load_csv_index(csv_file, index, parallel)) {
		return 1;
	}
	auto index_end = std::chrono::high_resolution_clock::now();
	auto index_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(index_end - index_start).count();

	// Step 2: Load FD specifications
	std::vector<FDSpec> fds;
	if (!load_fds_file(fds_file, fds)) {
		return 1;
	}

	// Step 3: Create FD checker
	std::cout << "\n=== Creating FD Checker ===" << std::endl;
	auto checker = create_fd_checker(impl);
	if (!checker) {
		std::cerr << "ERROR: Unknown implementation: " << impl << std::endl;
		return 1;
	}
	std::cout << "✓ Created " << impl << " checker" << std::endl;

	// Step 4: Check all FDs
	std::cout << "\n=== Checking Functional Dependencies ===" << std::endl;
	std::cout << "Check mode: " << (parallel_check ? "parallel" : "sequential") << std::endl;
	auto check_start = std::chrono::high_resolution_clock::now();
	auto results = checker->check_fds(index, fds, parallel_check);
	auto check_end = std::chrono::high_resolution_clock::now();
	auto check_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(check_end - check_start).count();

	// Step 5: Print results
	size_t ok_count = 0;
	size_t violation_count = 0;

	for (size_t i = 0; i < results.size(); ++i) {
		const auto &result = results[i];
		// std::cout << "[" << (i + 1) << "] ";
		// result.fd.print();

		if (result.holds) {
			// std::cout << "    ✓ OK" << std::endl;
			++ok_count;
		} else {
			// std::cout << "    ✗ VIOLATION: " << result.reason << std::endl;
			++violation_count;
		}
	}

	// Summary
	std::cout << "\n========================================" << std::endl;
	std::cout << "Summary:" << std::endl;
	std::cout << "  Total FDs: " << results.size() << std::endl;
	std::cout << "  Passed: " << ok_count << std::endl;
	std::cout << "  Failed: " << violation_count << std::endl;
	std::cout << "  Index build time: " << index_time_ms << " ms" << std::endl;
	std::cout << "  FD check time: " << check_time_ms << " ms" << std::endl;
	std::cout << "  Total time: " << (index_time_ms + check_time_ms) << " ms" << std::endl;
	std::cout << "========================================" << std::endl;

	return violation_count > 0 ? 1 : 0;
}

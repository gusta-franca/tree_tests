# Makefile for CSV Index with Roaring Bitmaps (GCC version)

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O3 -march=native -DHASHMAP_IS_X86 -DIS_LINUX -DNDEBUG -Isrc/metrics/cpp -Iinclude -Iinclude/hashmap -Wno-interference-size

FD_CHECKER_TEST_TARGET = build/bin/fd_checker_test
FD_CHECKER_TEST_SRCS = fd_checker_test.cpp fd_checker.cpp prefix_tree_fd_checker.cpp fd_input.cpp csv_index.cpp roaring.c
_FD_CHECKER_TEST_OBJS = $(FD_CHECKER_TEST_SRCS:.cpp=.o)
_FD_CHECKER_TEST_OBJS := $(_FD_CHECKER_TEST_OBJS:.c=.o)
FD_CHECKER_TEST_OBJS = $(addprefix build/obj/, $(_FD_CHECKER_TEST_OBJS))

FD_METRICS_TEST_TARGET = build/bin/fd_metrics_test
FD_METRICS_TEST_SRCS = fd_metrics_test.cpp fd_metrics.cpp fd_input.cpp csv_index.cpp roaring.c
_FD_METRICS_TEST_OBJS = $(FD_METRICS_TEST_SRCS:.cpp=.o)
_FD_METRICS_TEST_OBJS := $(_FD_METRICS_TEST_OBJS:.c=.o)
FD_METRICS_TEST_OBJS = $(addprefix build/obj/, $(_FD_METRICS_TEST_OBJS))

FD_METRICS_OPT_TEST_TARGET = build/bin/fd_metrics_opt_test
FD_METRICS_OPT_TEST_SRCS = fd_metrics_opt_test.cpp fd_metrics_opt.cpp fd_input.cpp csv_index.cpp roaring.c
_FD_METRICS_OPT_TEST_OBJS = $(FD_METRICS_OPT_TEST_SRCS:.cpp=.o)
_FD_METRICS_OPT_TEST_OBJS := $(_FD_METRICS_OPT_TEST_OBJS:.c=.o)
FD_METRICS_OPT_TEST_OBJS = $(addprefix build/obj/, $(_FD_METRICS_OPT_TEST_OBJS))

FD_METRICS_PARTITIONED_TEST_TARGET = build/bin/fd_metrics_partitioned_test
FD_METRICS_PARTITIONED_TEST_SRCS = fd_metrics_partitioned_test.cpp fd_metrics_partitioned.cpp fd_input.cpp csv_index.cpp roaring.c
_FD_METRICS_PARTITIONED_TEST_OBJS = $(FD_METRICS_PARTITIONED_TEST_SRCS:.cpp=.o)
_FD_METRICS_PARTITIONED_TEST_OBJS := $(_FD_METRICS_PARTITIONED_TEST_OBJS:.c=.o)
FD_METRICS_PARTITIONED_TEST_OBJS = $(addprefix build/obj/, $(_FD_METRICS_PARTITIONED_TEST_OBJS))

BUCKETING_SIMD_TEST_TARGET = build/bin/bucketing_simd_test
BUCKETING_SIMD_TEST_SRCS = bucketing_simd_test.cpp simd_metrics.cpp metrics.cpp utils.cpp fd_input.cpp csv_index.cpp roaring.c 
_BUCKETING_SIMD_TEST_OBJS = $(BUCKETING_SIMD_TEST_SRCS:.cpp=.o)
_BUCKETING_SIMD_TEST_OBJS := $(_BUCKETING_SIMD_TEST_OBJS:.c=.o)
BUCKETING_SIMD_TEST_OBJS = $(addprefix build/obj/, $(_BUCKETING_SIMD_TEST_OBJS))

ANKERL_TEST_TARGET = build/bin/ankerl_test
ANKERL_TEST_SRCS = ankerl_test.cpp ankerl_metrics.cpp metrics.cpp utils.cpp fd_input.cpp csv_index.cpp roaring.c 
_ANKERL_TEST_OBJS = $(ANKERL_TEST_SRCS:.cpp=.o)
_ANKERL_TEST_OBJS := $(_ANKERL_TEST_OBJS:.c=.o)
ANKERL_TEST_OBJS = $(addprefix build/obj/, $(_ANKERL_TEST_OBJS))


$(FD_CHECKER_TEST_TARGET): $(FD_CHECKER_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_METRICS_TEST_TARGET): $(FD_METRICS_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_METRICS_OPT_TEST_TARGET): $(FD_METRICS_OPT_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_METRICS_PARTITIONED_TEST_TARGET): $(FD_METRICS_PARTITIONED_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUCKETING_SIMD_TEST_TARGET): $(BUCKETING_SIMD_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(ANKERL_TEST_TARGET): $(ANKERL_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@


.PHONY: all clean run setup directories fd_checker_test fd_metrics_test fd_metrics_opt_test fd_metrics_partitioned_test bucketing_simd_test ankerl_test

fd_checker_test: setup $(FD_CHECKER_TEST_TARGET)

fd_metrics_test: setup $(FD_METRICS_TEST_TARGET)

fd_metrics_opt_test: setup $(FD_METRICS_OPT_TEST_TARGET)

fd_metrics_partitioned_test: setup $(FD_METRICS_PARTITIONED_TEST_TARGET)

bucketing_simd_test: setup $(BUCKETING_SIMD_TEST_TARGET)

ankerl_test: setup $(ANKERL_TEST_TARGET)


build/obj/%.o: src/metrics/cpp/%.cpp | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/obj/%.o: src/metrics/cpp/%.c | directories
	$(CC) -O3 -std=c11 -c $< -o $@


directories:
	@mkdir -p build/obj
	@mkdir -p build/bin

setup: directories
	@./setup_deps.sh

all: setup fd_metrics_opt_test fd_metrics_partitioned_test bucketing_simd_test ankerl_test

run: 
	@python3 main.py

clean:
	rm -rf build/

distclean: clean
	rm -rf include/

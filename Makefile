# Makefile for CSV Index with Roaring Bitmaps (GCC version)

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O3 -Isrc/metrics/cpp -Iinclude

# TARGET = build/bin/csv_indexer
# SRCS = main.cpp csv_index.cpp roaring.c
# _OBJS = $(SRCS:.cpp=.o)
# _OBJS := $(_OBJS:.c=.o)
# OBJS = $(addprefix build/obj/, $(_OBJS))

TEST_TARGET = build/bin/test_index_equivalence
TEST_SRCS = test_index_equivalence.cpp csv_index.cpp roaring.c
_TEST_OBJS = $(TEST_SRCS:.cpp=.o)
_TEST_OBJS := $(_TEST_OBJS:.c=.o)
TEST_OBJS = $(addprefix build/obj/, $(_TEST_OBJS))

FD_TARGET = build/bin/fd_check
FD_SRCS = fd_main.cpp fd_checker.cpp csv_index.cpp roaring.c
_FD_OBJS = $(FD_SRCS:.cpp=.o)
_FD_OBJS := $(_FD_OBJS:.c=.o)
FD_OBJS = $(addprefix build/obj/, $(_FD_OBJS))

FD_FILE_TARGET = build/bin/fd_file_check
FD_FILE_SRCS = fd_file_check.cpp fd_checker.cpp csv_index.cpp roaring.c
_FD_FILE_OBJS = $(FD_FILE_SRCS:.cpp=.o)
_FD_FILE_OBJS := $(_FD_FILE_OBJS:.c=.o)
FD_FILE_OBJS = $(addprefix build/obj/, $(_FD_FILE_OBJS))

FD_INPUT_TARGET = build/bin/fd_input_test
FD_INPUT_SRCS = fd_input_test.cpp fd_input.cpp csv_index.cpp roaring.c
_FD_INPUT_OBJS = $(FD_INPUT_SRCS:.cpp=.o)
_FD_INPUT_OBJS := $(_FD_INPUT_OBJS:.c=.o)
FD_INPUT_OBJS = $(addprefix build/obj/, $(_FD_INPUT_OBJS))

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

.PHONY: all clean run setup directories fd_input fd_checker_test fd_metrics_test fd_metrics_opt_test 

directories:
	@mkdir -p build/obj
	@mkdir -p build/bin

setup: directories
	@./setup_deps.sh

all: setup $(TARGET)

test: setup $(TEST_TARGET)

fd_input: setup $(FD_INPUT_TARGET)

fd_checker_test: setup $(FD_CHECKER_TEST_TARGET)

fd_metrics_test: setup $(FD_METRICS_TEST_TARGET)

fd_metrics_opt_test: setup $(FD_METRICS_OPT_TEST_TARGET)

# $(TARGET): $(OBJS)
# 	$(CXX) $(CXXFLAGS) $^ -o $@

$(TEST_TARGET): $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_TARGET): $(FD_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_FILE_TARGET): $(FD_FILE_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_INPUT_TARGET): $(FD_INPUT_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_CHECKER_TEST_TARGET): $(FD_CHECKER_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_METRICS_TEST_TARGET): $(FD_METRICS_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_METRICS_OPT_TEST_TARGET): $(FD_METRICS_OPT_TEST_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@

build/obj/%.o: src/metrics/cpp/%.cpp | directories
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/obj/%.o: src/metrics/cpp/%.c | directories
	$(CC) -O3 -std=c11 -c $< -o $@

# run: $(TARGET)
# 	./$(TARGET) data/test_int.csv

clean:
	rm -rf build/

distclean: clean
	rm -rf include/

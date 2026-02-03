# Makefile for CSV Index with Roaring Bitmaps (GCC version)

CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O3 -I. -Iinclude
TARGET = csv_indexer
SRCS = main.cpp csv_index.cpp roaring.c
OBJS = $(SRCS:.cpp=.o)
OBJS := $(OBJS:.c=.o)

.PHONY: all clean run setup fd_input fd_checker_test

TEST_TARGET = test_index_equivalence
TEST_SRCS = test_index_equivalence.cpp csv_index.cpp roaring.c
TEST_OBJS = $(TEST_SRCS:.cpp=.o)
TEST_OBJS := $(TEST_OBJS:.c=.o)

# FD checker tool
FD_TARGET = fd_check
FD_SRCS = fd_main.cpp fd_checker.cpp csv_index.cpp roaring.c
FD_OBJS = $(FD_SRCS:.cpp=.o)
FD_OBJS := $(FD_OBJS:.c=.o)

# FD file checker tool
FD_FILE_TARGET = fd_file_check
FD_FILE_SRCS = fd_file_check.cpp fd_checker.cpp csv_index.cpp roaring.c
FD_FILE_OBJS = $(FD_FILE_SRCS:.cpp=.o)
FD_FILE_OBJS := $(FD_FILE_OBJS:.c=.o)

# FD input test
FD_INPUT_TARGET = fd_input_test
FD_INPUT_SRCS = fd_input_test.cpp fd_input.cpp csv_index.cpp roaring.c
FD_INPUT_OBJS = $(FD_INPUT_SRCS:.cpp=.o)
FD_INPUT_OBJS := $(FD_INPUT_OBJS:.c=.o)

# FD checker test
FD_CHECKER_TEST_TARGET = fd_checker_test
FD_CHECKER_TEST_SRCS = fd_checker_test.cpp fd_checker.cpp prefix_tree_fd_checker.cpp fd_input.cpp csv_index.cpp roaring.c
FD_CHECKER_TEST_OBJS = $(FD_CHECKER_TEST_SRCS:.cpp=.o)
FD_CHECKER_TEST_OBJS := $(FD_CHECKER_TEST_OBJS:.c=.o)

# FD metrics test
FD_METRICS_TEST_TARGET = fd_metrics_test
FD_METRICS_TEST_SRCS = fd_metrics_test.cpp fd_metrics.cpp fd_input.cpp csv_index.cpp roaring.c
FD_METRICS_TEST_OBJS = $(FD_METRICS_TEST_SRCS:.cpp=.o)
FD_METRICS_TEST_OBJS := $(FD_METRICS_TEST_OBJS:.c=.o)

# FD metrics optimized test
FD_METRICS_OPT_TEST_TARGET = fd_metrics_opt_test
FD_METRICS_OPT_TEST_SRCS = fd_metrics_opt_test.cpp fd_metrics_opt.cpp fd_input.cpp roaring.c
FD_METRICS_OPT_TEST_OBJS = $(FD_METRICS_OPT_TEST_SRCS:.cpp=.o)
FD_METRICS_OPT_TEST_OBJS := $(FD_METRICS_OPT_TEST_OBJS:.c=.o)

all: setup $(TARGET)

test: setup $(TEST_TARGET)

$(TEST_TARGET): test_index_equivalence.o csv_index.o roaring.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_TARGET): fd_main.o fd_checker.o csv_index.o roaring.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_FILE_TARGET): fd_file_check.o fd_checker.o csv_index.o roaring.o
	$(CXX) $(CXXFLAGS) $^ -o $@

fd_input: $(FD_INPUT_TARGET)

fd_checker_test: $(FD_CHECKER_TEST_TARGET)

# fd_metrics_test: $(FD_METRICS_TEST_TARGET)

$(FD_INPUT_TARGET): fd_input_test.o fd_input.o csv_index.o roaring.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_CHECKER_TEST_TARGET): fd_checker_test.o fd_checker.o prefix_tree_fd_checker.o fd_input.o csv_index.o roaring.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_METRICS_TEST_TARGET): fd_metrics_test.o fd_metrics.o fd_input.o csv_index.o roaring.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FD_METRICS_OPT_TEST_TARGET): fd_metrics_opt_test.o fd_metrics_opt.o fd_input.o csv_index.o roaring.o
	$(CXX) $(CXXFLAGS) $^ -o $@

setup:
	@./setup_deps.sh

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) -O3 -std=c11 -c $< -o $@

$(TARGET): main.o csv_index.o roaring.o
	$(CXX) $(CXXFLAGS) $^ -o $@

run: $(TARGET)
	./$(TARGET) ../data/test_int.csv

clean:
	rm -f $(TARGET) *.o
	rm -rf include/

distclean: clean
	rm -rf include/
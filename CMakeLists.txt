cmake_minimum_required(VERSION 3.16)
project(fd_metrics LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/bin)

add_compile_options(-Wall -Wextra -O3 -march=native -Wno-interference-size)
add_compile_definitions(HASHMAP_IS_X86 IS_LINUX NDEBUG)

find_package(OpenMP REQUIRED)

include_directories(
    src/metrics/cpp
    include
    include/hashmap
)

add_library(core STATIC
    src/metrics/cpp/csv_index.cpp
    src/metrics/cpp/fd_input.cpp
    src/metrics/cpp/utils.cpp
    src/metrics/cpp/metrics.cpp
    src/metrics/cpp/roaring.c
    src/metrics/cpp/sampling.cpp
)
target_link_libraries(core PUBLIC OpenMP::OpenMP_CXX)

add_library(auto_relate_lib STATIC
    src/metrics/cpp/auto_relate.cpp
)
target_link_libraries(auto_relate_lib PUBLIC core)

add_executable(ankerl_test
    src/metrics/cpp/ankerl_test.cpp
    src/metrics/cpp/ankerl_metrics.cpp
)
target_link_libraries(ankerl_test PRIVATE auto_relate_lib)

add_executable(auto_relate_test
    src/metrics/cpp/auto_relate_test.cpp
)
target_link_libraries(auto_relate_test PRIVATE auto_relate_lib)

add_executable(bucketing_simd_test
    src/metrics/cpp/bucketing_simd_test.cpp
    src/metrics/cpp/simd_metrics.cpp
)
target_link_libraries(bucketing_simd_test PRIVATE core)

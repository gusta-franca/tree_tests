cmake_minimum_required(VERSION 3.16)
project(fd_metrics LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/bin)

add_compile_options(-Wall -Wextra -O3 -march=native -Wno-interference-size)
add_compile_definitions(HASHMAP_IS_X86 IS_LINUX NDEBUG)

find_package(OpenMP REQUIRED)
find_package(Boost REQUIRED)

set(CORE_DIR   ${CMAKE_SOURCE_DIR}/src/cpp/core)
set(BASE_DIR   ${CMAKE_SOURCE_DIR}/src/cpp/base)
set(OPT_DIR    ${CMAKE_SOURCE_DIR}/src/cpp/opt)
set(VENDOR_DIR ${CMAKE_SOURCE_DIR}/src/cpp/vendor)

include_directories(
    ${CORE_DIR}
    ${VENDOR_DIR}
    ${VENDOR_DIR}/hashmap
)

add_library(core STATIC
    ${CORE_DIR}/csv_index.cpp
    ${CORE_DIR}/fd_input.cpp
    ${CORE_DIR}/utils.cpp
    ${CORE_DIR}/metrics.cpp
    ${CORE_DIR}/sampling.cpp
    ${VENDOR_DIR}/roaring.c
)
target_link_libraries(core PUBLIC OpenMP::OpenMP_CXX)

add_library(naive_metrics_lib STATIC
    ${BASE_DIR}/naive_metrics.cpp
)
target_link_libraries(naive_metrics_lib PUBLIC core)

add_library(auto_relate_lib STATIC
    ${BASE_DIR}/auto_relate.cpp
)
target_include_directories(auto_relate_lib PUBLIC ${BASE_DIR})
target_link_libraries(auto_relate_lib PUBLIC core Boost::boost)

add_executable(auto_relate_test ${BASE_DIR}/auto_relate_test.cpp)
target_link_libraries(auto_relate_test PRIVATE auto_relate_lib)

add_executable(ankerl_test
    ${OPT_DIR}/ankerl_test.cpp
    ${OPT_DIR}/ankerl_metrics.cpp
)
target_link_libraries(ankerl_test PRIVATE auto_relate_lib)

add_executable(simd_test
    ${OPT_DIR}/simd_test.cpp
    ${OPT_DIR}/simd_metrics.cpp
)
target_link_libraries(simd_test PRIVATE core)

foreach(NAIVE_BIN mu_test rfi_test g1_test g2_test g3_test)
    add_executable(${NAIVE_BIN} ${BASE_DIR}/${NAIVE_BIN}.cpp)
    target_link_libraries(${NAIVE_BIN} PRIVATE naive_metrics_lib)
endforeach()

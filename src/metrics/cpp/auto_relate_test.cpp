#include <chrono>
#include <iostream>
#include <string>

#include "auto_relate.h"
#include "csv_index.h"

std::vector<int> parse_violation_rows(const std::string& rows_str) {
    std::vector<int> rows;

    if (rows_str.empty() || rows_str == "") {
        return rows;
    }

    std::stringstream ss(rows_str);
    std::string r;

    while (std::getline(ss, r, ',')) {
        rows.push_back(stoi(r));
    }

    return rows;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <csv_file> <left_col> <right_col> <dirty|clean>" << std::endl;
        
        return 1;
    }

    std::string csv_file = argv[1];
    std::string left_col = argv[2];
    std::string right_col = argv[3];
    std::string mode = (argc > 4) ? argv[4] : "dirty";
    
    std::string v_rows_str;
    std::getline(std::cin, v_rows_str);

    std::vector<int> violation_rows = parse_violation_rows(v_rows_str);

    AutoRelateFDConfig config;

    config.dirty_data = (mode == "dirty");
    config.use_independence_test = config.dirty_data;

    std::chrono::duration<double> load_time_s(0);
    auto load_start = std::chrono::steady_clock::now();

    ColumnarData data;
    if (!load_csv_columnar(csv_file, data, false)) {
        std::cerr << "Error: Failed to load CSV file" << std::endl;
        return 1;
    }

    auto load_end = std::chrono::steady_clock::now();
    load_time_s = (load_end - load_start);

    AutoRelateFDResult result = compute_auto_relate_fd(data, left_col, right_col, violation_rows, config);

    std::cout << "RESULT_JSON: {"
              << "\"left_col\": \"" << result.left_col << "\", "
              << "\"right_col\": \"" << result.right_col << "\", "
              << "\"score\": " << result.score << ", "
              << "\"independence_pvalue\": " << result.independence_pvalue << ", "
              << "\"independence_used\": " << result.independence_used << ", "
              << "\"independence_rejected\": " << result.independence_rejected << ", "
              << "\"is_reliable\": " << (result.is_reliable ? "true" : "false") << ", "
              << "\"violation_count\": " << result.violation_count << ", "
              << "\"violation_rate\": " << result.violation_rate << ", "
              << "\"load_time_s\": " << load_time_s.count() << ", "
              << "\"build_time_s\": " << result.build_time_s << ", "
              << "\"compute_time_s\": " << result.compute_time_s
              << "}" << std::endl;

    return 0;
}

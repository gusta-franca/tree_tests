// fd_metrics_anchor.h
#ifndef FD_METRICS_CHECKER_H
#define FD_METRICS_CHECKER_H

#include <vector>
#include <cstdint>

#include "csv_index.h" 
#include "fd_input.h"


double compute_pdep(
    const CSVIndex& data, // Still using CSVIndex for inverted index
    const FDSpec& fd
);

#endif // FD_METRICS_ANCHOR_H

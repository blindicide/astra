#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace astra {
struct ProcessResult {
    int status = -1;
    bool timed_out = false;
    bool truncated = false;
    std::string output;
    std::string error;
};
ProcessResult run_process(const std::vector<std::string>& arguments, int timeout_seconds = 5,
                          size_t max_output_bytes = 1024 * 1024);
}

#pragma once
#include <string>
#include <vector>

namespace astra {
struct ProcessResult {
    int status = -1;
    bool timed_out = false;
    std::string output;
};
ProcessResult run_process(const std::vector<std::string>& arguments, int timeout_seconds = 5);
}

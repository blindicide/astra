#pragma once
#include <string>

namespace astra {
struct BackendStatus {
    bool executable_found = false;
    bool version_supported = false;
    std::string version;
    std::string detail;
};
BackendStatus detect_backend();
void require_trusted_cache_policy(const BackendStatus& backend);
}

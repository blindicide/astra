#include "astra/backend.hpp"
#include "astra/process.hpp"

#include <regex>

namespace astra {
BackendStatus detect_backend() {
    BackendStatus status;
    ProcessResult result = run_process({"nix", "--version"}, 5);
    if (result.timed_out) { status.detail = "Nix version check timed out"; return status; }
    if (result.status == 127) { status.detail = "Nix executable is missing from PATH"; return status; }
    if (result.status != 0) { status.detail = "Nix version check failed"; return status; }
    status.executable_found = true;
    std::regex pattern(R"(^nix \(Nix\) ([0-9]+)\.([0-9]+)(\.[0-9]+)?\s*$)");
    std::smatch match;
    if (!std::regex_match(result.output, match, pattern)) { status.detail = "Unrecognized Nix version output"; return status; }
    status.version = match[1].str() + "." + match[2].str() + match[3].str();
    int major = std::stoi(match[1].str()), minor = std::stoi(match[2].str());
    status.version_supported = major == 2 && minor >= 18 && minor <= 30;
    status.detail = status.version_supported ? "Nix version is a compatibility candidate; runtime capabilities need verification" : "Nix version is outside the candidate compatibility range 2.18–2.30";
    return status;
}
}

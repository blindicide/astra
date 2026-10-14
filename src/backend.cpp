#include "astra/backend.hpp"
#include "astra/process.hpp"

#include <json-c/json.h>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>

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
void require_trusted_cache_policy(const BackendStatus& backend) {
    if (!backend.version_supported) throw std::runtime_error("supported Nix backend required for cache policy");
    const std::vector<std::string> command = backend.version == "2.18" || backend.version.rfind("2.18.", 0) == 0 ||
        backend.version == "2.19" || backend.version.rfind("2.19.", 0) == 0
        ? std::vector<std::string>{"nix", "--extra-experimental-features", "nix-command flakes", "show-config", "--json"}
        : std::vector<std::string>{"nix", "--extra-experimental-features", "nix-command flakes", "config", "show", "--json"};
    ProcessResult result = run_process(command, 15, 8 * 1024 * 1024);
    if (result.status != 0 || result.timed_out || result.truncated)
        throw std::runtime_error("cannot verify effective Nix cache trust configuration");
    json_tokener* tokener = json_tokener_new();
    if (!tokener) throw std::runtime_error("cannot allocate Nix configuration parser");
    json_object* raw = json_tokener_parse_ex(tokener, result.output.data(), static_cast<int>(result.output.size()));
    bool valid = json_tokener_get_error(tokener) == json_tokener_success && raw && json_object_is_type(raw, json_type_object);
    json_tokener_free(tokener);
    std::unique_ptr<json_object, decltype(&json_object_put)> configuration(raw, json_object_put);
    if (!valid) throw std::runtime_error("invalid Nix cache trust configuration output");
    auto value = [&](const char* key) -> std::string {
        json_object* setting = nullptr;
        if (!json_object_object_get_ex(configuration.get(), key, &setting))
            throw std::runtime_error("Nix cache trust configuration is missing a required setting");
        if (json_object_is_type(setting, json_type_object) && !json_object_object_get_ex(setting, "value", &setting))
            throw std::runtime_error("Nix cache trust configuration has an invalid setting");
        if (json_object_is_type(setting, json_type_boolean)) return json_object_get_boolean(setting) ? "true" : "false";
        if (!json_object_is_type(setting, json_type_string))
            throw std::runtime_error("Nix cache trust configuration has an invalid setting");
        return json_object_get_string(setting);
    };
    if (value("require-sigs") != "true")
        throw std::runtime_error("Nix cache policy requires signature verification; require-sigs must be true");
    constexpr const char* official_key = "cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY=";
    if (value("trusted-public-keys") != official_key)
        throw std::runtime_error("Nix cache policy rejects unapproved trusted public keys");
    std::istringstream sources(value("substituters"));
    std::string source, extra;
    if (!(sources >> source) || (source != "https://cache.nixos.org/" && source != "https://cache.nixos.org") || sources >> extra)
        throw std::runtime_error("Nix cache policy rejects unapproved substituters");
}
}

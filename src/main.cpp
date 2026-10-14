#include "astra/backend.hpp"
#include "astra/database.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string json_escape(const std::string& value) {
    std::string out;
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20) { const char* hex = "0123456789abcdef"; out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out;
}
void help() {
    std::cout << "ASTRA 0.1.0 — nixpkgs-backed application manager (development)\n"
              << "Usage: astra [--json] <command>\n"
              << "Commands: help, doctor, verify\n"
              << "Planned commands: search, info, list, why, install, remove, upgrade, update,\n"
              << "                  history, rollback, repair, clean, repo\n";
}
}
int main(int argc, char** argv) {
    try {
        bool json = false;
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            if (arg == "--json") json = true;
            else args.push_back(arg);
        }
        if (args.empty() || (args.size() == 1 && (args[0] == "help" || args[0] == "--help"))) {
            if (json) throw std::invalid_argument("--json is not supported for help");
            help(); return 0;
        }
        if (args.size() == 1 && args[0] == "--version") {
            if (json) std::cout << "{\"schema_version\":1,\"version\":\"0.1.0\"}\n";
            else std::cout << "astra 0.1.0\n";
            return 0;
        }
        if (args.size() == 1 && (args[0] == "doctor" || args[0] == "verify")) {
            astra::Database db(astra::state_directory() / "state.db");
            std::string integrity = db.integrity_check();
            astra::BackendStatus backend = astra::detect_backend();
            if (json) {
                std::cout << "{\"schema_version\":1,\"command\":\"" << args[0] << "\",\"database\":{\"integrity\":\""
                          << json_escape(integrity) << "\",\"schema_version\":" << db.schema_version()
                          << "},\"backend\":{\"found\":" << (backend.executable_found ? "true" : "false")
                          << ",\"candidate_version\":" << (backend.version_supported ? "true" : "false")
                          << ",\"version\":\"" << json_escape(backend.version) << "\",\"detail\":\""
                          << json_escape(backend.detail) << "\"}}\n";
            } else {
                std::cout << "Database: " << integrity << " (schema " << db.schema_version() << ")\n"
                          << "Backend: " << backend.detail << "\n";
            }
            return integrity == "ok" && backend.version_supported ? 0 : 1;
        }
        throw std::invalid_argument("command is unavailable in this development build; run 'astra help'");
    } catch (const std::exception& error) {
        std::cerr << "astra: " << error.what() << '\n';
        return 2;
    }
}

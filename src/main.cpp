#include "astra/backend.hpp"
#include "astra/database.hpp"
#include "astra/repository.hpp"

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
    std::cout << "ASTRA 0.2.0-dev — nixpkgs-backed application manager (development)\n"
              << "Usage: astra [--json] [--offline] <command>\n"
              << "Commands: help, doctor, verify, repo list, repo update, update, search, info\n"
              << "Planned commands: list, why, install, remove, upgrade, history, rollback,\n"
              << "                  repair, clean\n";
}
}
int main(int argc, char** argv) {
    try {
        bool json = false, offline = false;
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            if (arg == "--json") json = true;
            else if (arg == "--offline") offline = true;
            else args.push_back(arg);
        }
        if (args.empty() || (args.size() == 1 && (args[0] == "help" || args[0] == "--help"))) {
            if (json) throw std::invalid_argument("--json is not supported for help");
            help(); return 0;
        }
        if (args.size() == 1 && args[0] == "--version") {
            if (offline) throw std::invalid_argument("--offline is not meaningful for --version");
            if (json) std::cout << "{\"schema_version\":1,\"version\":\"0.2.0-dev\"}\n";
            else std::cout << "astra 0.2.0-dev\n";
            return 0;
        }
        if (args.size() == 1 && (args[0] == "doctor" || args[0] == "verify")) {
            if (offline) throw std::invalid_argument("--offline is not meaningful for this command");
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
        astra::Database db(astra::state_directory() / "state.db");
        astra::RepositoryManager repo(db);
        if (args.size() == 2 && args[0] == "repo" && args[1] == "list") {
            if (offline) throw std::invalid_argument("--offline is not meaningful for repo list");
            std::string revision = repo.selected_revision();
            if (json) std::cout << "{\"schema_version\":1,\"command\":\"repo list\",\"repositories\":["
                                << (revision.empty() ? "" : "{\"name\":\"nixpkgs\",\"revision\":\"" + json_escape(revision) + "\"}") << "]}\n";
            else std::cout << (revision.empty() ? "No repository revision is indexed\n" : "nixpkgs " + revision + "\n");
            return 0;
        }
        if ((args.size() == 2 && args[0] == "repo" && args[1] == "update") || (args.size() == 1 && args[0] == "update")) {
            size_t count = repo.update(offline);
            if (json) std::cout << "{\"schema_version\":1,\"command\":\"repo update\",\"revision\":\""
                                << json_escape(repo.selected_revision()) << "\",\"indexed_packages\":" << count << "}\n";
            else std::cout << "Indexed " << count << " packages from nixpkgs " << repo.selected_revision() << '\n';
            return 0;
        }
        if (args.size() == 2 && args[0] == "search") {
            auto matches = repo.search(args[1]);
            if (json) {
                std::cout << "{\"schema_version\":1,\"command\":\"search\",\"packages\":[";
                for (size_t i = 0; i < matches.size(); ++i) {
                    const auto& p = matches[i];
                    if (i) std::cout << ',';
                    std::cout << "{\"attribute\":\"" << json_escape(p.attribute) << "\",\"name\":\"" << json_escape(p.name)
                              << "\",\"version\":\"" << json_escape(p.version) << "\",\"description\":\""
                              << json_escape(p.description) << "\",\"revision\":\"" << json_escape(p.revision) << "\"}";
                }
                std::cout << "]}\n";
            } else {
                for (const auto& p : matches) std::cout << p.attribute << " " << p.version << " — " << p.description << '\n';
                if (matches.empty()) std::cout << "No indexed packages matched. Run 'astra repo update' if the catalog is empty.\n";
            }
            return 0;
        }
        if (args.size() == 2 && args[0] == "info") {
            auto p = repo.info(args[1]);
            if (json) std::cout << "{\"schema_version\":1,\"command\":\"info\",\"package\":{\"attribute\":\""
                                << json_escape(p.attribute) << "\",\"name\":\"" << json_escape(p.name) << "\",\"version\":\""
                                << json_escape(p.version) << "\",\"description\":\"" << json_escape(p.description)
                                << "\",\"license\":null,\"availability\":\"unverified\",\"system\":\""
                                << json_escape(p.system) << "\",\"revision\":\"" << json_escape(p.revision) << "\"}}\n";
            else std::cout << p.name << ' ' << p.version << "\nAttribute: " << p.attribute << "\nDescription: " << p.description
                           << "\nLicense: not indexed\nAvailability: unverified\nSystem: " << p.system << "\nRepository revision: " << p.revision << '\n';
            return 0;
        }
        throw std::invalid_argument("command is unavailable in this development build; run 'astra help'");
    } catch (const std::exception& error) {
        std::cerr << "astra: " << error.what() << '\n';
        return 2;
    }
}

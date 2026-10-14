#include "astra/backend.hpp"
#include "astra/database.hpp"
#include "astra/process.hpp"
#include "astra/repository.hpp"
#include "astra/transaction.hpp"

#include <iostream>
#include <streambuf>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
class QuietOutput {
public:
    explicit QuietOutput(bool enabled) {
        if (enabled) original_ = std::cout.rdbuf(&discard_);
    }
    ~QuietOutput() { if (original_) std::cout.rdbuf(original_); }
private:
    class DiscardBuffer : public std::streambuf {
        int overflow(int character) override { return traits_type::not_eof(character); }
    } discard_;
    std::streambuf* original_ = nullptr;
};
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
    std::cout << "ASTRA 0.3.0-dev — nixpkgs-backed application manager (development)\n"
              << "Usage: astra [--json|--quiet|--verbose] [--offline] [--profile NAME] [--yes] [--dry-run] [--allow-build] <command>\n"
              << "Commands: help, doctor, verify, repo list, repo update, update, search, info,\n"
              << "          list, install, remove, upgrade, history, rollback\n"
              << "          why\n"
              << "          clean, repair\n";
}
}
int main(int argc, char** argv) {
    try {
        bool json = false, quiet = false, verbose = false, offline = false, yes = false, dry_run = false, allow_build = false;
        bool help_requested = false, profile_supplied = false;
        std::string profile = "default";
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            if (arg == "--json") json = true;
            else if (arg == "--quiet") quiet = true;
            else if (arg == "--verbose") verbose = true;
            else if (arg == "--help") help_requested = true;
            else if (arg == "--version") args.push_back(arg);
            else if (arg == "--offline") offline = true;
            else if (arg == "--yes") yes = true;
            else if (arg == "--dry-run") dry_run = true;
            else if (arg == "--allow-build") allow_build = true;
            else if (arg == "--profile") { if (++i >= argc) throw std::invalid_argument("--profile needs a name"); profile = argv[i]; profile_supplied = true; }
            else if (arg.rfind("--", 0) == 0) throw std::invalid_argument("unknown switch: " + arg);
            else args.push_back(arg);
        }
        if ((json && quiet) || (quiet && verbose) || (json && verbose))
            throw std::invalid_argument("--json, --quiet, and --verbose are mutually exclusive");
        if (help_requested) {
            if (json || quiet || verbose || offline || yes || dry_run || allow_build || profile_supplied)
                throw std::invalid_argument("--help cannot be combined with other switches");
            help(); return 0;
        }
        const std::string command = args.empty() ? "" : args[0];
        const bool mutating = command == "install" || command == "remove" || command == "upgrade" || command == "rollback" || command == "clean" || command == "repair";
        const bool profile_command = mutating || command == "list" || command == "why" || command == "history";
        if (profile_supplied && !profile_command)
            throw std::invalid_argument("--profile is only meaningful for profile commands");
        if ((yes || dry_run) && !mutating)
            throw std::invalid_argument("--yes and --dry-run are only meaningful for profile changes, clean, or repair");
        if (quiet && !(mutating || command == "update" || (command == "repo" && args.size() == 2 && args[1] == "update")))
            throw std::invalid_argument("--quiet is only meaningful for commands that report an operation result");
        if (allow_build && (args.empty() || (args[0] != "install" && args[0] != "remove" && args[0] != "upgrade")))
            throw std::invalid_argument("--allow-build is only meaningful for install, remove, or upgrade");
        if (args.empty() || (args.size() == 1 && args[0] == "help")) {
            if (json) throw std::invalid_argument("--json is not supported for help");
            if (verbose) throw std::invalid_argument("--verbose is not meaningful for help");
            help(); return 0;
        }
        if (args.size() == 1 && args[0] == "--version") {
            if (offline) throw std::invalid_argument("--offline is not meaningful for --version");
            if (quiet || verbose) throw std::invalid_argument("--quiet and --verbose are not meaningful for --version");
            if (json) std::cout << "{\"schema_version\":1,\"version\":\"0.3.0-dev\"}\n";
            else std::cout << "astra 0.3.0-dev\n";
            return 0;
        }
        QuietOutput quiet_output(quiet);
        std::ostream& out = std::cout;
        if (verbose) std::cerr << "ASTRA state directory: " << astra::state_directory() << '\n';
        if (args.size() == 1 && (args[0] == "doctor" || args[0] == "verify")) {
            astra::Database db(astra::state_directory() / "state.db");
            std::string integrity = db.integrity_check();
            int profiles_verified = 0;
            if (args[0] == "verify" && integrity == "ok") {
                astra::RepositoryManager repository(db);
                sqlite3_stmt* profiles = nullptr;
                if (sqlite3_prepare_v2(db.handle(), "SELECT name FROM profiles WHERE scope='user' ORDER BY name;", -1, &profiles, nullptr) != SQLITE_OK)
                    throw std::runtime_error(sqlite3_errmsg(db.handle()));
                try {
                    int step = SQLITE_ROW;
                    while ((step = sqlite3_step(profiles)) == SQLITE_ROW) {
                        std::string name(reinterpret_cast<const char*>(sqlite3_column_text(profiles, 0)));
                        astra::TransactionEngine engine(db, repository, name);
                        engine.recover();
                        ++profiles_verified;
                    }
                    if (step != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db.handle()));
                } catch (...) { sqlite3_finalize(profiles); throw; }
                sqlite3_finalize(profiles);
            }
            astra::BackendStatus backend = astra::detect_backend();
            bool runtime_found = false, policy_trusted = false, daemon_reachable = false, cache_reachable = false;
            std::string policy_detail = "not checked", daemon_detail = "not checked", cache_detail = offline ? "skipped offline" : "not checked";
            if (args[0] == "doctor" && backend.version_supported) {
                const auto store = astra::run_process({"nix-store", "--version"}, 5);
                const auto environment = astra::run_process({"nix-env", "--version"}, 5);
                runtime_found = store.status == 0 && environment.status == 0 && !store.timed_out && !environment.timed_out;
                try { astra::require_trusted_cache_policy(backend); policy_trusted = true; policy_detail = "approved signatures and official cache"; }
                catch (const std::exception& error) { policy_detail = error.what(); }
                if (runtime_found && policy_trusted) {
                    const std::vector<std::string> base{"nix", "--extra-experimental-features", "nix-command flakes",
                        "--option", "require-sigs", "true", "--option", "trusted-public-keys",
                        "cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY=",
                        "--option", "substituters", "https://cache.nixos.org/", "store", "ping", "--store"};
                    auto daemon_command = base;
                    daemon_command.push_back("daemon");
                    auto daemon = astra::run_process(daemon_command, 10);
                    daemon_reachable = daemon.status == 0 && !daemon.timed_out;
                    daemon_detail = daemon_reachable ? "reachable" : "Nix daemon is unreachable";
                    if (!offline) {
                        auto cache_command = base;
                        cache_command.push_back("https://cache.nixos.org/");
                        auto cache = astra::run_process(cache_command, 15);
                        cache_reachable = cache.status == 0 && !cache.timed_out;
                        cache_detail = cache_reachable ? "official cache reachable" : "official cache is unreachable";
                    }
                }
            }
            if (json) {
                out << "{\"schema_version\":1,\"command\":\"" << args[0] << "\",\"database\":{\"integrity\":\""
                          << json_escape(integrity) << "\",\"schema_version\":" << db.schema_version()
                          << "},\"profiles_verified\":" << profiles_verified << ",\"backend\":{\"found\":" << (backend.executable_found ? "true" : "false")
                          << ",\"candidate_version\":" << (backend.version_supported ? "true" : "false")
                          << ",\"version\":\"" << json_escape(backend.version) << "\",\"detail\":\""
                          << json_escape(backend.detail) << "\"}";
                if (args[0] == "doctor")
                    out << ",\"runtime\":{\"found\":" << (runtime_found ? "true" : "false")
                        << "},\"cache_policy\":{\"trusted\":" << (policy_trusted ? "true" : "false")
                        << ",\"detail\":\"" << json_escape(policy_detail) << "\"},\"daemon\":{\"reachable\":"
                        << (daemon_reachable ? "true" : "false") << ",\"detail\":\"" << json_escape(daemon_detail)
                        << "\"},\"cache\":{\"reachable\":" << (cache_reachable ? "true" : "false")
                        << ",\"detail\":\"" << json_escape(cache_detail) << "\"}";
                out << "}\n";
            } else {
                out << "Database: " << integrity << " (schema " << db.schema_version() << ")\n"
                          << "Profiles verified: " << profiles_verified << "\nBackend: " << backend.detail << "\n";
                if (args[0] == "doctor")
                    out << "Runtime commands: " << (runtime_found ? "available" : "missing or failed")
                        << "\nCache policy: " << policy_detail << "\nDaemon: " << daemon_detail
                        << "\nCache: " << cache_detail << "\n";
            }
            bool healthy = integrity == "ok" && backend.version_supported;
            if (args[0] == "doctor") healthy = healthy && runtime_found && policy_trusted && daemon_reachable && (offline || cache_reachable);
            return healthy ? 0 : 1;
        }
        astra::Database db(astra::state_directory() / "state.db");
        astra::RepositoryManager repo(db);
        if (!args.empty() && (args[0] == "install" || args[0] == "remove" || args[0] == "upgrade" || args[0] == "list" || args[0] == "why" || args[0] == "history" || args[0] == "rollback" || args[0] == "clean" || args[0] == "repair")) {
            if (json && !(args[0] == "history" && args.size() == 3 && args[1] == "show"))
                throw std::invalid_argument("--json is not yet available for this profile command");
            astra::TransactionEngine engine(db, repo, profile);
            if (args[0] == "clean" && args.size() == 1) { out << engine.clean(yes, dry_run); return 0; }
            if (args[0] == "repair" && args.size() == 1) { out << engine.repair(yes, dry_run); return 0; }
            if (args[0] == "list" && args.size() == 1) { engine.recover(); out << engine.list(); return 0; }
            if (args[0] == "why" && args.size() == 2) { engine.recover(); out << engine.why(args[1]); return 0; }
            if (args[0] == "history" && args.size() == 1) { engine.recover(); out << engine.history(); return 0; }
            if (args[0] == "history" && args.size() == 3 && args[1] == "show") {
                engine.recover(); out << engine.history_show(args[2], json); return 0;
            }
            if (args[0] == "rollback" && args.size() <= 2) {
                std::string result = engine.mutate("rollback", std::vector<std::string>(args.begin() + 1, args.end()), yes, dry_run, offline, allow_build);
                out << (dry_run ? result : "Transaction " + result + " committed") << '\n';
                return 0;
            }
            if (((args[0] == "install" || args[0] == "remove") && args.size() > 1) || args[0] == "upgrade") {
                std::string result = engine.mutate(args[0], std::vector<std::string>(args.begin() + 1, args.end()), yes, dry_run, offline, allow_build);
                out << (dry_run ? result : "Transaction " + result + " committed") << '\n';
                return 0;
            }
        }
        if (args.size() == 2 && args[0] == "repo" && args[1] == "list") {
            if (offline) throw std::invalid_argument("--offline is not meaningful for repo list");
            std::string revision = repo.selected_revision();
            std::string nar_hash = repo.selected_nar_hash();
            if (json) out << "{\"schema_version\":1,\"command\":\"repo list\",\"repositories\":["
                          << (revision.empty() ? "" : "{\"name\":\"nixpkgs\",\"revision\":\"" + json_escape(revision) + "\",\"nar_hash\":\"" + json_escape(nar_hash) + "\"}") << "]}\n";
            else out << (revision.empty() ? "No repository revision is indexed\n" : "nixpkgs " + revision + " " + nar_hash + "\n");
            return 0;
        }
        if ((args.size() == 2 && args[0] == "repo" && args[1] == "update") || (args.size() == 1 && args[0] == "update")) {
            size_t count = repo.update(offline);
            const std::string command = args[0] == "update" ? "update" : "repo update";
            if (json) out << "{\"schema_version\":1,\"command\":\"" << command << "\",\"revision\":\""
                                << json_escape(repo.selected_revision()) << "\",\"nar_hash\":\"" << json_escape(repo.selected_nar_hash()) << "\",\"indexed_packages\":" << count
                                << ",\"profiles_changed\":false}\n";
            else out << "Indexed " << count << " packages from nixpkgs " << repo.selected_revision()
                           << "; installed profiles unchanged\n";
            return 0;
        }
        if (args.size() == 2 && args[0] == "search") {
            auto matches = repo.search(args[1]);
            if (json) {
                out << "{\"schema_version\":1,\"command\":\"search\",\"packages\":[";
                for (size_t i = 0; i < matches.size(); ++i) {
                    const auto& p = matches[i];
                    if (i) out << ',';
                    out << "{\"attribute\":\"" << json_escape(p.attribute) << "\",\"name\":\"" << json_escape(p.name)
                              << "\",\"version\":\"" << json_escape(p.version) << "\",\"description\":\""
                              << json_escape(p.description) << "\",\"revision\":\"" << json_escape(p.revision) << "\"}";
                }
                out << "]}\n";
            } else {
                for (const auto& p : matches) out << p.attribute << " " << p.version << " — " << p.description << '\n';
                if (matches.empty()) out << "No indexed packages matched. Run 'astra repo update' if the catalog is empty.\n";
            }
            return 0;
        }
        if (args.size() == 2 && args[0] == "info") {
            auto p = repo.info(args[1]);
            if (json) out << "{\"schema_version\":1,\"command\":\"info\",\"package\":{\"attribute\":\""
                                << json_escape(p.attribute) << "\",\"name\":\"" << json_escape(p.name) << "\",\"version\":\""
                                << json_escape(p.version) << "\",\"description\":\"" << json_escape(p.description)
                                << "\",\"license\":null,\"availability\":\"unverified\",\"system\":\""
                                << json_escape(p.system) << "\",\"revision\":\"" << json_escape(p.revision) << "\"}}\n";
            else out << p.name << ' ' << p.version << "\nAttribute: " << p.attribute << "\nDescription: " << p.description
                           << "\nLicense: not indexed\nAvailability: unverified\nSystem: " << p.system << "\nRepository revision: " << p.revision << '\n';
            return 0;
        }
        throw std::invalid_argument("command is unavailable in this development build; run 'astra help'");
    } catch (const std::exception& error) {
        std::cerr << "astra: " << error.what() << '\n';
        return 2;
    }
}

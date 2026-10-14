#include "astra/repository.hpp"
#include "astra/backend.hpp"
#include "astra/database.hpp"
#include "astra/process.hpp"

#include <json-c/json.h>
#include <regex>
#include <sqlite3.h>
#include <stdexcept>
#include <memory>

namespace astra {
namespace {
constexpr const char* source_ref = "github:NixOS/nixpkgs/nixos-26.05";
using Json = std::unique_ptr<json_object, decltype(&json_object_put)>;
class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    void bind(int position, const std::string& value) {
        if (sqlite3_bind_text(stmt_, position, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
    }
    void bind(int position, int64_t value) {
        if (sqlite3_bind_int64(stmt_, position, value) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
    }
    bool next() {
        int result = sqlite3_step(stmt_);
        if (result == SQLITE_ROW) return true;
        if (result == SQLITE_DONE) return false;
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    void execute() { if (next()) throw std::runtime_error("unexpected database row"); }
    void reset() { sqlite3_reset(stmt_); sqlite3_clear_bindings(stmt_); }
    std::string text(int column) const {
        const unsigned char* value = sqlite3_column_text(stmt_, column);
        return value ? reinterpret_cast<const char*>(value) : "";
    }
    int64_t integer(int column) const { return sqlite3_column_int64(stmt_, column); }
private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};
std::string field(json_object* object, const char* name) {
    json_object* value = nullptr;
    if (!json_object_object_get_ex(object, name, &value) || !json_object_is_type(value, json_type_string)) return {};
    return json_object_get_string(value);
}
Json parse(const std::string& source) {
    json_tokener* tokener = json_tokener_new();
    if (!tokener) throw std::runtime_error("cannot allocate JSON parser");
    json_object* parsed = json_tokener_parse_ex(tokener, source.c_str(), static_cast<int>(source.size()));
    bool valid = json_tokener_get_error(tokener) == json_tokener_success && parsed && json_object_is_type(parsed, json_type_object);
    json_tokener_free(tokener);
    if (!valid) { if (parsed) json_object_put(parsed); throw std::runtime_error("invalid Nix JSON output"); }
    return Json(parsed, json_object_put);
}
std::string safe_error(const ProcessResult& result, const std::string& fallback) {
    if (result.timed_out) return fallback + ": timed out";
    if (result.truncated) return fallback + ": output exceeded size limit";
    if (result.status == 127) return fallback + ": Nix executable missing";
    return fallback + (result.error.empty() ? "" : ": " + result.error.substr(0, 500));
}
PackageRecord row(Statement& statement) {
    return {statement.text(0), statement.text(1), statement.text(2), statement.text(3), statement.text(4), statement.text(5), statement.text(6)};
}
}

RepositoryManager::RepositoryManager(Database& database) : database_(database) {}
std::string RepositoryManager::selected_revision() const {
    Statement query(database_.handle(), "SELECT selected_revision FROM repositories WHERE name='nixpkgs';");
    return query.next() ? query.text(0) : "";
}

size_t RepositoryManager::update(bool offline) {
    if (offline) throw std::runtime_error("repository update needs network access and is unavailable with --offline");
    BackendStatus backend = detect_backend();
    if (!backend.version_supported) throw std::runtime_error("supported Nix backend required: " + backend.detail);
    const std::vector<std::string> base{"nix", "--extra-experimental-features", "nix-command flakes"};
    auto metadata_args = base;
    metadata_args.insert(metadata_args.end(), {"flake", "metadata", "--json", source_ref});
    ProcessResult metadata = run_process(metadata_args, 120, 4 * 1024 * 1024);
    if (metadata.status != 0 || metadata.truncated || metadata.timed_out)
        throw std::runtime_error(safe_error(metadata, "repository revision lookup failed"));
    Json metadata_json = parse(metadata.output);
    json_object* locked = nullptr;
    if (!json_object_object_get_ex(metadata_json.get(), "locked", &locked) || !json_object_is_type(locked, json_type_object)
        || field(locked, "type") != "github" || field(locked, "owner") != "NixOS" || field(locked, "repo") != "nixpkgs")
        throw std::runtime_error("Nix returned an unexpected repository identity");
    std::string revision = field(locked, "rev");
    if (!std::regex_match(revision, std::regex("[0-9a-f]{40}")) ||
        !std::regex_match(field(locked, "narHash"), std::regex("sha256-[A-Za-z0-9+/]{43}=")))
        throw std::runtime_error("Nix returned a repository without a verified immutable revision and content hash");

    auto search_args = base;
    search_args.insert(search_args.end(), {"search", "--json", "github:NixOS/nixpkgs/" + revision, "^"});
    ProcessResult catalog = run_process(search_args, 600, 64 * 1024 * 1024);
    if (catalog.status != 0 || catalog.truncated || catalog.timed_out)
        throw std::runtime_error(safe_error(catalog, "repository catalog evaluation failed"));
    Json packages = parse(catalog.output);
    std::vector<PackageRecord> records;
    json_object_object_foreach(packages.get(), key, value) {
        if (!json_object_is_type(value, json_type_object)) continue;
        std::string attribute(key);
        std::smatch match;
        if (!std::regex_match(attribute, match, std::regex(R"((packages|legacyPackages)\.([a-zA-Z0-9_+-]+)\.([a-zA-Z0-9_+.-]+))"))) continue;
        std::string name = field(value, "pname");
        if (name.empty()) name = match[3];
        records.push_back({attribute, name, field(value, "version"), field(value, "description"), "", match[2], revision});
    }
    if (records.empty()) throw std::runtime_error("Nix catalog contained no usable packages");

    sqlite3* db = database_.handle();
    char* error = nullptr;
    if (sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : "cannot begin repository update"; sqlite3_free(error); throw std::runtime_error(message);
    }
    try {
        Statement repository(db, "INSERT INTO repositories(name,url,selected_revision) VALUES('nixpkgs',?,NULL) ON CONFLICT(name) DO NOTHING;");
        repository.bind(1, source_ref); repository.execute();
        Statement repository_id(db, "SELECT id FROM repositories WHERE name='nixpkgs';");
        if (!repository_id.next()) throw std::runtime_error("repository record missing");
        int64_t rid = repository_id.integer(0);
        Statement revision_insert(db, "INSERT OR IGNORE INTO repository_revisions(repository_id,revision) VALUES(?,?);");
        revision_insert.bind(1, rid); revision_insert.bind(2, revision); revision_insert.execute();
        Statement revision_id(db, "SELECT id FROM repository_revisions WHERE repository_id=? AND revision=?;");
        revision_id.bind(1, rid); revision_id.bind(2, revision);
        if (!revision_id.next()) throw std::runtime_error("repository revision record missing");
        int64_t revision_key = revision_id.integer(0);
        Statement insert(db, "INSERT INTO packages(repository_revision_id,attribute,name,version,description,license,system) VALUES(?,?,?,?,?,?,?) ON CONFLICT(repository_revision_id,attribute,system) DO UPDATE SET name=excluded.name,version=excluded.version,description=excluded.description,license=excluded.license;");
        for (const auto& record : records) {
            insert.bind(1, revision_key); insert.bind(2, record.attribute); insert.bind(3, record.name);
            insert.bind(4, record.version); insert.bind(5, record.description); insert.bind(6, record.license);
            insert.bind(7, record.system); insert.execute(); insert.reset();
        }
        Statement select(db, "UPDATE repositories SET selected_revision=?,url=? WHERE id=?;");
        select.bind(1, revision); select.bind(2, source_ref); select.bind(3, rid); select.execute();
        if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &error) != SQLITE_OK) {
            std::string message = error ? error : "cannot commit repository update"; sqlite3_free(error); throw std::runtime_error(message);
        }
        return records.size();
    } catch (...) { sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr); throw; }
}

std::vector<PackageRecord> RepositoryManager::search(const std::string& query) const {
    if (query.empty() || query.size() > 128) throw std::invalid_argument("search query must contain 1 to 128 characters");
    std::string pattern = "%";
    for (char c : query) { if (c == '%' || c == '_' || c == '\\') pattern += '\\'; pattern += c; }
    pattern += '%';
    Statement statement(database_.handle(), "SELECT p.attribute,p.name,p.version,coalesce(p.description,''),coalesce(p.license,''),p.system,r.revision FROM packages p JOIN repository_revisions r ON r.id=p.repository_revision_id JOIN repositories repo ON repo.id=r.repository_id WHERE repo.name='nixpkgs' AND r.revision=repo.selected_revision AND (p.name LIKE ? ESCAPE '\\' OR p.attribute LIKE ? ESCAPE '\\' OR p.description LIKE ? ESCAPE '\\') ORDER BY p.name,p.attribute LIMIT 50;");
    statement.bind(1, pattern); statement.bind(2, pattern); statement.bind(3, pattern);
    std::vector<PackageRecord> records;
    while (statement.next()) records.push_back(row(statement));
    return records;
}
PackageRecord RepositoryManager::info(const std::string& identifier) const {
    if (identifier.empty() || identifier.size() > 256) throw std::invalid_argument("invalid package identifier");
    Statement statement(database_.handle(), "SELECT p.attribute,p.name,p.version,coalesce(p.description,''),coalesce(p.license,''),p.system,r.revision FROM packages p JOIN repository_revisions r ON r.id=p.repository_revision_id JOIN repositories repo ON repo.id=r.repository_id WHERE repo.name='nixpkgs' AND r.revision=repo.selected_revision AND (p.attribute=? OR p.name=?) LIMIT 2;");
    statement.bind(1, identifier); statement.bind(2, identifier);
    if (!statement.next()) throw std::runtime_error("package not found in the indexed revision");
    PackageRecord record = row(statement);
    if (statement.next()) throw std::runtime_error("package name is ambiguous; use the full attribute identifier");
    return record;
}
PackageRecord RepositoryManager::info_for_system(const std::string& identifier, const std::string& system) const {
    if (identifier.empty() || identifier.size() > 256 || system.empty()) throw std::invalid_argument("invalid package identifier or system");
    Statement statement(database_.handle(), "SELECT p.attribute,p.name,p.version,coalesce(p.description,''),coalesce(p.license,''),p.system,r.revision FROM packages p JOIN repository_revisions r ON r.id=p.repository_revision_id JOIN repositories repo ON repo.id=r.repository_id WHERE repo.name='nixpkgs' AND r.revision=repo.selected_revision AND p.system=? AND (p.attribute=? OR p.name=?) LIMIT 2;");
    statement.bind(1, system); statement.bind(2, identifier); statement.bind(3, identifier);
    if (!statement.next()) throw std::runtime_error("package is unavailable for this Nix host in the selected revision");
    PackageRecord record = row(statement);
    if (statement.next()) throw std::runtime_error("package name is ambiguous for this Nix host; use the full attribute identifier");
    return record;
}
}

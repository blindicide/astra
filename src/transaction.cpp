#include "astra/transaction.hpp"
#include "astra/backend.hpp"
#include "astra/database.hpp"
#include "astra/process.hpp"
#include "astra/repository.hpp"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json-c/json.h>
#include <memory>
#include <openssl/sha.h>
#include <regex>
#include <sqlite3.h>
#include <stdexcept>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
namespace astra {
namespace {
using Json = std::unique_ptr<json_object, decltype(&json_object_put)>;
class Query {
public:
    Query(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Query() { sqlite3_finalize(stmt_); }
    void bind(int i, const std::string& s) {
        if (sqlite3_bind_text(stmt_, i, s.c_str(), static_cast<int>(s.size()), SQLITE_TRANSIENT) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
    }
    void bind(int i, long long n) {
        if (sqlite3_bind_int64(stmt_, i, n) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_));
    }
    bool next() {
        int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    void done() { if (next()) throw std::runtime_error("unexpected database result"); }
    std::string text(int i) const {
        auto* p = sqlite3_column_text(stmt_, i);
        return p ? reinterpret_cast<const char*>(p) : "";
    }
    long long number(int i) const { return sqlite3_column_int64(stmt_, i); }
private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};
void sql(sqlite3* db, const char* command) {
    char* error = nullptr;
    if (sqlite3_exec(db, command, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : "SQLite error";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}
class Lock {
public:
    explicit Lock(const fs::path& path) {
        fd_ = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd_ < 0) throw std::runtime_error("cannot open profile lock");
        struct stat st{};
        if (fstat(fd_, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077))
            throw std::runtime_error("unsafe profile lock");
        if (flock(fd_, LOCK_EX | LOCK_NB) != 0) throw std::runtime_error("profile is busy");
    }
    ~Lock() { if (fd_ >= 0) close(fd_); }
private:
    int fd_ = -1;
};
void private_dir(const fs::path& path) {
    if (mkdir(path.c_str(), 0700) != 0 && errno != EEXIST) throw std::runtime_error("cannot create private profile directory");
    struct stat st{};
    if (lstat(path.c_str(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077))
        throw std::runtime_error("unsafe profile directory");
}
void sync_dir(const fs::path& path) {
    int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || fsync(fd) != 0) { if (fd >= 0) close(fd); throw std::runtime_error("cannot synchronize journal directory"); }
    close(fd);
}
void durable_file(const fs::path& path, const std::string& value) {
    fs::path temp = path;
    temp += ".tmp";
    int fd = open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) throw std::runtime_error("cannot create transaction journal");
    size_t offset = 0;
    while (offset < value.size()) {
        ssize_t n = write(fd, value.data() + offset, value.size() - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); throw std::runtime_error("cannot write transaction journal"); }
        offset += static_cast<size_t>(n);
    }
    if (fsync(fd) != 0) { close(fd); throw std::runtime_error("cannot synchronize transaction journal"); }
    close(fd);
    if (rename(temp.c_str(), path.c_str()) != 0) throw std::runtime_error("cannot publish transaction journal");
    sync_dir(path.parent_path());
}
std::string hex(const unsigned char* bytes, size_t count) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (size_t i = 0; i < count; ++i) { result += digits[bytes[i] >> 4]; result += digits[bytes[i] & 15]; }
    return result;
}
std::string hash_of(const std::string& value) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest);
    return hex(digest, sizeof(digest));
}
std::string nonce() {
    unsigned char bytes[16];
    if (getrandom(bytes, sizeof(bytes), 0) != static_cast<ssize_t>(sizeof(bytes))) throw std::runtime_error("cannot obtain transaction nonce");
    return hex(bytes, sizeof(bytes));
}
void add(json_object* object, const char* key, const std::string& value) {
    json_object_object_add(object, key, json_object_new_string_len(value.data(), static_cast<int>(value.size())));
}
void add(json_object* object, const char* key, long long value) {
    json_object_object_add(object, key, json_object_new_int64(value));
}
std::string store_target(const fs::path& path) {
    std::error_code error;
    if (!fs::is_symlink(path, error)) return {};
    fs::path target = path;
    for (int i = 0; i < 8; ++i) {
        struct stat st{};
        if (lstat(target.c_str(), &st) != 0) {
            if (errno == ENOENT) break;
            throw std::runtime_error("cannot inspect profile link");
        }
        if (!S_ISLNK(st.st_mode)) break;
        fs::path next = fs::read_symlink(target, error);
        if (error) throw std::runtime_error("cannot inspect profile link");
        target = next.is_absolute() ? next : (target.parent_path() / next).lexically_normal();
    }
    if (error || !std::regex_match(target.string(), std::regex(R"(/nix/store/[a-z0-9]{32}-[A-Za-z0-9+._?=-]+)")))
        throw std::runtime_error("profile link does not resolve to a valid Nix store path");
    return target.string();
}
void checked_process(const std::vector<std::string>& args, int seconds = 600) {
    ProcessResult result = run_process(args, seconds, 16 * 1024 * 1024);
    if (result.status != 0 || result.timed_out || result.truncated)
        throw std::runtime_error("Nix operation failed: " + result.error.substr(0, 400));
}
void verify_store(const std::string& path) {
    checked_process({"nix-store", "--verify-path", path}, 30);
}
void fault(const char* boundary) {
    const char* requested = std::getenv("ASTRA_TEST_FAULT_POINT");
    if (requested && std::string(requested) == boundary) _exit(77);
}
std::string display_id(sqlite3* db, const std::string& hash) {
    for (int width = 8; width <= 64; width += 4) {
        std::string suffix = hash.substr(hash.size() - static_cast<size_t>(width));
        Query q(db, "SELECT count(*) FROM transactions WHERE substr(hash,-?)=?;");
        q.bind(1, static_cast<long long>(width)); q.bind(2, suffix); q.next();
        if (q.number(0) <= 1) return suffix;
    }
    return hash;
}
long long profile_id(sqlite3* db, const std::string& name) {
    Query insert(db, "INSERT OR IGNORE INTO profiles(name,scope) VALUES(?,'user');");
    insert.bind(1, name); insert.done();
    Query select(db, "SELECT id FROM profiles WHERE name=? AND scope='user';");
    select.bind(1, name);
    if (!select.next()) throw std::runtime_error("profile scope mismatch");
    return select.number(0);
}
std::vector<Selection> current_selections(sqlite3* db, long long pid) {
    Query q(db, "SELECT s.package_id,p.attribute,r.revision,s.output_path FROM generations g JOIN package_selections s ON s.generation_id=g.id JOIN packages p ON p.id=s.package_id JOIN repository_revisions r ON r.id=p.repository_revision_id WHERE g.profile_id=? AND g.active=1 ORDER BY p.attribute;");
    q.bind(1, pid);
    std::vector<Selection> selections;
    while (q.next()) selections.push_back({q.number(0), q.text(1), q.text(2), q.text(3)});
    return selections;
}
long long package_id(sqlite3* db, const PackageRecord& p) {
    Query q(db, "SELECT p.id FROM packages p JOIN repository_revisions r ON r.id=p.repository_revision_id WHERE p.attribute=? AND p.system=? AND r.revision=?;");
    q.bind(1, p.attribute); q.bind(2, p.system); q.bind(3, p.revision);
    if (!q.next()) throw std::runtime_error("resolved package left repository index");
    return q.number(0);
}
std::string read_journal(const fs::path& path) {
    struct stat st{};
    if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077) || st.st_size > 1024 * 1024)
        throw std::runtime_error("transaction journal is missing or unsafe");
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}
}

TransactionEngine::TransactionEngine(Database& db, RepositoryManager& repo, std::string profile)
    : db_(db), repo_(repo), profile_(std::move(profile)), directory_(db.path().parent_path() / "profiles" / profile_) {
    if (!std::regex_match(profile_, std::regex("[A-Za-z][A-Za-z0-9_-]{0,31}"))) throw std::invalid_argument("invalid profile name");
    private_dir(db.path().parent_path() / "profiles");
    private_dir(directory_);
    private_dir(directory_ / "staged");
    private_dir(directory_ / "roots");
    private_dir(directory_ / "journals");
}

void TransactionEngine::recover() {
    Lock lock(directory_ / "operation.lock");
    recover_locked();
}

void TransactionEngine::recover_locked() {
    sqlite3* db = db_.handle();
    long long pid = profile_id(db, profile_);
    std::string active = store_target(directory_ / "profile");
    Query pending(db, "SELECT t.sequence,t.hash,t.intent,t.status,g.store_path FROM transactions t JOIN generations g ON g.id=t.target_generation WHERE t.profile_id=? AND t.status='prepared' ORDER BY t.sequence;");
    pending.bind(1, pid);
    while (pending.next()) {
        long long seq = pending.number(0);
        std::string hash = pending.text(1), intent = pending.text(2), target = pending.text(4);
        fs::path journal = directory_ / "journals" / (hash + ".json");
        if (hash_of(intent) != hash)
            throw std::runtime_error("transaction intent or journal is corrupt; operator intervention required");
        if (fs::exists(journal)) {
            if (read_journal(journal) != intent)
                throw std::runtime_error("transaction journal differs from immutable intent; operator intervention required");
        } else if (active != target) durable_file(journal, intent);
        else throw std::runtime_error("activated transaction has no journal; operator intervention required");
        Query prev(db, "SELECT g.store_path FROM transactions t LEFT JOIN generations g ON g.id=t.previous_generation WHERE t.sequence=?;");
        prev.bind(1, seq); prev.next();
        std::string previous = prev.text(0);
        verify_store(target);
        if (active == target) {
            sql(db, "BEGIN IMMEDIATE;");
            try {
                Query clear(db, "UPDATE generations SET active=0 WHERE profile_id=?;"); clear.bind(1, pid); clear.done();
                Query activate(db, "UPDATE generations SET active=1 WHERE id=(SELECT target_generation FROM transactions WHERE sequence=?);"); activate.bind(1, seq); activate.done();
                Query commit(db, "UPDATE transactions SET status='committed' WHERE sequence=?;"); commit.bind(1, seq); commit.done();
                Query event(db, "INSERT INTO transaction_events(transaction_sequence,event) VALUES(?,'recovered_committed');"); event.bind(1, seq); event.done();
                sql(db, "COMMIT;");
            } catch (...) { sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr); throw; }
        } else if (active == previous) {
            Query abort(db, "UPDATE transactions SET status='not_applied' WHERE sequence=?;"); abort.bind(1, seq); abort.done();
            Query event(db, "INSERT INTO transaction_events(transaction_sequence,event) VALUES(?,'recovered_not_applied');"); event.bind(1, seq); event.done();
        } else throw std::runtime_error("active profile differs from both prepared and previous generations; operator intervention required");
    }
    Query expected(db, "SELECT store_path FROM generations WHERE profile_id=? AND active=1;");
    expected.bind(1, pid);
    std::string recorded = expected.next() ? expected.text(0) : "";
    if (active != recorded) throw std::runtime_error("active profile and database disagree; operator intervention required");
    if (!active.empty()) verify_store(active);
    Query audit(db, "SELECT t.hash,t.intent,t.status,g.store_path,r.root_path FROM transactions t JOIN generations g ON g.id=t.target_generation LEFT JOIN store_references r ON r.generation_id=g.id AND r.store_path=g.store_path WHERE t.profile_id=?;");
    audit.bind(1, pid);
    while (audit.next()) {
        std::string hash = audit.text(0), intent = audit.text(1), status = audit.text(2);
        if (hash_of(intent) != hash) throw std::runtime_error("transaction hash mismatch; operator intervention required");
        fs::path journal = directory_ / "journals" / (hash + ".json");
        if (fs::exists(journal) && read_journal(journal) != intent)
            throw std::runtime_error("transaction journal mismatch; operator intervention required");
        if (status == "committed") {
            if (!fs::exists(journal)) throw std::runtime_error("committed transaction has no journal; operator intervention required");
            if (audit.text(4).empty() || store_target(audit.text(4)) != audit.text(3))
                throw std::runtime_error("retained generation GC root is missing; operator intervention required");
            verify_store(audit.text(3));
        }
    }
}

std::string TransactionEngine::mutate(const std::string& operation, const std::vector<std::string>& names,
                                      bool yes, bool dry_run, bool offline) {
    if (offline && operation != "rollback") throw std::invalid_argument("offline profile mutation is not yet supported");
    if (operation != "rollback" && names.empty()) throw std::invalid_argument("at least one package is required");
    if (operation != "install" && operation != "remove" && operation != "rollback") throw std::invalid_argument("unsupported profile operation");
    if (operation == "rollback" && names.size() > 1) throw std::invalid_argument("rollback accepts at most one transaction ID");
    Lock lock(directory_ / "operation.lock");
    recover_locked();
    if (!detect_backend().version_supported) throw std::runtime_error("a supported Nix backend is required for profile changes");
    ProcessResult system_result = run_process({"nix", "--extra-experimental-features", "nix-command flakes", "eval", "--raw", "--impure", "--expr", "builtins.currentSystem"}, 30);
    if (system_result.status != 0 || system_result.timed_out || system_result.truncated)
        throw std::runtime_error("cannot determine Nix host system");
    std::string system = system_result.output;
    if (!system.empty() && system.back() == '\n') system.pop_back();
    if (!std::regex_match(system, std::regex("[A-Za-z0-9_+-]+"))) throw std::runtime_error("invalid Nix host system");
    sqlite3* db = db_.handle();
    long long pid = profile_id(db, profile_);
    std::string selected_revision = repo_.selected_revision();
    std::vector<Selection> selections = current_selections(db, pid);
    std::string restored_store;
    long long restored_generation = 0;
    if (operation == "rollback") {
        if (names.empty()) {
            Query previous(db, "SELECT t.previous_generation,g.store_path FROM transactions t JOIN generations g ON g.id=t.previous_generation WHERE t.profile_id=? AND t.status='committed' ORDER BY t.sequence DESC LIMIT 1;");
            previous.bind(1, pid);
            if (!previous.next()) throw std::runtime_error("no previous ASTRA generation is available");
            restored_generation = previous.number(0); restored_store = previous.text(1);
        } else {
            if (!std::regex_match(names[0], std::regex("[0-9a-f]{8,64}"))) throw std::invalid_argument("invalid transaction identifier");
            Query requested(db, "SELECT g.id,g.store_path FROM transactions t JOIN generations g ON g.id=t.target_generation WHERE t.profile_id=? AND t.status='committed' AND substr(t.hash,-length(?))=? LIMIT 2;");
            requested.bind(1, pid); requested.bind(2, names[0]); requested.bind(3, names[0]);
            if (!requested.next()) throw std::runtime_error("transaction ID does not identify a committed generation");
            restored_generation = requested.number(0); restored_store = requested.text(1);
            if (requested.next()) throw std::runtime_error("transaction ID is ambiguous; supply a longer hash suffix");
        }
        Query restored(db, "SELECT s.package_id,p.attribute,r.revision,s.output_path FROM package_selections s JOIN packages p ON p.id=s.package_id JOIN repository_revisions r ON r.id=p.repository_revision_id WHERE s.generation_id=? ORDER BY p.attribute;");
        restored.bind(1, restored_generation);
        selections.clear();
        while (restored.next()) selections.push_back({restored.number(0), restored.text(1), restored.text(2), restored.text(3)});
        verify_store(restored_store);
    }
    for (const std::string& name : (operation == "rollback" ? std::vector<std::string>{} : names)) {
        if (name.empty() || name.size() > 256 || name[0] == '-') throw std::invalid_argument("invalid package identifier");
        auto found = std::find_if(selections.begin(), selections.end(), [&](const Selection& s) { return s.attribute == name || s.attribute.substr(s.attribute.rfind('.') + 1) == name; });
        if (operation == "install") {
            PackageRecord p = repo_.info(name);
            if (p.system != system) throw std::runtime_error("package is indexed for " + p.system + ", not this Nix host " + system);
            Selection selected{package_id(db, p), p.attribute, p.revision, ""};
            if (found == selections.end()) selections.push_back(selected);
            else *found = selected;
        } else {
            if (found == selections.end()) throw std::runtime_error("package is not installed in this ASTRA profile: " + name);
            selections.erase(found);
        }
    }
    std::sort(selections.begin(), selections.end(), [](const Selection& a, const Selection& b) { return a.attribute < b.attribute; });
    for (size_t i = 1; i < selections.size(); ++i)
        if (selections[i - 1].attribute == selections[i].attribute) throw std::runtime_error("duplicate package selection");
    if (dry_run) {
        std::string plan = "Profile " + profile_ + " would contain:";
        for (const auto& s : selections) plan += "\n  " + s.attribute + " @ " + s.revision;
        return plan + "\nNo package was built or activated.";
    }
    std::vector<std::string> preview{"nix", "--extra-experimental-features", "nix-command flakes", "build", "--dry-run", "--no-link"};
    for (const auto& s : selections) if (s.output.empty())
        preview.push_back("github:NixOS/nixpkgs/" + s.revision + "#" + s.attribute.substr(s.attribute.rfind('.') + 1));
    if (selections.empty() && operation != "rollback") preview.push_back("github:NixOS/nixpkgs/" + selected_revision + "#emptyDirectory");
    if (preview.size() > 6) {
        ProcessResult preflight = run_process(preview, 120, 4 * 1024 * 1024);
        if (preflight.status != 0 || preflight.timed_out || preflight.truncated)
            throw std::runtime_error("Nix build preview failed: " + preflight.error.substr(0, 400));
        std::cout << "Nix build preview:\n" << preflight.output << preflight.error;
    }
    std::cout << "Profile " << profile_ << " will contain " << selections.size()
              << " package(s). Nix may fetch substitutes or build locally.\n";
    if (!yes) {
        if (!isatty(STDIN_FILENO)) throw std::runtime_error("confirmation required; pass --yes after reviewing the plan");
        std::cout << "Proceed? [y/N] " << std::flush;
        std::string response;
        std::getline(std::cin, response);
        if (response != "y" && response != "Y") throw std::runtime_error("transaction cancelled");
    }
    std::string revision = selected_revision;
    if (revision.empty()) throw std::runtime_error("repository revision is not selected");
    if (repo_.selected_revision() != revision) throw std::runtime_error("repository revision changed during profile planning; retry");
    if (selections.empty() && operation != "rollback") {
        ProcessResult empty = run_process({"nix", "--extra-experimental-features", "nix-command flakes", "build", "--no-link", "--print-out-paths", "github:NixOS/nixpkgs/" + revision + "#emptyDirectory"}, 600);
        if (empty.status != 0 || empty.timed_out || empty.truncated) throw std::runtime_error("cannot build empty profile seed");
        std::string path = empty.output;
        if (!path.empty() && path.back() == '\n') path.pop_back();
        selections.push_back({0, "__empty_profile_seed", revision, path});
    } else if (operation != "rollback") {
        for (auto& s : selections) {
            if (!s.output.empty()) continue;
            std::string attr = s.attribute.substr(s.attribute.rfind('.') + 1);
            ProcessResult built = run_process({"nix", "--extra-experimental-features", "nix-command flakes", "build", "--no-link", "--print-out-paths", "github:NixOS/nixpkgs/" + s.revision + "#" + attr}, 3600);
            if (built.status != 0 || built.timed_out || built.truncated) throw std::runtime_error("Nix build or substitute failed for " + s.attribute + ": " + built.error.substr(0, 400));
            s.output = built.output;
            if (!s.output.empty() && s.output.back() == '\n') s.output.pop_back();
            if (!std::regex_match(s.output, std::regex(R"(/nix/store/[a-z0-9]{32}-[A-Za-z0-9+._?=-]+)"))) throw std::runtime_error("Nix returned an invalid package output path");
        }
    }
    fault("after_build");
    if (repo_.selected_revision() != revision) throw std::runtime_error("repository revision changed during profile build; retry");
    std::string transaction_nonce = nonce();
    std::string target = restored_store;
    if (operation != "rollback") {
        fs::path stage = directory_ / "staged" / transaction_nonce;
        std::vector<std::string> install{"nix", "--extra-experimental-features", "nix-command flakes", "profile", "install", "--profile", stage.string()};
        for (const auto& s : selections) install.push_back(s.output);
        checked_process(install);
        target = store_target(stage);
    }
    if (target.empty()) throw std::runtime_error("staged Nix profile was not created");
    verify_store(target);
    fault("after_staging");
    Query seq_query(db, "SELECT coalesce(max(sequence),0)+1 FROM transactions;"); seq_query.next();
    long long sequence = seq_query.number(0);
    Query prior(db, "SELECT t.hash,g.id,g.store_path FROM transactions t JOIN generations g ON g.id=t.target_generation WHERE t.profile_id=? AND t.status='committed' ORDER BY t.sequence DESC LIMIT 1;");
    prior.bind(1, pid);
    std::string previous_hash, previous_store;
    long long previous_generation = 0;
    if (prior.next()) { previous_hash = prior.text(0); previous_generation = prior.number(1); previous_store = prior.text(2); }
    Json intent(json_object_new_object(), json_object_put);
    add(intent.get(), "schema_version", 1LL); add(intent.get(), "nonce", transaction_nonce); add(intent.get(), "sequence", sequence);
    add(intent.get(), "previous_hash", previous_hash); add(intent.get(), "operation", operation); add(intent.get(), "profile", profile_);
    add(intent.get(), "previous_store", previous_store); add(intent.get(), "repository_revision", revision);
    add(intent.get(), "target_store", target);
    add(intent.get(), "restored_generation", restored_generation);
    json_object* array = json_object_new_array();
    for (const auto& s : selections) {
        json_object* item = json_object_new_object();
        add(item, "attribute", s.attribute); add(item, "revision", s.revision); add(item, "output", s.output);
        json_object_array_add(array, item);
    }
    json_object_object_add(intent.get(), "selections", array);
    std::string canonical = json_object_to_json_string_ext(intent.get(), JSON_C_TO_STRING_PLAIN);
    std::string hash = hash_of(canonical);
    fs::path root = directory_ / "roots" / hash;
    checked_process({"nix-store", "--add-root", root.string(), "--realise", target});
    if (store_target(root) != target) throw std::runtime_error("Nix GC root verification failed");
    verify_store(target);
    fault("after_root");
    fault("before_prepared_db");
    sql(db, "BEGIN IMMEDIATE;");
    try {
        Query gen(db, "INSERT INTO generations(profile_id,store_path,active) VALUES(?,?,0);"); gen.bind(1, pid); gen.bind(2, target); gen.done();
        long long gid = sqlite3_last_insert_rowid(db);
        for (const auto& s : selections) if (s.package_id) {
            Query selection(db, "INSERT INTO package_selections(generation_id,package_id,output_path) VALUES(?,?,?);");
            selection.bind(1, gid); selection.bind(2, s.package_id); selection.bind(3, s.output); selection.done();
        }
        Query reference(db, "INSERT INTO store_references(generation_id,store_path,root_path) VALUES(?,?,?);");
        reference.bind(1, gid); reference.bind(2, target); reference.bind(3, root.string()); reference.done();
        Query tx(db, "INSERT INTO transactions(sequence,hash,intent,profile_id,status,previous_generation,target_generation) VALUES(?,?,?,?,'prepared',?,?);");
        tx.bind(1, sequence); tx.bind(2, hash); tx.bind(3, canonical); tx.bind(4, pid);
        if (previous_generation) tx.bind(5, previous_generation);
        tx.bind(6, gid); tx.done();
        Query event(db, "INSERT INTO transaction_events(transaction_sequence,event) VALUES(?,'prepared');"); event.bind(1, sequence); event.done();
        sql(db, "COMMIT;");
    } catch (...) { sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr); throw; }
    fault("after_prepared_db");
    fault("before_journal");
    durable_file(directory_ / "journals" / (hash + ".json"), canonical);
    fault("after_journal");
    fault("before_activation");
    checked_process({"nix-env", "--profile", (directory_ / "profile").string(), "--set", target});
    fault("after_activation");
    if (store_target(directory_ / "profile") != target) throw std::runtime_error("active profile verification failed; recovery required");
    verify_store(target);
    fault("after_verification");
    fault("before_commit");
    recover_locked();
    fault("after_commit");
    return display_id(db, hash);
}

std::string TransactionEngine::list() const {
    sqlite3* db = db_.handle();
    Query q(db, "SELECT p.attribute,p.name,p.version,s.output_path FROM profiles pr JOIN generations g ON g.profile_id=pr.id AND g.active=1 JOIN package_selections s ON s.generation_id=g.id JOIN packages p ON p.id=s.package_id WHERE pr.name=? ORDER BY p.attribute;");
    q.bind(1, profile_);
    std::string result;
    while (q.next()) result += q.text(0) + " " + q.text(2) + " " + q.text(3) + "\n";
    return result.empty() ? "No ASTRA-managed packages installed\n" : result;
}

std::string TransactionEngine::history() const {
    Query q(db_.handle(), "SELECT t.hash,t.status,t.sequence,g.store_path FROM transactions t JOIN profiles p ON p.id=t.profile_id JOIN generations g ON g.id=t.target_generation WHERE p.name=? ORDER BY t.sequence DESC;");
    q.bind(1, profile_);
    std::string result;
    while (q.next()) result += display_id(db_.handle(), q.text(0)) + " " + q.text(1) + " seq=" + std::to_string(q.number(2)) + " " + q.text(3) + "\n";
    return result.empty() ? "No ASTRA transactions\n" : result;
}
}

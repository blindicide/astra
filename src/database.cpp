#include "astra/database.hpp"

#include <cstdlib>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include "sqlite3.h"

namespace fs = std::filesystem;
namespace astra {
static void sync_directory(const fs::path& directory) {
    int fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0 || fsync(fd) != 0) {
        if (fd >= 0) close(fd);
        throw std::runtime_error("cannot synchronize ASTRA state directory");
    }
    close(fd);
}

static void verify_migration_backup(const fs::path& backup) {
    sqlite3* source = nullptr;
    if (sqlite3_open_v2(backup.c_str(), &source, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOFOLLOW, nullptr) != SQLITE_OK) {
        if (source) sqlite3_close(source);
        throw std::runtime_error("cannot open database migration backup");
    }
    sqlite3_stmt* check = nullptr;
    bool valid = sqlite3_prepare_v2(source, "PRAGMA integrity_check;", -1, &check, nullptr) == SQLITE_OK &&
                 sqlite3_step(check) == SQLITE_ROW &&
                 std::string(reinterpret_cast<const char*>(sqlite3_column_text(check, 0))) == "ok";
    sqlite3_finalize(check);
    check = nullptr;
    valid = valid && sqlite3_prepare_v2(source, "SELECT max(version) FROM schema_versions;", -1, &check, nullptr) == SQLITE_OK &&
            sqlite3_step(check) == SQLITE_ROW && sqlite3_column_int(check, 0) == 1;
    sqlite3_finalize(check);
    sqlite3_close(source);
    if (!valid) throw std::runtime_error("database migration backup is incomplete or corrupt; operator intervention required");
}

static fs::path checked_base() {
    const char* xdg = std::getenv("XDG_STATE_HOME");
    if (xdg && *xdg) {
        fs::path p(xdg);
        if (!p.is_absolute()) throw std::runtime_error("XDG_STATE_HOME must be absolute");
        return p;
    }
    const char* home = std::getenv("HOME");
    if (!home || !*home) throw std::runtime_error("HOME is not set");
    fs::path p(home);
    if (!p.is_absolute()) throw std::runtime_error("HOME must be absolute");
    return p / ".local" / "state";
}

fs::path state_directory() {
    fs::path path = checked_base() / "astra";
    struct stat entry{};
    if (lstat(path.c_str(), &entry) == 0) {
        if (!S_ISDIR(entry.st_mode) || entry.st_uid != geteuid() || (entry.st_mode & 0077))
            throw std::runtime_error("ASTRA state directory has unsafe ownership or permissions");
    } else if (errno == ENOENT) {
        fs::create_directories(path.parent_path());
        if (mkdir(path.c_str(), 0700) != 0) throw std::runtime_error("cannot create ASTRA state directory");
    } else throw std::runtime_error("cannot inspect ASTRA state directory");
    return path;
}

Database::Database(const fs::path& path) : path_(path) {
    struct stat entry{};
    if (lstat(path.c_str(), &entry) == 0) {
        if (!S_ISREG(entry.st_mode) || entry.st_uid != geteuid() || (entry.st_mode & 0077))
            throw std::runtime_error("ASTRA database has unsafe ownership or permissions");
    } else if (errno == ENOENT) {
        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) throw std::runtime_error("cannot create ASTRA database securely");
        close(fd);
    } else throw std::runtime_error("cannot inspect ASTRA database");
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX | SQLITE_OPEN_NOFOLLOW, nullptr) != SQLITE_OK) {
        std::string message = db_ ? sqlite3_errmsg(db_) : "cannot open SQLite database";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(message);
    }
    sqlite3_busy_timeout(db_, 5000);
    exec("PRAGMA foreign_keys=ON;");
    exec("PRAGMA journal_mode=DELETE;");
    exec("PRAGMA synchronous=FULL;");
    {
        sqlite3_stmt* check = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT max(version) FROM schema_versions;", -1, &check, nullptr) == SQLITE_OK) {
            int result = sqlite3_step(check);
            int version = result == SQLITE_ROW ? sqlite3_column_int(check, 0) : 0;
            sqlite3_finalize(check);
            if (version > 2) throw std::runtime_error("ASTRA database schema is newer than this executable");
        } else if (check) sqlite3_finalize(check);
    }
    exec("BEGIN IMMEDIATE;");
    try {
        exec("CREATE TABLE IF NOT EXISTS schema_versions(version INTEGER PRIMARY KEY, applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);");
        exec("CREATE TABLE IF NOT EXISTS profiles(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, scope TEXT NOT NULL CHECK(scope IN ('user','system')));");
        exec("CREATE TABLE IF NOT EXISTS repositories(id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, url TEXT NOT NULL, selected_revision TEXT);");
        exec("CREATE TABLE IF NOT EXISTS repository_revisions(id INTEGER PRIMARY KEY, repository_id INTEGER NOT NULL REFERENCES repositories(id), revision TEXT NOT NULL, recorded_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP, UNIQUE(repository_id,revision));");
        exec("CREATE TABLE IF NOT EXISTS packages(id INTEGER PRIMARY KEY, repository_revision_id INTEGER NOT NULL REFERENCES repository_revisions(id), attribute TEXT NOT NULL, name TEXT NOT NULL, version TEXT NOT NULL, description TEXT, license TEXT, system TEXT NOT NULL, UNIQUE(repository_revision_id,attribute,system));");
        exec("CREATE TABLE IF NOT EXISTS generations(id INTEGER PRIMARY KEY, profile_id INTEGER NOT NULL REFERENCES profiles(id), store_path TEXT NOT NULL, active INTEGER NOT NULL DEFAULT 0, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);");
        exec("CREATE TABLE IF NOT EXISTS package_selections(generation_id INTEGER NOT NULL REFERENCES generations(id), package_id INTEGER NOT NULL REFERENCES packages(id), output_path TEXT NOT NULL, PRIMARY KEY(generation_id,package_id));");
        exec("CREATE TABLE IF NOT EXISTS transactions(sequence INTEGER PRIMARY KEY, hash TEXT NOT NULL UNIQUE, intent TEXT NOT NULL, profile_id INTEGER NOT NULL REFERENCES profiles(id), status TEXT NOT NULL, previous_generation INTEGER REFERENCES generations(id), target_generation INTEGER REFERENCES generations(id));");
        exec("CREATE TABLE IF NOT EXISTS transaction_events(id INTEGER PRIMARY KEY, transaction_sequence INTEGER NOT NULL REFERENCES transactions(sequence), event TEXT NOT NULL, recorded_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);");
        exec("CREATE TABLE IF NOT EXISTS store_references(generation_id INTEGER NOT NULL REFERENCES generations(id), store_path TEXT NOT NULL, root_path TEXT NOT NULL, PRIMARY KEY(generation_id,store_path));");
        exec("CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY, value TEXT NOT NULL);");
        exec("INSERT OR IGNORE INTO schema_versions(version) VALUES(1);");
        exec("COMMIT;");
    } catch (...) { sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr); throw; }
    if (schema_version() == 1) {
        fs::path backup = path;
        backup += ".backup-v1";
        struct stat backup_entry{};
        bool existing_backup = false;
        if (lstat(backup.c_str(), &backup_entry) == 0) {
            if (!S_ISREG(backup_entry.st_mode) || backup_entry.st_uid != geteuid() || (backup_entry.st_mode & 0077))
                throw std::runtime_error("ASTRA migration backup path is unsafe");
            existing_backup = true;
        } else if (errno != ENOENT) throw std::runtime_error("cannot inspect database migration backup");
        if (!existing_backup) {
            int fd = open(backup.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
            if (fd < 0) throw std::runtime_error("cannot create database migration backup securely");
            close(fd);
            sqlite3* destination = nullptr;
            if (sqlite3_open_v2(backup.c_str(), &destination, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOFOLLOW, nullptr) != SQLITE_OK) {
                if (destination) sqlite3_close(destination);
                throw std::runtime_error("cannot open database migration backup");
            }
            sqlite3_backup* copy = sqlite3_backup_init(destination, "main", db_, "main");
            if (!copy || sqlite3_backup_step(copy, -1) != SQLITE_DONE) {
                if (copy) sqlite3_backup_finish(copy);
                sqlite3_close(destination);
                throw std::runtime_error("cannot back up database before migration");
            }
            sqlite3_backup_finish(copy);
            sqlite3_close(destination);
            fd = open(backup.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
            if (fd < 0 || fsync(fd) != 0) {
                if (fd >= 0) close(fd);
                throw std::runtime_error("cannot synchronize database migration backup");
            }
            close(fd);
            sync_directory(backup.parent_path());
        }
        verify_migration_backup(backup);
        exec("BEGIN IMMEDIATE;");
        try {
            exec("CREATE TRIGGER immutable_transaction_intent BEFORE UPDATE OF hash,intent,sequence,profile_id ON transactions BEGIN SELECT RAISE(ABORT,'transaction intent is immutable'); END;");
            exec("CREATE TRIGGER immutable_transaction_delete BEFORE DELETE ON transactions BEGIN SELECT RAISE(ABORT,'transaction history is immutable'); END;");
            exec("CREATE UNIQUE INDEX one_active_generation ON generations(profile_id) WHERE active=1;");
            exec("INSERT INTO schema_versions(version) VALUES(2);");
            exec("COMMIT;");
        } catch (...) { sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr); throw; }
    }
}

Database::~Database() { if (db_) sqlite3_close(db_); }
void Database::exec(const char* sql) const {
    char* error = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : "SQLite error";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}
std::string Database::integrity_check() const {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA integrity_check;", -1, &statement, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db_));
    int rc = sqlite3_step(statement);
    std::string result = rc == SQLITE_ROW ? reinterpret_cast<const char*>(sqlite3_column_text(statement, 0)) : "integrity check failed";
    sqlite3_finalize(statement);
    return result;
}
int Database::schema_version() const {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT max(version) FROM schema_versions;", -1, &statement, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db_));
    int rc = sqlite3_step(statement);
    int result = rc == SQLITE_ROW ? sqlite3_column_int(statement, 0) : 0;
    sqlite3_finalize(statement);
    return result;
}
}

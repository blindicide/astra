#pragma once
#include <filesystem>
#include <string>

struct sqlite3;
namespace astra {
class Database {
public:
    explicit Database(const std::filesystem::path& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    std::string integrity_check() const;
    int schema_version() const;
    const std::filesystem::path& path() const { return path_; }
    sqlite3* handle() const { return db_; }
private:
    sqlite3* db_ = nullptr;
    std::filesystem::path path_;
    void exec(const char* sql) const;
};
std::filesystem::path state_directory();
}

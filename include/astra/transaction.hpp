#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace astra {
class Database;
class RepositoryManager;

struct Selection {
    long long package_id = 0;
    std::string attribute;
    std::string revision;
    std::string output;
};

class TransactionEngine {
public:
    TransactionEngine(Database& db, RepositoryManager& repo, std::string profile);
    std::string mutate(const std::string& operation, const std::vector<std::string>& names,
                       bool yes, bool dry_run, bool offline);
    void recover();
    std::string list() const;
    std::string history() const;
    std::string history_show(const std::string& identifier, bool json) const;
    std::string why(const std::string& identifier) const;
private:
    void recover_locked();
    Database& db_;
    RepositoryManager& repo_;
    std::string profile_;
    std::filesystem::path directory_;
};
}

#pragma once
#include <string>
#include <vector>

namespace astra {
class Database;
struct PackageRecord {
    std::string attribute;
    std::string name;
    std::string version;
    std::string description;
    std::string license;
    std::string system;
    std::string revision;
};
class RepositoryManager {
public:
    explicit RepositoryManager(Database& database);
    std::string selected_revision() const;
    std::string selected_nar_hash() const;
    size_t update(bool offline);
    std::vector<PackageRecord> search(const std::string& query) const;
    PackageRecord info(const std::string& identifier) const;
    PackageRecord info_for_system(const std::string& identifier, const std::string& system) const;
private:
    Database& database_;
};
}

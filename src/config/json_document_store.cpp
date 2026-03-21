#include "json_document_store.h"

#include <format>
#include <fstream>

namespace config
{
    std::optional<nlohmann::json> JsonFileDocumentStore::load(const std::filesystem::path& path) const
    {
        if (!std::filesystem::exists(path))
        {
            return std::nullopt;
        }

        std::ifstream input(path);
        if (!input.is_open())
        {
            return std::nullopt;
        }

        nlohmann::json document{};
        input >> document;
        return document;
    }

    void JsonFileDocumentStore::save(const std::filesystem::path& path, const nlohmann::json& document) const
    {
        if (path.has_parent_path())
        {
            std::filesystem::create_directories(path.parent_path());
        }

        std::ofstream output(path, std::ios::trunc);
        if (!output.is_open())
        {
            throw std::runtime_error(std::format("JsonFileDocumentStore::save: failed to open {}", path.string()));
        }

        output << document.dump(2) << '\n';
    }
}

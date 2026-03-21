#pragma once

#include <filesystem>
#include <optional>

#include "third_party/nlohmann/json.hpp"

namespace config
{
    class IJsonDocumentStore
    {
    public:
        virtual ~IJsonDocumentStore() = default;

        [[nodiscard]] virtual std::optional<nlohmann::json> load(const std::filesystem::path& path) const = 0;
        virtual void save(const std::filesystem::path& path, const nlohmann::json& document) const = 0;
    };

    class JsonFileDocumentStore final : public IJsonDocumentStore
    {
    public:
        [[nodiscard]] std::optional<nlohmann::json> load(const std::filesystem::path& path) const override;
        void save(const std::filesystem::path& path, const nlohmann::json& document) const override;
    };
}

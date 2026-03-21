#pragma once

#include "json_document_store.h"
#include "world_gen_config_repository.h"

namespace config
{
    class ConfigService
    {
    public:
        ConfigService();

        [[nodiscard]] WorldGenConfigRepository& world_gen() noexcept;
        [[nodiscard]] const WorldGenConfigRepository& world_gen() const noexcept;

    private:
        JsonFileDocumentStore _documentStore{};
        WorldGenConfigRepository _worldGenRepository;
    };
}

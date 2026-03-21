#pragma once

#include "json_document_store.h"
#include "world/terrain_gen.h"

namespace config
{
    class WorldGenConfigRepository
    {
    public:
        explicit WorldGenConfigRepository(const IJsonDocumentStore& documentStore);

        [[nodiscard]] TerrainGeneratorSettings load_or_default() const;
        void save(const TerrainGeneratorSettings& settings) const;

    private:
        const IJsonDocumentStore& _documentStore;
    };
}

#pragma once

#include "json_document_store.h"
#include "world/world_geometry.h"

namespace config
{
    class WorldGeometryConfigRepository
    {
    public:
        explicit WorldGeometryConfigRepository(const IJsonDocumentStore& documentStore);

        [[nodiscard]] WorldGeometrySettings load_or_default() const;
        void save(const WorldGeometrySettings& settings) const;

    private:
        const IJsonDocumentStore& _documentStore;
    };
}

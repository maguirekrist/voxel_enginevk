#pragma once

#include "json_document_store.h"
#include "settings/game_settings.h"

namespace config
{
    class GameSettingsConfigRepository
    {
    public:
        explicit GameSettingsConfigRepository(const IJsonDocumentStore& documentStore);

        [[nodiscard]] settings::GameSettingsPersistence load_or_default() const;
        void save(const settings::GameSettingsPersistence& settings) const;

    private:
        const IJsonDocumentStore& _documentStore;
    };
}

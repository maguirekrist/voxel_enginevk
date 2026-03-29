#pragma once

#include <memory>
#include <optional>
#include <span>
#include <unordered_map>

#include "ui_draw_commands.h"

namespace ui
{
    class TextSystem
    {
    public:
        struct CachedAtlasKey
        {
            FontFaceId faceId{};
            FontAtlasType atlasType{FontAtlasType::MultiChannelSignedDistanceField};

            auto operator<=>(const CachedAtlasKey&) const = default;
        };

        FontCatalog& font_catalog() noexcept;
        const FontCatalog& font_catalog() const noexcept;
        FontBackendRegistry& backend_registry() noexcept;
        const FontBackendRegistry& backend_registry() const noexcept;

        void register_face(FontFaceDescriptor face);
        void register_family(FontFamily family);
        void register_backend(std::shared_ptr<FontBackend> backend);
        void set_default_atlas_type(FontAtlasType atlasType) noexcept;

        [[nodiscard]] const GeneratedFontAtlas* find_generated_atlas(FontFaceId faceId, FontAtlasType atlasType) const;
        [[nodiscard]] std::optional<PreparedTextCommand> prepare_text_command(const TextCommand& command);

    private:
        struct CachedAtlasRecord
        {
            GeneratedFontAtlas atlas{};
            std::vector<uint32_t> codePoints{};
            std::unordered_map<uint32_t, size_t> glyphsByCodePoint{};
            std::unordered_map<uint32_t, size_t> glyphsByIndex{};
        };

        struct CachedAtlasKeyHash
        {
            size_t operator()(const CachedAtlasKey& key) const noexcept
            {
                const size_t faceHash = std::hash<FontFaceId>{}(key.faceId);
                const size_t atlasHash = std::hash<uint8_t>{}(static_cast<uint8_t>(key.atlasType));
                return faceHash ^ (atlasHash << 1);
            }
        };

        struct PreparedFontContext
        {
            const FontFaceDescriptor* face{nullptr};
            std::shared_ptr<FontBackend> backend{};
        };

        [[nodiscard]] std::optional<PreparedFontContext> resolve_font_context(FontFamilyId familyId) const;
        [[nodiscard]] CachedAtlasRecord* ensure_atlas(const PreparedFontContext& fontContext, std::span<const uint32_t> requiredCodePoints, float requestedPixelHeight);
        static void rebuild_lookup_maps(CachedAtlasRecord& record);
        [[nodiscard]] static std::vector<uint32_t> decode_utf8_codepoints(std::string_view text);
        [[nodiscard]] static std::vector<uint32_t> unique_codepoints(std::string_view text);

        FontCatalog _fontCatalog{};
        FontBackendRegistry _backendRegistry{};
        FontAtlasType _defaultAtlasType{FontAtlasType::MultiChannelSignedDistanceField};
        std::unordered_map<CachedAtlasKey, CachedAtlasRecord, CachedAtlasKeyHash> _atlasCache{};
    };
}

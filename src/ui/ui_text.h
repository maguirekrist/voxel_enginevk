#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui_types.h"

namespace ui
{
    enum class FontRasterizationMode : uint8_t
    {
        Bitmap = 0,
        Vector = 1,
        DistanceField = 2
    };

    enum class FontSourceFormat : uint8_t
    {
        BitmapAtlas = 0,
        TrueType = 1,
        OpenType = 2,
        MultiChannelDistanceFieldAtlas = 3,
        Custom = 4
    };

    struct FontAssetSource
    {
        std::string debugName{};
        std::filesystem::path path{};
        FontSourceFormat format{FontSourceFormat::BitmapAtlas};
    };

    struct GlyphRange
    {
        uint32_t firstCodePoint{0};
        uint32_t lastCodePoint{0};

        [[nodiscard]] bool contains(const uint32_t codePoint) const noexcept
        {
            return codePoint >= firstCodePoint && codePoint <= lastCodePoint;
        }
    };

    struct FontFaceDescriptor
    {
        FontFaceId id{};
        std::string debugName{};
        FontRasterizationMode rasterizationMode{FontRasterizationMode::Bitmap};
        float nominalPixelHeight{16.0f};
        std::vector<FontAssetSource> sources{};
        std::vector<GlyphRange> supportedRanges{};
    };

    struct FontFamily
    {
        FontFamilyId id{};
        std::string debugName{};
        std::vector<FontFaceId> preferredFaces{};
        std::vector<FontFaceId> fallbackFaces{};
    };

    enum class TextDirection : uint8_t
    {
        Auto = 0,
        LeftToRight = 1,
        RightToLeft = 2
    };

    struct TextRun
    {
        std::string text{};
        FontFamilyId familyId{};
        float pixelHeight{16.0f};
        float letterSpacing{0.0f};
        bool pixelSnap{true};
        std::string languageTag{};
        TextDirection direction{TextDirection::Auto};
    };

    struct TextMetrics
    {
        glm::vec2 size{0.0f};
        float ascent{0.0f};
        float descent{0.0f};
        float lineGap{0.0f};
    };

    struct ShapedGlyph
    {
        uint32_t glyphIndex{0};
        uint32_t cluster{0};
        glm::vec2 offset{0.0f};
        glm::vec2 advance{0.0f};
    };

    struct ShapedText
    {
        FontFaceId faceId{};
        TextMetrics metrics{};
        std::vector<ShapedGlyph> glyphs{};
    };

    enum class FontAtlasType : uint8_t
    {
        SignedDistanceField = 0,
        MultiChannelSignedDistanceField = 1,
        MultiChannelTrueSignedDistanceField = 2
    };

    struct FontAtlasRequest
    {
        std::vector<uint32_t> codepoints{};
        FontAtlasType atlasType{FontAtlasType::MultiChannelSignedDistanceField};
        float minimumScale{24.0f};
        float pixelRange{2.0f};
        float miterLimit{1.0f};
        double maxCornerAngle{3.0};
        int width{0};
        int height{0};
        int threadCount{0};
        bool squareDimensions{true};
    };

    struct FontAtlasGlyph
    {
        uint32_t codePoint{0};
        uint32_t glyphIndex{0};
        float advance{0.0f};
        // left, top, right, bottom relative to the baseline in top-down UI coordinates
        glm::vec4 planeBounds{0.0f};
        // left, top, right, bottom in atlas pixel coordinates with a top-left origin
        glm::vec4 atlasBounds{0.0f};
    };

    struct FontAtlasBitmap
    {
        int width{0};
        int height{0};
        int channels{0};
        std::vector<uint8_t> pixels{};
    };

    struct GeneratedFontAtlas
    {
        FontFaceId faceId{};
        FontAtlasType atlasType{FontAtlasType::MultiChannelSignedDistanceField};
        float scale{0.0f};
        float pixelRange{0.0f};
        float ascender{0.0f};
        float descender{0.0f};
        float lineHeight{0.0f};
        FontAtlasBitmap bitmap{};
        std::vector<FontAtlasGlyph> glyphs{};
    };

    class FontBackend
    {
    public:
        virtual ~FontBackend() = default;

        [[nodiscard]] virtual std::string_view backend_name() const = 0;
        [[nodiscard]] virtual bool supports_source_format(FontSourceFormat format) const = 0;
        [[nodiscard]] virtual bool supports_rasterization(FontRasterizationMode mode) const = 0;
        [[nodiscard]] virtual bool can_load_face(const FontFaceDescriptor& face) const = 0;
        [[nodiscard]] virtual bool supports_atlas_type(FontAtlasType type) const = 0;
        [[nodiscard]] virtual TextMetrics measure_text(const TextRun& run, const FontFaceDescriptor& face) const = 0;
        [[nodiscard]] virtual std::optional<ShapedText> shape_text(const TextRun& run, const FontFaceDescriptor& face) const = 0;
        [[nodiscard]] virtual std::optional<GeneratedFontAtlas> generate_atlas(const FontFaceDescriptor& face, const FontAtlasRequest& request) const = 0;
    };

    class FontCatalog
    {
    public:
        void register_face(FontFaceDescriptor face);
        void register_family(FontFamily family);

        [[nodiscard]] const FontFaceDescriptor* find_face(FontFaceId id) const;
        [[nodiscard]] const FontFamily* find_family(FontFamilyId id) const;
        [[nodiscard]] std::vector<const FontFaceDescriptor*> resolve_face_chain(FontFamilyId familyId) const;
        [[nodiscard]] std::optional<FontFaceId> resolve_face_for_backend(FontFamilyId familyId, const FontBackend& backend) const;

    private:
        std::unordered_map<FontFaceId, FontFaceDescriptor> _faces{};
        std::unordered_map<FontFamilyId, FontFamily> _families{};
    };

    class FontBackendRegistry
    {
    public:
        void register_backend(std::shared_ptr<FontBackend> backend);
        [[nodiscard]] std::shared_ptr<FontBackend> find_backend(std::string_view backendName) const;
        [[nodiscard]] const std::vector<std::shared_ptr<FontBackend>>& backends() const noexcept;

    private:
        std::vector<std::shared_ptr<FontBackend>> _backends{};
    };
}

#pragma once

#include "ui_font_backend_freetype.h"

namespace ui
{
    class MsdfAtlasFontBackend final : public FontBackend
    {
    public:
        MsdfAtlasFontBackend() = default;
        ~MsdfAtlasFontBackend() override = default;

        [[nodiscard]] std::string_view backend_name() const override;
        [[nodiscard]] bool supports_source_format(FontSourceFormat format) const override;
        [[nodiscard]] bool supports_rasterization(FontRasterizationMode mode) const override;
        [[nodiscard]] bool can_load_face(const FontFaceDescriptor& face) const override;
        [[nodiscard]] bool supports_atlas_type(FontAtlasType type) const override;
        [[nodiscard]] TextMetrics measure_text(const TextRun& run, const FontFaceDescriptor& face) const override;
        [[nodiscard]] std::optional<ShapedText> shape_text(const TextRun& run, const FontFaceDescriptor& face) const override;
        [[nodiscard]] std::optional<GeneratedFontAtlas> generate_atlas(const FontFaceDescriptor& face, const FontAtlasRequest& request) const override;

    private:
        [[nodiscard]] static const FontAssetSource* select_supported_source(const FontFaceDescriptor& face);

        FreeTypeHarfBuzzFontBackend _shapeBackend{};
    };
}

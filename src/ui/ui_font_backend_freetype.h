#pragma once

#include <mutex>
#include <unordered_map>

#include "ui_text.h"

struct FT_LibraryRec_;
struct FT_FaceRec_;
struct hb_font_t;

namespace ui
{
    class FreeTypeHarfBuzzFontBackend final : public FontBackend
    {
    public:
        FreeTypeHarfBuzzFontBackend();
        ~FreeTypeHarfBuzzFontBackend() override;

        FreeTypeHarfBuzzFontBackend(const FreeTypeHarfBuzzFontBackend&) = delete;
        FreeTypeHarfBuzzFontBackend& operator=(const FreeTypeHarfBuzzFontBackend&) = delete;

        [[nodiscard]] std::string_view backend_name() const override;
        [[nodiscard]] bool supports_source_format(FontSourceFormat format) const override;
        [[nodiscard]] bool supports_rasterization(FontRasterizationMode mode) const override;
        [[nodiscard]] bool can_load_face(const FontFaceDescriptor& face) const override;
        [[nodiscard]] bool supports_atlas_type(FontAtlasType type) const override;
        [[nodiscard]] TextMetrics measure_text(const TextRun& run, const FontFaceDescriptor& face) const override;
        [[nodiscard]] std::optional<ShapedText> shape_text(const TextRun& run, const FontFaceDescriptor& face) const override;
        [[nodiscard]] std::optional<GeneratedFontAtlas> generate_atlas(const FontFaceDescriptor& face, const FontAtlasRequest& request) const override;

    private:
        struct LoadedFace
        {
            std::filesystem::path path{};
            FT_FaceRec_* face{nullptr};
            hb_font_t* hbFont{nullptr};
        };

        [[nodiscard]] const LoadedFace* ensure_face_loaded(const FontFaceDescriptor& face) const;
        [[nodiscard]] static const FontAssetSource* select_supported_source(const FontFaceDescriptor& face);
        static void destroy_loaded_face(LoadedFace& face) noexcept;

        FT_LibraryRec_* _library{nullptr};
        mutable std::unordered_map<FontFaceId, LoadedFace> _loadedFaces{};
        mutable std::mutex _loadedFacesMutex{};
        bool _available{false};
    };
}

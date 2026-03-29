#include "ui_font_backend_freetype.h"

#include <algorithm>
#include <cmath>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <hb-ft.h>
#include <hb.h>

namespace ui
{
    namespace
    {
        [[nodiscard]] hb_direction_t to_hb_direction(const TextDirection direction)
        {
            switch (direction)
            {
            case TextDirection::LeftToRight:
                return HB_DIRECTION_LTR;
            case TextDirection::RightToLeft:
                return HB_DIRECTION_RTL;
            case TextDirection::Auto:
            default:
                return HB_DIRECTION_INVALID;
            }
        }
    }

    FreeTypeHarfBuzzFontBackend::FreeTypeHarfBuzzFontBackend()
    {
        _available = FT_Init_FreeType(reinterpret_cast<FT_Library*>(&_library)) == 0;
    }

    FreeTypeHarfBuzzFontBackend::~FreeTypeHarfBuzzFontBackend()
    {
        for (auto& [_, loadedFace] : _loadedFaces)
        {
            destroy_loaded_face(loadedFace);
        }

        _loadedFaces.clear();

        if (_library != nullptr)
        {
            FT_Done_FreeType(reinterpret_cast<FT_Library>(_library));
            _library = nullptr;
        }
    }

    std::string_view FreeTypeHarfBuzzFontBackend::backend_name() const
    {
        return "freetype_harfbuzz";
    }

    bool FreeTypeHarfBuzzFontBackend::supports_source_format(const FontSourceFormat format) const
    {
        return format == FontSourceFormat::TrueType || format == FontSourceFormat::OpenType;
    }

    bool FreeTypeHarfBuzzFontBackend::supports_rasterization(const FontRasterizationMode mode) const
    {
        return mode == FontRasterizationMode::Vector;
    }

    bool FreeTypeHarfBuzzFontBackend::can_load_face(const FontFaceDescriptor& face) const
    {
        return _available && select_supported_source(face) != nullptr;
    }

    bool FreeTypeHarfBuzzFontBackend::supports_atlas_type(const FontAtlasType) const
    {
        return false;
    }

    TextMetrics FreeTypeHarfBuzzFontBackend::measure_text(const TextRun& run, const FontFaceDescriptor& face) const
    {
        const std::optional<ShapedText> shaped = shape_text(run, face);
        if (!shaped.has_value())
        {
            return {};
        }

        return shaped->metrics;
    }

    std::optional<ShapedText> FreeTypeHarfBuzzFontBackend::shape_text(const TextRun& run, const FontFaceDescriptor& face) const
    {
        const LoadedFace* loaded = ensure_face_loaded(face);
        if (loaded == nullptr || loaded->face == nullptr || loaded->hbFont == nullptr)
        {
            return std::nullopt;
        }

        const auto pixelHeight = static_cast<uint32_t>(std::max(1.0f, std::round(run.pixelHeight)));
        if (FT_Set_Pixel_Sizes(reinterpret_cast<FT_Face>(loaded->face), 0, pixelHeight) != 0)
        {
            return std::nullopt;
        }

        hb_ft_font_set_load_flags(
            loaded->hbFont,
            FT_LOAD_NO_HINTING | FT_LOAD_NO_AUTOHINT | FT_LOAD_NO_BITMAP);
        hb_ft_font_changed(loaded->hbFont);

        hb_buffer_t* buffer = hb_buffer_create();
        hb_buffer_add_utf8(buffer, run.text.c_str(), static_cast<int>(run.text.size()), 0, static_cast<int>(run.text.size()));

        if (!run.languageTag.empty())
        {
            hb_buffer_set_language(buffer, hb_language_from_string(run.languageTag.c_str(), -1));
        }

        if (const hb_direction_t direction = to_hb_direction(run.direction); direction != HB_DIRECTION_INVALID)
        {
            hb_buffer_set_direction(buffer, direction);
        }

        hb_buffer_guess_segment_properties(buffer);
        hb_shape(loaded->hbFont, buffer, nullptr, 0);

        unsigned int glyphCount = 0;
        const hb_glyph_info_t* glyphInfos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
        const hb_glyph_position_t* glyphPositions = hb_buffer_get_glyph_positions(buffer, &glyphCount);

        ShapedText shaped{
            .faceId = face.id
        };
        shaped.glyphs.reserve(glyphCount);

        float totalWidth = 0.0f;
        for (unsigned int i = 0; i < glyphCount; ++i)
        {
            const float advanceX = static_cast<float>(glyphPositions[i].x_advance) / 64.0f;
            const float advanceY = static_cast<float>(glyphPositions[i].y_advance) / 64.0f;
            shaped.glyphs.push_back(ShapedGlyph{
                .glyphIndex = glyphInfos[i].codepoint,
                .cluster = glyphInfos[i].cluster,
                .offset = glm::vec2(
                    static_cast<float>(glyphPositions[i].x_offset) / 64.0f,
                    static_cast<float>(glyphPositions[i].y_offset) / 64.0f),
                .advance = glm::vec2(advanceX + run.letterSpacing, advanceY)
            });
            totalWidth += advanceX + run.letterSpacing;
        }

        const FT_Face ftFace = reinterpret_cast<FT_Face>(loaded->face);
        const float ascent = static_cast<float>(ftFace->size->metrics.ascender) / 64.0f;
        const float descent = std::abs(static_cast<float>(ftFace->size->metrics.descender) / 64.0f);
        const float lineHeight = static_cast<float>(ftFace->size->metrics.height) / 64.0f;
        const float lineGap = std::max(0.0f, lineHeight - ascent - descent);

        shaped.metrics = TextMetrics{
            .size = glm::vec2(totalWidth, lineHeight),
            .ascent = ascent,
            .descent = descent,
            .lineGap = lineGap
        };

        if (run.pixelSnap)
        {
            shaped.metrics.size.x = std::round(shaped.metrics.size.x);
            shaped.metrics.size.y = std::round(shaped.metrics.size.y);
        }

        hb_buffer_destroy(buffer);
        return shaped;
    }

    std::optional<GeneratedFontAtlas> FreeTypeHarfBuzzFontBackend::generate_atlas(const FontFaceDescriptor&, const FontAtlasRequest&) const
    {
        return std::nullopt;
    }

    const FreeTypeHarfBuzzFontBackend::LoadedFace* FreeTypeHarfBuzzFontBackend::ensure_face_loaded(const FontFaceDescriptor& face) const
    {
        if (!_available)
        {
            return nullptr;
        }

        {
            const std::scoped_lock lock(_loadedFacesMutex);
            const auto existing = _loadedFaces.find(face.id);
            if (existing != _loadedFaces.end())
            {
                return &existing->second;
            }
        }

        const FontAssetSource* source = select_supported_source(face);
        if (source == nullptr)
        {
            return nullptr;
        }

        FT_Face ftFace = nullptr;
        const std::string pathString = source->path.string();
        if (FT_New_Face(reinterpret_cast<FT_Library>(_library), pathString.c_str(), 0, &ftFace) != 0)
        {
            return nullptr;
        }

        hb_font_t* hbFont = hb_ft_font_create_referenced(ftFace);
        if (hbFont == nullptr)
        {
            FT_Done_Face(ftFace);
            return nullptr;
        }

        std::scoped_lock lock(_loadedFacesMutex);
        auto [it, inserted] = _loadedFaces.emplace(face.id, LoadedFace{
            .path = source->path,
            .face = reinterpret_cast<FT_FaceRec_*>(ftFace),
            .hbFont = hbFont
        });

        if (!inserted)
        {
            hb_font_destroy(hbFont);
            FT_Done_Face(ftFace);
        }

        return &it->second;
    }

    const FontAssetSource* FreeTypeHarfBuzzFontBackend::select_supported_source(const FontFaceDescriptor& face)
    {
        const auto it = std::ranges::find_if(face.sources, [&](const FontAssetSource& source)
        {
            return source.format == FontSourceFormat::TrueType || source.format == FontSourceFormat::OpenType;
        });

        if (it == face.sources.end())
        {
            return nullptr;
        }

        return &(*it);
    }

    void FreeTypeHarfBuzzFontBackend::destroy_loaded_face(LoadedFace& face) noexcept
    {
        if (face.hbFont != nullptr)
        {
            hb_font_destroy(face.hbFont);
            face.hbFont = nullptr;
        }

        if (face.face != nullptr)
        {
            FT_Done_Face(reinterpret_cast<FT_Face>(face.face));
            face.face = nullptr;
        }
    }
}

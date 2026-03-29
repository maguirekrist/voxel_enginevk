#include "ui_font_backend_msdf.h"

#include <algorithm>

#include <msdf-atlas-gen/msdf-atlas-gen.h>

namespace ui
{
    namespace
    {
        [[nodiscard]] msdf_atlas::Charset build_charset(const FontAtlasRequest& request)
        {
            if (request.codepoints.empty())
            {
                return msdf_atlas::Charset::ASCII;
            }

            msdf_atlas::Charset charset{};
            for (const uint32_t codePoint : request.codepoints)
            {
                charset.add(static_cast<msdf_atlas::unicode_t>(codePoint));
            }

            return charset;
        }

        void color_glyph_edges(std::vector<msdf_atlas::GlyphGeometry>& glyphs, const FontAtlasRequest& request)
        {
            for (msdf_atlas::GlyphGeometry& glyph : glyphs)
            {
                glyph.edgeColoring(&msdfgen::edgeColoringInkTrap, request.maxCornerAngle, 0);
            }
        }

        [[nodiscard]] bool pack_glyphs(std::vector<msdf_atlas::GlyphGeometry>& glyphs, const FontAtlasRequest& request, int& width, int& height, float& scale, float& pixelRange)
        {
            if ((request.width > 0) != (request.height > 0))
            {
                return false;
            }

            msdf_atlas::TightAtlasPacker packer{};
            if (request.width > 0 && request.height > 0)
            {
                packer.setDimensions(request.width, request.height);
            }
            else
            {
                packer.setDimensionsConstraint(request.squareDimensions ? msdf_atlas::DimensionsConstraint::SQUARE : msdf_atlas::DimensionsConstraint::NONE);
            }

            packer.setMinimumScale(std::max(1.0f, request.minimumScale));
            packer.setPixelRange(std::max(0.5f, request.pixelRange));
            packer.setMiterLimit(std::max(0.0f, request.miterLimit));

            if (packer.pack(glyphs.data(), static_cast<int>(glyphs.size())) != 0)
            {
                return false;
            }

            packer.getDimensions(width, height);
            scale = static_cast<float>(packer.getScale());
            const msdfgen::Range packedRange = packer.getPixelRange();
            pixelRange = static_cast<float>(packedRange.upper - packedRange.lower);
            return width > 0 && height > 0;
        }

        template <int Channels>
        void copy_bitmap_pixels(const msdfgen::BitmapConstRef<msdf_atlas::byte, Channels>& bitmapRef, FontAtlasBitmap& output)
        {
            msdfgen::BitmapConstSection<msdf_atlas::byte, Channels> bitmap = bitmapRef;
            bitmap.reorient(msdfgen::Y_DOWNWARD);
            output.width = bitmap.width;
            output.height = bitmap.height;
            output.channels = Channels;
            output.pixels.resize(static_cast<size_t>(bitmap.width) * static_cast<size_t>(bitmap.height) * Channels);

            for (int y = 0; y < bitmap.height; ++y)
            {
                for (int x = 0; x < bitmap.width; ++x)
                {
                    const msdf_atlas::byte* sourcePixel = bitmap(x, y);
                    const size_t destinationIndex = (static_cast<size_t>(y) * static_cast<size_t>(bitmap.width) + static_cast<size_t>(x)) * Channels;
                    for (int channel = 0; channel < Channels; ++channel)
                    {
                        output.pixels[destinationIndex + static_cast<size_t>(channel)] = sourcePixel[channel];
                    }
                }
            }
        }

        void append_glyph_metadata(const std::vector<msdf_atlas::GlyphGeometry>& glyphs, GeneratedFontAtlas& atlas)
        {
            atlas.glyphs.reserve(glyphs.size());
            for (const msdf_atlas::GlyphGeometry& glyph : glyphs)
            {
                double planeLeft = 0.0;
                double planeBottom = 0.0;
                double planeRight = 0.0;
                double planeTop = 0.0;
                glyph.getQuadPlaneBounds(planeLeft, planeBottom, planeRight, planeTop);

                double atlasLeft = 0.0;
                double atlasBottom = 0.0;
                double atlasRight = 0.0;
                double atlasTop = 0.0;
                glyph.getQuadAtlasBounds(atlasLeft, atlasBottom, atlasRight, atlasTop);

                atlas.glyphs.push_back(FontAtlasGlyph{
                    .codePoint = static_cast<uint32_t>(glyph.getCodepoint()),
                    .glyphIndex = glyph.getGlyphIndex().getIndex(),
                    .advance = static_cast<float>(glyph.getAdvance()),
                    .planeBounds = glm::vec4(
                        static_cast<float>(planeLeft),
                        static_cast<float>(-planeTop),
                        static_cast<float>(planeRight),
                        static_cast<float>(-planeBottom)),
                    .atlasBounds = glm::vec4(
                        static_cast<float>(atlasLeft),
                        static_cast<float>(static_cast<double>(atlas.bitmap.height) - atlasTop),
                        static_cast<float>(atlasRight),
                        static_cast<float>(static_cast<double>(atlas.bitmap.height) - atlasBottom))
                });
            }
        }

        template <int Channels, auto GeneratorFunction>
        [[nodiscard]] std::optional<GeneratedFontAtlas> generate_bitmap_atlas(
            const FontFaceDescriptor& face,
            const FontAtlasRequest& request,
            std::vector<msdf_atlas::GlyphGeometry> glyphs,
            const int width,
            const int height,
            const float scale,
            const float pixelRange)
        {
            msdf_atlas::ImmediateAtlasGenerator<
                float,
                Channels,
                GeneratorFunction,
                msdf_atlas::BitmapAtlasStorage<msdf_atlas::byte, Channels>> generator(width, height);

            msdf_atlas::GeneratorAttributes attributes{};
            generator.setAttributes(attributes);
            generator.setThreadCount(request.threadCount);
            generator.generate(glyphs.data(), static_cast<int>(glyphs.size()));

            GeneratedFontAtlas atlas{
                .faceId = face.id,
                .atlasType = request.atlasType,
                .scale = scale,
                .pixelRange = pixelRange
            };

            copy_bitmap_pixels<Channels>(static_cast<msdfgen::BitmapConstRef<msdf_atlas::byte, Channels>>(generator.atlasStorage()), atlas.bitmap);
            append_glyph_metadata(glyphs, atlas);
            return atlas;
        }
    }

    std::string_view MsdfAtlasFontBackend::backend_name() const
    {
        return "msdf_atlas";
    }

    bool MsdfAtlasFontBackend::supports_source_format(const FontSourceFormat format) const
    {
        return format == FontSourceFormat::TrueType || format == FontSourceFormat::OpenType;
    }

    bool MsdfAtlasFontBackend::supports_rasterization(const FontRasterizationMode mode) const
    {
        return mode == FontRasterizationMode::Vector || mode == FontRasterizationMode::DistanceField;
    }

    bool MsdfAtlasFontBackend::can_load_face(const FontFaceDescriptor& face) const
    {
        return _shapeBackend.can_load_face(face) && select_supported_source(face) != nullptr;
    }

    bool MsdfAtlasFontBackend::supports_atlas_type(const FontAtlasType type) const
    {
        return type == FontAtlasType::SignedDistanceField ||
            type == FontAtlasType::MultiChannelSignedDistanceField ||
            type == FontAtlasType::MultiChannelTrueSignedDistanceField;
    }

    TextMetrics MsdfAtlasFontBackend::measure_text(const TextRun& run, const FontFaceDescriptor& face) const
    {
        return _shapeBackend.measure_text(run, face);
    }

    std::optional<ShapedText> MsdfAtlasFontBackend::shape_text(const TextRun& run, const FontFaceDescriptor& face) const
    {
        return _shapeBackend.shape_text(run, face);
    }

    std::optional<GeneratedFontAtlas> MsdfAtlasFontBackend::generate_atlas(const FontFaceDescriptor& face, const FontAtlasRequest& request) const
    {
        if (!supports_atlas_type(request.atlasType))
        {
            return std::nullopt;
        }

        const FontAssetSource* source = select_supported_source(face);
        if (source == nullptr)
        {
            return std::nullopt;
        }

        msdfgen::FreetypeHandle* freeType = msdfgen::initializeFreetype();
        if (freeType == nullptr)
        {
            return std::nullopt;
        }

        std::optional<GeneratedFontAtlas> atlas{};
        const std::string pathString = source->path.string();
        if (msdfgen::FontHandle* font = msdfgen::loadFont(freeType, pathString.c_str()))
        {
            std::vector<msdf_atlas::GlyphGeometry> glyphs{};
            msdf_atlas::FontGeometry fontGeometry(&glyphs);
            const int loadedGlyphCount = fontGeometry.loadCharset(font, 1.0, build_charset(request));
            if (loadedGlyphCount > 0)
            {
                if (request.atlasType != FontAtlasType::SignedDistanceField)
                {
                    color_glyph_edges(glyphs, request);
                }

                int width = 0;
                int height = 0;
                float scale = 0.0f;
                float pixelRange = 0.0f;
                if (pack_glyphs(glyphs, request, width, height, scale, pixelRange))
                {
                    switch (request.atlasType)
                    {
                    case FontAtlasType::SignedDistanceField:
                        atlas = generate_bitmap_atlas<1, msdf_atlas::sdfGenerator>(face, request, std::move(glyphs), width, height, scale, pixelRange);
                        break;
                    case FontAtlasType::MultiChannelSignedDistanceField:
                        atlas = generate_bitmap_atlas<3, msdf_atlas::msdfGenerator>(face, request, std::move(glyphs), width, height, scale, pixelRange);
                        break;
                    case FontAtlasType::MultiChannelTrueSignedDistanceField:
                        atlas = generate_bitmap_atlas<4, msdf_atlas::mtsdfGenerator>(face, request, std::move(glyphs), width, height, scale, pixelRange);
                        break;
                    }

                    if (atlas.has_value())
                    {
                        const msdfgen::FontMetrics& metrics = fontGeometry.getMetrics();
                        atlas->ascender = static_cast<float>(metrics.ascenderY);
                        atlas->descender = static_cast<float>(std::abs(metrics.descenderY));
                        atlas->lineHeight = static_cast<float>(metrics.lineHeight);
                    }
                }
            }

            msdfgen::destroyFont(font);
        }

        msdfgen::deinitializeFreetype(freeType);
        return atlas;
    }

    const FontAssetSource* MsdfAtlasFontBackend::select_supported_source(const FontFaceDescriptor& face)
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
}

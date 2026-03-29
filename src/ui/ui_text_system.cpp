#include "ui_text_system.h"

#include <algorithm>
#include <array>
#include <ranges>

namespace ui
{
    namespace
    {
        [[nodiscard]] float maybe_snap(const float value, const bool pixelSnap)
        {
            return pixelSnap ? std::round(value) : value;
        }
    }

    FontCatalog& TextSystem::font_catalog() noexcept
    {
        return _fontCatalog;
    }

    const FontCatalog& TextSystem::font_catalog() const noexcept
    {
        return _fontCatalog;
    }

    FontBackendRegistry& TextSystem::backend_registry() noexcept
    {
        return _backendRegistry;
    }

    const FontBackendRegistry& TextSystem::backend_registry() const noexcept
    {
        return _backendRegistry;
    }

    void TextSystem::register_face(FontFaceDescriptor face)
    {
        _fontCatalog.register_face(std::move(face));
    }

    void TextSystem::register_family(FontFamily family)
    {
        _fontCatalog.register_family(std::move(family));
    }

    void TextSystem::register_backend(std::shared_ptr<FontBackend> backend)
    {
        _backendRegistry.register_backend(std::move(backend));
    }

    void TextSystem::set_default_atlas_type(const FontAtlasType atlasType) noexcept
    {
        _defaultAtlasType = atlasType;
    }

    const GeneratedFontAtlas* TextSystem::find_generated_atlas(const FontFaceId faceId, const FontAtlasType atlasType) const
    {
        const auto it = _atlasCache.find(CachedAtlasKey{
            .faceId = faceId,
            .atlasType = atlasType
        });

        if (it == _atlasCache.end())
        {
            return nullptr;
        }

        return &it->second.atlas;
    }

    std::optional<PreparedTextCommand> TextSystem::prepare_text_command(const TextCommand& command)
    {
        const std::optional<PreparedFontContext> fontContext = resolve_font_context(command.text.familyId);
        if (!fontContext.has_value() || fontContext->face == nullptr || fontContext->backend == nullptr)
        {
            return std::nullopt;
        }

        const std::vector<uint32_t> orderedCodePoints = decode_utf8_codepoints(command.text.text);
        CachedAtlasRecord* atlasRecord = ensure_atlas(*fontContext, unique_codepoints(command.text.text), command.text.pixelHeight);
        if (atlasRecord == nullptr)
        {
            return std::nullopt;
        }

        const glm::vec2 commandSize = command.rect.size();
        const float glyphScale =
            atlasRecord->atlas.lineHeight > 0.0f
            ? command.text.pixelHeight / atlasRecord->atlas.lineHeight
            : command.text.pixelHeight;

        const bool canUseAtlasLayout =
            command.text.direction != TextDirection::RightToLeft &&
            std::ranges::all_of(orderedCodePoints, [&](const uint32_t codePoint)
            {
                return codePoint < 0x80u && atlasRecord->glyphsByCodePoint.contains(codePoint);
            });

        const float layoutAscent =
            atlasRecord->atlas.ascender > 0.0f
            ? atlasRecord->atlas.ascender * glyphScale
            : 0.0f;
        const float layoutDescent =
            atlasRecord->atlas.descender >= 0.0f
            ? atlasRecord->atlas.descender * glyphScale
            : 0.0f;
        const float layoutLineHeight =
            atlasRecord->atlas.lineHeight > 0.0f
            ? atlasRecord->atlas.lineHeight * glyphScale
            : command.text.pixelHeight;

        if (canUseAtlasLayout)
        {
            float textWidth = 0.0f;
            for (size_t index = 0; index < orderedCodePoints.size(); ++index)
            {
                const FontAtlasGlyph& atlasGlyph = atlasRecord->atlas.glyphs[atlasRecord->glyphsByCodePoint.at(orderedCodePoints[index])];
                textWidth += (atlasGlyph.advance * glyphScale);
                if (index + 1 < orderedCodePoints.size())
                {
                    textWidth += command.text.letterSpacing;
                }
            }

            const float horizontalOffset =
                command.alignment == TextAlignment::Center ? (commandSize.x - textWidth) * 0.5f :
                command.alignment == TextAlignment::End ? (commandSize.x - textWidth) :
                0.0f;
            const float baselineY = maybe_snap(
                command.rect.min.y + std::max(0.0f, (commandSize.y - layoutLineHeight) * 0.5f) + layoutAscent,
                command.text.pixelSnap);

            PreparedTextCommand prepared{
                .rect = command.rect,
                .metrics = TextMetrics{
                    .size = glm::vec2(textWidth, layoutLineHeight),
                    .ascent = layoutAscent,
                    .descent = layoutDescent,
                    .lineGap = std::max(0.0f, layoutLineHeight - layoutAscent - layoutDescent)
                },
                .faceId = fontContext->face->id,
                .atlasType = _defaultAtlasType,
                .color = command.color,
                .layerId = command.layerId,
                .alignment = command.alignment,
                .style = command.style
            };
            prepared.glyphQuads.reserve(orderedCodePoints.size());

            float cursorX = command.rect.min.x + horizontalOffset;
            for (const uint32_t codePoint : orderedCodePoints)
            {
                const FontAtlasGlyph& atlasGlyph = atlasRecord->atlas.glyphs[atlasRecord->glyphsByCodePoint.at(codePoint)];
                const glm::vec4& plane = atlasGlyph.planeBounds;
                const glm::vec4& atlas = atlasGlyph.atlasBounds;
                const float glyphOriginX = maybe_snap(cursorX, command.text.pixelSnap);

                if (atlas.z > atlas.x && atlas.w > atlas.y)
                {
                    prepared.glyphQuads.push_back(PreparedGlyphQuad{
                        .rect = Rect{
                            .min = glm::vec2(
                                glyphOriginX + (plane.x * glyphScale),
                                baselineY + (plane.y * glyphScale)),
                            .max = glm::vec2(
                                glyphOriginX + (plane.z * glyphScale),
                                baselineY + (plane.w * glyphScale))
                        },
                        .uvBounds = glm::vec4(
                            atlas.x / static_cast<float>(atlasRecord->atlas.bitmap.width),
                            atlas.y / static_cast<float>(atlasRecord->atlas.bitmap.height),
                            atlas.z / static_cast<float>(atlasRecord->atlas.bitmap.width),
                            atlas.w / static_cast<float>(atlasRecord->atlas.bitmap.height)),
                        .codePoint = atlasGlyph.codePoint,
                        .glyphIndex = atlasGlyph.glyphIndex
                    });
                }

                cursorX += (atlasGlyph.advance * glyphScale) + command.text.letterSpacing;
            }

            return prepared;
        }

        const std::optional<ShapedText> shaped = fontContext->backend->shape_text(command.text, *fontContext->face);
        if (!shaped.has_value())
        {
            return std::nullopt;
        }

        const float horizontalOffset =
            command.alignment == TextAlignment::Center ? (commandSize.x - shaped->metrics.size.x) * 0.5f :
            command.alignment == TextAlignment::End ? (commandSize.x - shaped->metrics.size.x) :
            0.0f;
        const float baselineY = maybe_snap(
            command.rect.min.y + std::max(0.0f, (commandSize.y - layoutLineHeight) * 0.5f) + layoutAscent,
            command.text.pixelSnap);

        PreparedTextCommand prepared{
            .rect = command.rect,
            .metrics = TextMetrics{
                .size = glm::vec2(shaped->metrics.size.x, layoutLineHeight),
                .ascent = layoutAscent,
                .descent = layoutDescent,
                .lineGap = std::max(0.0f, layoutLineHeight - layoutAscent - layoutDescent)
            },
            .faceId = fontContext->face->id,
            .atlasType = _defaultAtlasType,
            .color = command.color,
            .layerId = command.layerId,
            .alignment = command.alignment,
            .style = command.style
        };
        prepared.glyphQuads.reserve(shaped->glyphs.size());

        float cursorX = command.rect.min.x + horizontalOffset;
        for (const ShapedGlyph& glyph : shaped->glyphs)
        {
            const auto glyphIt = atlasRecord->glyphsByIndex.find(glyph.glyphIndex);
            if (glyphIt != atlasRecord->glyphsByIndex.end())
            {
                const FontAtlasGlyph& atlasGlyph = atlasRecord->atlas.glyphs[glyphIt->second];
                const glm::vec4& plane = atlasGlyph.planeBounds;
                const glm::vec4& atlas = atlasGlyph.atlasBounds;
                const float glyphOriginX = maybe_snap(cursorX + glyph.offset.x, command.text.pixelSnap);
                const float glyphBaselineY = maybe_snap(baselineY - glyph.offset.y, command.text.pixelSnap);
                const float left = glyphOriginX + (plane.x * glyphScale);
                const float right = glyphOriginX + (plane.z * glyphScale);
                const float top = glyphBaselineY + (plane.y * glyphScale);
                const float bottom = glyphBaselineY + (plane.w * glyphScale);

                prepared.glyphQuads.push_back(PreparedGlyphQuad{
                    .rect = Rect{
                        .min = glm::vec2(left, top),
                        .max = glm::vec2(right, bottom)
                    },
                    .uvBounds = glm::vec4(
                        atlas.x / static_cast<float>(atlasRecord->atlas.bitmap.width),
                        atlas.y / static_cast<float>(atlasRecord->atlas.bitmap.height),
                        atlas.z / static_cast<float>(atlasRecord->atlas.bitmap.width),
                        atlas.w / static_cast<float>(atlasRecord->atlas.bitmap.height)),
                    .codePoint = atlasGlyph.codePoint,
                    .glyphIndex = glyph.glyphIndex
                });
            }

            cursorX += glyph.advance.x;
        }

        return prepared;
    }

    std::optional<TextSystem::PreparedFontContext> TextSystem::resolve_font_context(const FontFamilyId familyId) const
    {
        for (const FontFaceDescriptor* face : _fontCatalog.resolve_face_chain(familyId))
        {
            if (face == nullptr)
            {
                continue;
            }

            const auto backendIt = std::ranges::find_if(_backendRegistry.backends(), [&](const std::shared_ptr<FontBackend>& backend)
            {
                return backend != nullptr &&
                    backend->can_load_face(*face) &&
                    backend->supports_atlas_type(_defaultAtlasType);
            });

            if (backendIt != _backendRegistry.backends().end())
            {
                return PreparedFontContext{
                    .face = face,
                    .backend = *backendIt
                };
            }
        }

        return std::nullopt;
    }

    TextSystem::CachedAtlasRecord* TextSystem::ensure_atlas(
        const PreparedFontContext& fontContext,
        const std::span<const uint32_t> requiredCodePoints,
        const float requestedPixelHeight)
    {
        const CachedAtlasKey key{
            .faceId = fontContext.face->id,
            .atlasType = _defaultAtlasType
        };

        auto [it, inserted] = _atlasCache.try_emplace(key);
        CachedAtlasRecord& record = it->second;

        std::vector<uint32_t> mergedCodePoints = record.codePoints;
        mergedCodePoints.insert(mergedCodePoints.end(), requiredCodePoints.begin(), requiredCodePoints.end());
        std::ranges::sort(mergedCodePoints);
        mergedCodePoints.erase(std::ranges::unique(mergedCodePoints).begin(), mergedCodePoints.end());

        const bool atlasMissing = inserted || record.atlas.bitmap.pixels.empty();
        const bool atlasNeedsExpansion = mergedCodePoints.size() != record.codePoints.size();
        if (!atlasMissing && !atlasNeedsExpansion)
        {
            return &record;
        }

        const float existingScale = record.atlas.scale;
        const FontAtlasRequest request{
            .codepoints = mergedCodePoints,
            .atlasType = _defaultAtlasType,
            .minimumScale = std::max({ 24.0f, fontContext.face->nominalPixelHeight, requestedPixelHeight, existingScale }),
            .pixelRange = 2.0f,
            .miterLimit = 1.0f,
            .maxCornerAngle = 3.0,
            .threadCount = 1,
            .squareDimensions = true
        };

        const std::optional<GeneratedFontAtlas> atlas = fontContext.backend->generate_atlas(*fontContext.face, request);
        if (!atlas.has_value())
        {
            return nullptr;
        }

        record.atlas = atlas.value();
        record.codePoints = std::move(mergedCodePoints);
        rebuild_lookup_maps(record);
        return &record;
    }

    void TextSystem::rebuild_lookup_maps(CachedAtlasRecord& record)
    {
        record.glyphsByCodePoint.clear();
        record.glyphsByIndex.clear();

        for (size_t index = 0; index < record.atlas.glyphs.size(); ++index)
        {
            const FontAtlasGlyph& glyph = record.atlas.glyphs[index];
            record.glyphsByCodePoint[glyph.codePoint] = index;
            record.glyphsByIndex[glyph.glyphIndex] = index;
        }
    }

    std::vector<uint32_t> TextSystem::decode_utf8_codepoints(const std::string_view text)
    {
        std::vector<uint32_t> codePoints{};
        codePoints.reserve(text.size());

        size_t index = 0;
        while (index < text.size())
        {
            const unsigned char lead = static_cast<unsigned char>(text[index]);
            uint32_t codePoint = 0;
            size_t sequenceLength = 0;

            if ((lead & 0x80u) == 0)
            {
                codePoint = lead;
                sequenceLength = 1;
            }
            else if ((lead & 0xE0u) == 0xC0u && index + 1 < text.size())
            {
                codePoint = (static_cast<uint32_t>(lead & 0x1Fu) << 6) |
                    static_cast<uint32_t>(static_cast<unsigned char>(text[index + 1]) & 0x3Fu);
                sequenceLength = 2;
            }
            else if ((lead & 0xF0u) == 0xE0u && index + 2 < text.size())
            {
                codePoint =
                    (static_cast<uint32_t>(lead & 0x0Fu) << 12) |
                    (static_cast<uint32_t>(static_cast<unsigned char>(text[index + 1]) & 0x3Fu) << 6) |
                    static_cast<uint32_t>(static_cast<unsigned char>(text[index + 2]) & 0x3Fu);
                sequenceLength = 3;
            }
            else if ((lead & 0xF8u) == 0xF0u && index + 3 < text.size())
            {
                codePoint =
                    (static_cast<uint32_t>(lead & 0x07u) << 18) |
                    (static_cast<uint32_t>(static_cast<unsigned char>(text[index + 1]) & 0x3Fu) << 12) |
                    (static_cast<uint32_t>(static_cast<unsigned char>(text[index + 2]) & 0x3Fu) << 6) |
                    static_cast<uint32_t>(static_cast<unsigned char>(text[index + 3]) & 0x3Fu);
                sequenceLength = 4;
            }
            else
            {
                codePoint = 0xFFFDu;
                sequenceLength = 1;
            }

            codePoints.push_back(codePoint);
            index += sequenceLength;
        }

        return codePoints;
    }

    std::vector<uint32_t> TextSystem::unique_codepoints(const std::string_view text)
    {
        std::vector<uint32_t> codePoints = decode_utf8_codepoints(text);
        std::ranges::sort(codePoints);
        codePoints.erase(std::ranges::unique(codePoints).begin(), codePoints.end());
        return codePoints;
    }
}

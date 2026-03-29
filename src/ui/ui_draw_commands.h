#pragma once

#include <string>
#include <variant>
#include <vector>

#include "ui_text.h"

namespace ui
{
    struct QuadCommand
    {
        Rect rect{};
        glm::vec4 color{1.0f};
        LayerId layerId{};
        Style style{};
    };

    struct TextCommand
    {
        Rect rect{};
        TextRun text{};
        glm::vec4 color{1.0f};
        LayerId layerId{};
        TextAlignment alignment{TextAlignment::Start};
        Style style{};
    };

    struct IconCommand
    {
        Rect rect{};
        std::string iconId{};
        glm::vec4 tint{1.0f};
        LayerId layerId{};
        Style style{};
    };

    using DrawCommandPayload = std::variant<QuadCommand, TextCommand, IconCommand>;

    struct DrawCommand
    {
        DrawCommandPayload payload{};
    };

    struct PreparedGlyphQuad
    {
        Rect rect{};
        glm::vec4 uvBounds{0.0f};
        uint32_t codePoint{0};
        uint32_t glyphIndex{0};
    };

    struct PreparedTextCommand
    {
        Rect rect{};
        TextMetrics metrics{};
        FontFaceId faceId{};
        FontAtlasType atlasType{FontAtlasType::MultiChannelSignedDistanceField};
        glm::vec4 color{1.0f};
        LayerId layerId{};
        TextAlignment alignment{TextAlignment::Start};
        Style style{};
        std::vector<PreparedGlyphQuad> glyphQuads{};
    };
}

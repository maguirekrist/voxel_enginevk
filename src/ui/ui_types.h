#pragma once

#include <algorithm>
#include <compare>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace ui
{
    namespace detail
    {
        constexpr uint64_t fnv1a_64(const std::string_view value) noexcept
        {
            uint64_t hash = 14695981039346656037ull;
            for (const unsigned char c : value)
            {
                hash ^= c;
                hash *= 1099511628211ull;
            }
            return hash;
        }
    }

    template <typename Tag>
    struct StableId
    {
        uint64_t value{0};

        [[nodiscard]] constexpr bool valid() const noexcept
        {
            return value != 0;
        }

        constexpr auto operator<=>(const StableId&) const = default;
    };

    using ScreenId = StableId<struct ScreenIdTag>;
    using ElementId = StableId<struct ElementIdTag>;
    using LayerId = StableId<struct LayerIdTag>;
    using FontFaceId = StableId<struct FontFaceIdTag>;
    using FontFamilyId = StableId<struct FontFamilyIdTag>;
    using ModelId = StableId<struct ModelIdTag>;
    using SignalId = StableId<struct SignalIdTag>;

    template <typename Id>
    [[nodiscard]] constexpr Id make_stable_id(const std::string_view value) noexcept
    {
        return Id{ detail::fnv1a_64(value) };
    }

    [[nodiscard]] constexpr ScreenId make_screen_id(const std::string_view value) noexcept
    {
        return make_stable_id<ScreenId>(value);
    }

    [[nodiscard]] constexpr ElementId make_element_id(const std::string_view value) noexcept
    {
        return make_stable_id<ElementId>(value);
    }

    [[nodiscard]] constexpr LayerId make_layer_id(const std::string_view value) noexcept
    {
        return make_stable_id<LayerId>(value);
    }

    [[nodiscard]] constexpr FontFaceId make_font_face_id(const std::string_view value) noexcept
    {
        return make_stable_id<FontFaceId>(value);
    }

    [[nodiscard]] constexpr FontFamilyId make_font_family_id(const std::string_view value) noexcept
    {
        return make_stable_id<FontFamilyId>(value);
    }

    [[nodiscard]] constexpr ModelId make_model_id(const std::string_view value) noexcept
    {
        return make_stable_id<ModelId>(value);
    }

    [[nodiscard]] constexpr SignalId make_signal_id(const std::string_view value) noexcept
    {
        return make_stable_id<SignalId>(value);
    }

    struct Extent2D
    {
        uint32_t width{0};
        uint32_t height{0};
    };

    enum class HorizontalAnchor : uint8_t
    {
        Left = 0,
        Center = 1,
        Right = 2
    };

    enum class VerticalAnchor : uint8_t
    {
        Top = 0,
        Center = 1,
        Bottom = 2
    };

    enum class LayerInputPolicy : uint8_t
    {
        Passthrough = 0,
        Capture = 1,
        Modal = 2
    };

    enum class TextAlignment : uint8_t
    {
        Start = 0,
        Center = 1,
        End = 2
    };

    struct Anchor
    {
        HorizontalAnchor horizontal{HorizontalAnchor::Left};
        VerticalAnchor vertical{VerticalAnchor::Top};
        glm::vec2 offset{0.0f};
    };

    struct LayoutBox
    {
        glm::vec2 size{0.0f};
        Anchor anchor{};
    };

    struct Rect
    {
        glm::vec2 min{0.0f};
        glm::vec2 max{0.0f};

        [[nodiscard]] glm::vec2 size() const noexcept
        {
            return max - min;
        }
    };

    struct Style
    {
        glm::vec4 tint{1.0f};
        glm::vec2 padding{0.0f};
        float opacity{1.0f};
    };

    struct Theme
    {
        std::string debugName{};
        Style baseStyle{};
    };

    [[nodiscard]] inline Rect resolve_rect(const LayoutBox& layout, const Extent2D viewport) noexcept
    {
        const float width = static_cast<float>(viewport.width);
        const float height = static_cast<float>(viewport.height);

        glm::vec2 origin{0.0f};
        switch (layout.anchor.horizontal)
        {
        case HorizontalAnchor::Left:
            origin.x = 0.0f;
            break;
        case HorizontalAnchor::Center:
            origin.x = (width - layout.size.x) * 0.5f;
            break;
        case HorizontalAnchor::Right:
            origin.x = width - layout.size.x;
            break;
        }

        switch (layout.anchor.vertical)
        {
        case VerticalAnchor::Top:
            origin.y = 0.0f;
            break;
        case VerticalAnchor::Center:
            origin.y = (height - layout.size.y) * 0.5f;
            break;
        case VerticalAnchor::Bottom:
            origin.y = height - layout.size.y;
            break;
        }

        origin += layout.anchor.offset;
        return Rect{
            .min = origin,
            .max = origin + layout.size
        };
    }
}

namespace std
{
    template <typename Tag>
    struct hash<ui::StableId<Tag>>
    {
        size_t operator()(const ui::StableId<Tag>& id) const noexcept
        {
            return hash<uint64_t>{}(id.value);
        }
    };
}

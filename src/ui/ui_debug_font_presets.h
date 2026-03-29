#pragma once

#include <array>
#include <string_view>

#include "ui_types.h"

namespace ui
{
    struct DebugFontPreset
    {
        std::string_view label{};
        std::string_view faceKey{};
        std::string_view familyKey{};
        std::string_view fileName{};
        float nominalPixelHeight{16.0f};
    };

    inline constexpr std::array<DebugFontPreset, 6> runtime_debug_font_presets{{
        {
            .label = "Droid Sans",
            .faceKey = "runtime.font.face.droid_sans",
            .familyKey = "runtime.font.family.droid_sans",
            .fileName = "DroidSans.ttf",
            .nominalPixelHeight = 20.0f
        },
        {
            .label = "Roboto Medium",
            .faceKey = "runtime.font.face.roboto_medium",
            .familyKey = "runtime.font.family.roboto_medium",
            .fileName = "Roboto-Medium.ttf",
            .nominalPixelHeight = 20.0f
        },
        {
            .label = "Cousine Regular",
            .faceKey = "runtime.font.face.cousine_regular",
            .familyKey = "runtime.font.family.cousine_regular",
            .fileName = "Cousine-Regular.ttf",
            .nominalPixelHeight = 20.0f
        },
        {
            .label = "Karla Regular",
            .faceKey = "runtime.font.face.karla_regular",
            .familyKey = "runtime.font.family.karla_regular",
            .fileName = "Karla-Regular.ttf",
            .nominalPixelHeight = 20.0f
        },
        {
            .label = "Proggy Clean",
            .faceKey = "runtime.font.face.proggy_clean",
            .familyKey = "runtime.font.family.proggy_clean",
            .fileName = "ProggyClean.ttf",
            .nominalPixelHeight = 18.0f
        },
        {
            .label = "Proggy Tiny",
            .faceKey = "runtime.font.face.proggy_tiny",
            .familyKey = "runtime.font.family.proggy_tiny",
            .fileName = "ProggyTiny.ttf",
            .nominalPixelHeight = 16.0f
        }
    }};

    [[nodiscard]] constexpr FontFamilyId default_runtime_debug_font_family() noexcept
    {
        return make_font_family_id(runtime_debug_font_presets.front().familyKey);
    }
}

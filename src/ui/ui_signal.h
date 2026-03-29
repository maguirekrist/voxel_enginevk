#pragma once

#include <string>

#include "ui_types.h"

namespace ui
{
    enum class SignalKind : uint8_t
    {
        Generic = 0,
        Toast = 1,
        DamageFlash = 2,
        Pulse = 3
    };

    struct Signal
    {
        SignalId id{};
        SignalKind kind{SignalKind::Generic};
        ScreenId targetScreen{};
        ElementId targetElement{};
        std::string channel{};
        std::string payload{};
        float intensity{1.0f};
        double timestampSeconds{0.0};
    };
}

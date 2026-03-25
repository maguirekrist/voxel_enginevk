#pragma once

#include <SDL.h>

namespace editor_shortcuts
{
    [[nodiscard]] inline bool has_primary_modifier(const SDL_Keymod modifiers)
    {
        return (modifiers & (KMOD_CTRL | KMOD_GUI)) != 0;
    }

    [[nodiscard]] inline bool has_shift_modifier(const SDL_Keymod modifiers)
    {
        return (modifiers & KMOD_SHIFT) != 0;
    }
}

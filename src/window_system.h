#pragma once

#include <functional>

#include <SDL.h>
#include <vk_types.h>

struct WindowEventState
{
    bool quitRequested{false};
    bool resizeRequested{false};
};

class WindowSystem
{
public:
    void init(const char* title, VkExtent2D extent);
    void cleanup();

    [[nodiscard]] SDL_Window* handle() const;
    [[nodiscard]] bool is_focused() const;
    [[nodiscard]] const Uint8* keyboard_state() const;

    WindowEventState poll_events(const std::function<void(const SDL_Event&)>& sceneInputHandler);
    void sync_extent(VkExtent2D& extent) const;

private:
    SDL_Window* _window{nullptr};
    bool _focused{false};
};

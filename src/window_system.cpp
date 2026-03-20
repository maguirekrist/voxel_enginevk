#include "window_system.h"

#include "constants.h"

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"

void WindowSystem::init(const char* title, const VkExtent2D extent)
{
    SDL_Init(SDL_INIT_VIDEO);

    const SDL_WindowFlags windowFlags = static_cast<SDL_WindowFlags>(SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    _window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        extent.width,
        extent.height,
        windowFlags
    );
}

void WindowSystem::cleanup()
{
    if (_window != nullptr)
    {
        SDL_DestroyWindow(_window);
        _window = nullptr;
    }

    SDL_Quit();
    _focused = false;
}

SDL_Window* WindowSystem::handle() const
{
    return _window;
}

bool WindowSystem::is_focused() const
{
    return _focused;
}

const Uint8* WindowSystem::keyboard_state() const
{
    return SDL_GetKeyboardState(nullptr);
}

WindowEventState WindowSystem::poll_events(const std::function<void(const SDL_Event&)>& sceneInputHandler)
{
    WindowEventState eventState{};
    SDL_Event event;

    while (SDL_PollEvent(&event) != 0)
    {
        if (USE_IMGUI)
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
        }

        const bool imguiCaptureAllowed = !_focused;
        const bool imguiCapturingMouse = imguiCaptureAllowed && USE_IMGUI && ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
        const bool imguiCapturingKeyboard = imguiCaptureAllowed && USE_IMGUI && ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;

        switch (event.type)
        {
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_RESIZED)
            {
                eventState.resizeRequested = true;
            }
            break;
        case SDL_KEYDOWN:
            if (event.key.keysym.sym == SDLK_ESCAPE)
            {
                _focused = false;
                SDL_SetWindowGrab(_window, SDL_FALSE);
                SDL_SetRelativeMouseMode(SDL_FALSE);
                SDL_ShowCursor(SDL_TRUE);
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
            if (!_focused && !imguiCapturingMouse)
            {
                _focused = true;
                SDL_SetWindowGrab(_window, SDL_TRUE);
                SDL_SetRelativeMouseMode(SDL_TRUE);
                SDL_ShowCursor(SDL_FALSE);
            }
            break;
        case SDL_QUIT:
            eventState.quitRequested = true;
            break;
        default:
            break;
        }

        const bool isMouseEvent = event.type == SDL_MOUSEMOTION ||
            event.type == SDL_MOUSEBUTTONDOWN ||
            event.type == SDL_MOUSEBUTTONUP ||
            event.type == SDL_MOUSEWHEEL;
        const bool isKeyboardEvent = event.type == SDL_KEYDOWN || event.type == SDL_KEYUP || event.type == SDL_TEXTINPUT;
        const bool imguiCapturedEvent = (isMouseEvent && imguiCapturingMouse) || (isKeyboardEvent && imguiCapturingKeyboard);

        if (_focused && !imguiCapturedEvent)
        {
            sceneInputHandler(event);
        }
    }

    return eventState;
}

void WindowSystem::sync_extent(VkExtent2D& extent) const
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(_window, &width, &height);
    extent.width = static_cast<uint32_t>(width);
    extent.height = static_cast<uint32_t>(height);
}

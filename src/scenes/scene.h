#pragma once

#include "SDL_events.h"
#include <render/render_queue.h>

class Scene {
public:
    virtual void queue_objects() = 0;
    virtual void update(float deltaTime) = 0;
    virtual void handle_input(const SDL_Event& event) = 0;
    virtual void handle_keystate(const Uint8* state) = 0;
    virtual void cleanup() = 0;
    virtual void draw_imgui() = 0;

    virtual ~Scene() = default;
};
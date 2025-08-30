#pragma once

#include "SDL_events.h"

class Scene {
public:
    virtual void update_buffers() = 0;
    virtual void update(float deltaTime) = 0;
    virtual void handle_input(const SDL_Event& event) = 0;
    virtual void handle_keystate(const Uint8* state) = 0;
    virtual void draw_imgui() = 0;
    virtual void build_pipelines() = 0;
    virtual void rebuild_pipelines() = 0;

    virtual ~Scene() = default;
};
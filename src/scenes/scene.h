#pragma once

#include "SDL_events.h"

struct SceneRenderState;
namespace ui
{
    class FrameBuilder;
    class Runtime;
    class WorldLabelCollector;
    struct Signal;
}

class Scene {
public:
    virtual void update_buffers() = 0;
    virtual void update(float deltaTime) = 0;
    virtual void handle_input(const SDL_Event& event) = 0;
    virtual void handle_keystate(const Uint8* state) = 0;
    virtual void clear_input() = 0;
    virtual void draw_imgui() = 0;
    virtual void build_runtime_ui(ui::FrameBuilder& builder) = 0;
    virtual void submit_ui_signal(const ui::Signal& signal) = 0;
    virtual void collect_world_labels(ui::WorldLabelCollector& collector) = 0;
    [[nodiscard]] virtual ui::Runtime& runtime_ui() = 0;
    virtual void build_pipelines() = 0;
    virtual void rebuild_pipelines() = 0;
    virtual SceneRenderState& get_render_state() = 0;
    [[nodiscard]] virtual bool wants_mouse_capture() const = 0;

    virtual ~Scene() = default;
};

#pragma once

#include "camera.h"
#include "scene.h"
#include "render/resource.h"

struct GridUniform {
    glm::ivec2 gridSpacing;
    glm::vec3 color;
    glm::vec3 backgroundColor;
};

class BlueprintBuilderScene final : public Scene {
public:
    BlueprintBuilderScene();
    void update_buffers() override;
    void update(float deltaTime) override;
    void handle_input(const SDL_Event& event) override;
    void handle_keystate(const Uint8* state) override;
    void draw_imgui() override;
private:
    void build_chunk_platform(ChunkCoord coord);
    void set_grid_uniform();
    void update_camera_uniform();


private:
    Camera _camera;

    std::shared_ptr<Resource> _gridResource;
    std::shared_ptr<Resource> _cameraUboResource;
};


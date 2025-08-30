
#pragma once

#include <game/world.h>
#include "components/game_object.h"


struct RaycastResult {
    Block _block;
    FaceDirection _blockFace;
    glm::ivec3 _worldPos;
    float _distance;
};

class Camera final : public GameObject
{
public:
    glm::mat4 _view;
    glm::mat4 _projection;

    explicit Camera(const glm::vec3& position, VkExtent2D windowExtent);
    void update(float dt) override;
    static std::optional<RaycastResult> get_target_block(World& world, GameObject& player);
    void resize(VkExtent2D windowExtent);
};
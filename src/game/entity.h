#pragma once

#include "components/game_object.h"

class Entity : public GameObject
{
public:
    using GameObject::GameObject;
    ~Entity() override = default;

    virtual void tick(float deltaTime) = 0;
};

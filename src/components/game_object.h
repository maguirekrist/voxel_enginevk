//
// Created by Maguire Krist on 8/9/25.
//

#ifndef GAMEOBJECT_H
#define GAMEOBJECT_H
#include <atomic>
#include <array>
#include <memory>
#include <stdexcept>

#include "constants.h"
#include "glm/vec3.hpp"

struct Component
{
    virtual ~Component() = default;
};

inline std::size_t next_component_slot()
{
    static std::atomic_size_t counter{0};
    return counter++;
}

template<typename T>
std::size_t component_slot()
{
    static const std::size_t slot = next_component_slot();
    return slot;
}

class GameObject
{
public:
    virtual ~GameObject() = default;
    explicit GameObject(const glm::vec3& position) : _position(position) {}

    virtual void update(const float dt)
    {
        _moveSpeed = GameConfig::DEFAULT_MOVE_SPEED * dt;
    };

    template <std::derived_from<Component> T, typename... Args>
    T& Add(Args&&... args);
    template <std::derived_from<Component> T>
    T& Get();
    template <std::derived_from<Component> T>
    [[nodiscard]] bool Has() const;

    std::array<std::unique_ptr<Component>, MAX_COMPONENTS> _components;

    glm::vec3 _front = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 _position;
    glm::vec3 _up = glm::vec3(0.0f, 1.0f, 0.0f);
    float _yaw = 0.0f;
    float _pitch = 0.0f;
    float _moveSpeed{GameConfig::DEFAULT_MOVE_SPEED};
};

template <std::derived_from<Component> T, typename... Args>
T& GameObject::Add(Args&&... args)
{
    const std::size_t idx = component_slot<T>();
    if (idx >= MAX_COMPONENTS) {
        throw std::runtime_error("Too many components");
    }
    auto ptr = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = ptr.get();
    _components[idx] = std::move(ptr);
    return *raw;
}

template <std::derived_from<Component> T>
T& GameObject::Get()
{
    const std::size_t idx = component_slot<T>();
    if (idx >= MAX_COMPONENTS)
    {
        throw std::runtime_error("Too many components");
    }

    if (!_components[idx])
    {
        throw std::runtime_error("Component not found");
    }

    Component* base = _components[idx].get();
    if (!base)
    {
        throw std::runtime_error("Component not found");
    }

    return static_cast<T&>(*base);
}

template <std::derived_from<Component> T>
bool GameObject::Has() const
{
    const std::size_t idx = component_slot<T>();
    if (idx >= MAX_COMPONENTS)
    {
        return false;
    }

    if (!_components[idx])
    {
        return false;
    }

    return true;
}

template<std::derived_from<Component> T>
struct ComponentIter
{
    using ObjectList = std::vector<std::unique_ptr<GameObject>>;
    ObjectList& _objects;

    struct Item
    {
        GameObject& go;
        T& component;
    };

    struct It
    {
        ObjectList* list{};
        std::size_t index{};

        void advance()
        {
            const std::size_t n = list ? list->size() : 0;
            while (index < n)
            {
                auto* ptr = list->at(index).get();
                if (ptr && ptr->template Has<T>())
                {
                    break;
                }
                ++index;
            }
        }
        Item operator*() const
        {
            const auto& go = list->at(index);
            return { *go, go->template Get<T>() };
        }
        It& operator++()
        {
            ++index;
            advance();
            return *this;
        }
        //My guess is this is supposed to check, if the iterator is at the same index?
        bool operator!=(const It& other) const
        {
            return list != other.list || index != other.index;
        }
    };

    explicit ComponentIter(ObjectList& objects) : _objects(objects) {}

    It begin()
    {
        It it { &_objects, 0 };
        it.advance();
        return it;
    }

    It end()
    {
        return It{ &_objects, _objects.size() };
    }
};



#endif //GAMEOBJECT_H

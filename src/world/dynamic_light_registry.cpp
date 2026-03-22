#include "dynamic_light_registry.h"

#include <algorithm>

namespace world_lighting
{
    DynamicLightRegistry::LightId DynamicLightRegistry::create(const DynamicPointLight& light)
    {
        const LightId id = _nextId++;
        DynamicPointLight stored = light;
        stored.id = id;
        _lightsById.insert_or_assign(id, stored);
        rebuild_snapshot();
        return id;
    }

    bool DynamicLightRegistry::update(const LightId id, const DynamicPointLight& light)
    {
        const auto it = _lightsById.find(id);
        if (it == _lightsById.end())
        {
            return false;
        }

        DynamicPointLight stored = light;
        stored.id = id;
        it->second = stored;
        rebuild_snapshot();
        return true;
    }

    bool DynamicLightRegistry::remove(const LightId id)
    {
        if (_lightsById.erase(id) == 0)
        {
            return false;
        }

        rebuild_snapshot();
        return true;
    }

    void DynamicLightRegistry::clear() noexcept
    {
        _lightsById.clear();
        _snapshot.clear();
    }

    std::optional<DynamicPointLight> DynamicLightRegistry::find(const LightId id) const
    {
        if (const auto it = _lightsById.find(id); it != _lightsById.end())
        {
            return it->second;
        }

        return std::nullopt;
    }

    const std::vector<DynamicPointLight>& DynamicLightRegistry::snapshot() const noexcept
    {
        return _snapshot;
    }

    size_t DynamicLightRegistry::active_light_count() const noexcept
    {
        return _snapshot.size();
    }

    void DynamicLightRegistry::rebuild_snapshot()
    {
        _snapshot.clear();
        _snapshot.reserve(_lightsById.size());

        for (const auto& [id, light] : _lightsById)
        {
            (void)id;
            if (!light.active || light.radius <= 0.0f || light.intensity <= 0.0f)
            {
                continue;
            }

            _snapshot.push_back(light);
        }

        std::sort(_snapshot.begin(), _snapshot.end(), [](const DynamicPointLight& lhs, const DynamicPointLight& rhs)
        {
            return lhs.id < rhs.id;
        });
    }
}

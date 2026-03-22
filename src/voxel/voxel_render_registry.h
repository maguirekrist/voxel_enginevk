#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>

#include "collections/spare_set.h"
#include "render/scene_render_state.h"
#include "voxel_render_instance.h"

class MaterialManager;
class MeshManager;

class VoxelRenderRegistry
{
public:
    using InstanceId = uint64_t;

    [[nodiscard]] InstanceId add_instance(const VoxelRenderInstance& instance);
    bool update_instance(InstanceId id, const VoxelRenderInstance& instance);
    [[nodiscard]] VoxelRenderInstance* get_instance(InstanceId id);
    [[nodiscard]] const VoxelRenderInstance* get_instance(InstanceId id) const;
    bool remove_instance(InstanceId id, SceneRenderState& renderState);
    void clear(SceneRenderState& renderState);
    void sync(MeshManager& meshManager, MaterialManager& materialManager, SceneRenderState& renderState);
    [[nodiscard]] size_t instance_count() const noexcept;

private:
    struct Entry
    {
        VoxelRenderInstance instance{};
        bool uploadRequested{false};
        std::optional<RenderLayer> submittedLayer{};
        std::optional<dev_collections::sparse_set<RenderObject>::Handle> renderHandle{};
    };

    void remove_render_object(Entry& entry, SceneRenderState& renderState);
    [[nodiscard]] dev_collections::sparse_set<RenderObject>& render_bucket(SceneRenderState& renderState, RenderLayer layer) const;
    [[nodiscard]] const dev_collections::sparse_set<RenderObject>& render_bucket(const SceneRenderState& renderState, RenderLayer layer) const;

    InstanceId _nextId{1};
    std::unordered_map<InstanceId, Entry> _entries{};
};

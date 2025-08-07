#pragma once

#include <vk_mesh.h>
#include <collections/spare_set.h>

enum class RenderLayer {
    Opaque,
    Transparent
};

struct RenderObject {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    glm::ivec2 xzPos;
    RenderLayer layer;
    dev_collections::sparse_set<RenderObject>::Handle handle;
};

// struct Handle
// {
//     uint32_t id;
//     uint32_t generation;
// };
//
// struct Slot
// {
//     uint32_t dense_index;
//     uint32_t generation;
// };


// class RenderSet {
//     std::vector<RenderObject> _dense{};
//     std::vector<Slot> sparse{};
//     std::vector<uint32_t> free_ids{};
// public:
//
//     Handle create(RenderObject&& obj);
//     bool destroy(Handle h);
//
//     void clear() {
//         sparse.clear();
//         free_ids.clear();
//         _dense.clear();
//     }
//
//     [[nodiscard]] const std::vector<RenderObject>& getSet() const noexcept {
//         return _dense;
//     }
// };
//
// inline Handle RenderSet::create(RenderObject&& obj)
// {
//     uint32_t id;
//     if (!free_ids.empty())
//     {
//         id = free_ids.back();
//         free_ids.pop_back();
//     }
//     else
//     {
//         id = static_cast<uint32_t>(sparse.size());
//         sparse.push_back({});
//     }
//
//
//     auto dense_index = static_cast<uint32_t>(_dense.size());
//     _dense.push_back(std::move(obj));
//
//     sparse[id].dense_index = dense_index;
//
//     return Handle{ id, sparse[id].generation };
// }
//
// inline bool RenderSet::destroy(Handle h)
// {
//
//     if (h.id >= sparse.size()) return false;
//     Slot& s = sparse[h.id];
//     if (s.generation != h.generation) return false; // stale handle
//
//     uint32_t di = s.dense_index;
//     auto last = static_cast<uint32_t>(_dense.size() - 1);
//     if (di != last) {
//         std::swap(_dense[di], _dense[last]);               // swap victim with last
//         // find the moved object's id and fix its sparse index
//         // Suppose RenderObject stores its own Handle, or you keep a side map:
//
//         uint32_t movedId = _dense[last].handle.id;
//         sparse[movedId].dense_index = di;
//     }
//     _dense.pop_back();
//
//     // retire i d
//     ++s.generation;           // invalidate old handles
//     free_ids.push_back(h.id);
//     return true;
// }

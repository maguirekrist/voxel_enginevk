#pragma once

#include <vk_types.h>
#include <collections/spare_set.h>

struct Mesh;
struct Material;

enum class RenderLayer {
    Opaque,
    Transparent
};

enum class LightingMode : uint32_t
{
    BakedChunk = 0,
    SampledRuntime = 1,
    BakedPlusDynamic = 2,
    Unlit = 3
};

struct SampledLightPayload
{
    glm::vec3 localLight{0.0f};
    float sunlight{1.0f};
    glm::vec3 dynamicLight{0.0f};
};

struct ObjectPushConstants {
    glm::mat4 modelMatrix{1.0f};
    glm::vec4 sampledLocalLightAndSunlight{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 sampledDynamicLightAndMode{0.0f};
};

struct RenderObject {
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    glm::mat4 transform{1.0f};
    RenderLayer layer{RenderLayer::Opaque};
    LightingMode lightingMode{LightingMode::BakedChunk};
    SampledLightPayload sampledLight{};
    //dev_collections::sparse_set<RenderObject>::Handle handle;
};

struct PushConstant {
    VkShaderStageFlags stageFlags;
    uint32_t size;
    std::function<ObjectPushConstants(const RenderObject&)> build_constant;
};

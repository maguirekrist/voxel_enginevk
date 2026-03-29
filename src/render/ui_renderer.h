#pragma once

#include <memory>
#include <unordered_map>

#include "resource.h"
#include "ui/ui_runtime.h"
#include "vk_types.h"

namespace vkutil
{
    class DescriptorAllocator;
    class DescriptorLayoutCache;
}

class UiRenderer
{
public:
    struct Context
    {
        VkDevice device{VK_NULL_HANDLE};
        VmaAllocator allocator{};
        VkRenderPass* renderPass{};
        VkExtent2D* windowExtent{};
        vkutil::DescriptorAllocator* descriptorAllocator{};
        vkutil::DescriptorLayoutCache* descriptorLayoutCache{};
        QueueFamily uploadQueue{};
    };

    void init(const Context& context);
    void cleanup();
    void draw_runtime_ui(VkCommandBuffer cmd, const ui::Runtime& runtime);

private:
    struct UiVertex
    {
        glm::vec2 position{0.0f};
        glm::vec2 uv{0.0f};
        glm::vec4 color{1.0f};
    };

    struct AtlasKey
    {
        ui::FontFaceId faceId{};
        ui::FontAtlasType atlasType{ui::FontAtlasType::MultiChannelSignedDistanceField};

        auto operator<=>(const AtlasKey&) const = default;
    };

    struct AtlasKeyHash
    {
        size_t operator()(const AtlasKey& key) const noexcept
        {
            const size_t faceHash = std::hash<ui::FontFaceId>{}(key.faceId);
            const size_t atlasHash = std::hash<uint8_t>{}(static_cast<uint8_t>(key.atlasType));
            return faceHash ^ (atlasHash << 1);
        }
    };

    struct AtlasGpuResource
    {
        std::shared_ptr<Resource> image{};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        int width{0};
        int height{0};
        int channels{0};
        size_t pixelCount{0};
        size_t glyphCount{0};
        size_t contentHash{0};
    };

    struct DrawBatch
    {
        AtlasKey atlasKey{};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        uint32_t firstIndex{0};
        uint32_t indexCount{0};
    };

    struct PushConstants
    {
        glm::vec2 viewportSize{1.0f};
        glm::vec2 padding{0.0f};
    };

    void ensure_pipeline();
    void rebuild_pipeline();
    void destroy_pipeline();
    void ensure_geometry_capacity(size_t vertexCount, size_t indexCount);
    void upload_geometry(std::span<const UiVertex> vertices, std::span<const uint32_t> indices);
    [[nodiscard]] AtlasGpuResource* ensure_atlas_resource(const ui::GeneratedFontAtlas& atlas);
    [[nodiscard]] std::shared_ptr<Resource> create_atlas_image_resource(const ui::GeneratedFontAtlas& atlas) const;
    [[nodiscard]] VkDescriptorSet allocate_atlas_descriptor_set(const Resource& imageResource);
    void immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) const;
    void copy_font_bitmap_rgba(const ui::GeneratedFontAtlas& atlas, std::vector<uint8_t>& rgbaPixels) const;

    Context _context{};
    UploadContext _uploadContext{};
    VkSampler _atlasSampler{VK_NULL_HANDLE};
    VkPipeline _pipeline{VK_NULL_HANDLE};
    VkPipelineLayout _pipelineLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout _descriptorSetLayout{VK_NULL_HANDLE};
    std::shared_ptr<Resource> _vertexBuffer{};
    std::shared_ptr<Resource> _indexBuffer{};
    size_t _vertexCapacity{0};
    size_t _indexCapacity{0};
    VkExtent2D _pipelineExtent{};
    std::unordered_map<AtlasKey, AtlasGpuResource, AtlasKeyHash> _atlasResources{};
};

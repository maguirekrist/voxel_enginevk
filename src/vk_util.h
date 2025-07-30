#pragma once

#include <vk_types.h>

namespace vkutil {

    //Transitions a written image to a image ready to be procesed by a compute shader
    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout targetLayout);
    bool load_shader_module(const std::string& filePath, VkDevice device, VkShaderModule* outShaderModule);
    AllocatedBuffer create_buffer(VmaAllocator allocator, size_t size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memUsage);
    AllocatedImage create_image(VmaAllocator allocator, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, VmaMemoryUsage memUsage);

    class DescriptorAllocator {
    public:
        struct PoolSizes {
            std::vector<std::pair<VkDescriptorType,float>> sizes =
            {
                { VK_DESCRIPTOR_TYPE_SAMPLER, 0.5f },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4.f },
                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4.f },
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.f },
                { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1.f },
                { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1.f },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2.f },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2.f },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1.f },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1.f },
                { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 0.5f }
            };
        };

        void reset_pools();
        bool allocate(VkDescriptorSet* set, VkDescriptorSetLayout layout);
        void init(VkDevice newDevice);

        void cleanup();
        VkDescriptorPool grab_pool();
        VkDevice device;
    private:

        VkDescriptorPool currentPool{ VK_NULL_HANDLE };
        PoolSizes descriptorSizes;
        std::vector<VkDescriptorPool> usedPools;
        std::vector<VkDescriptorPool> freePools;
    };

    class DescriptorLayoutCache {
    public:
            void init(VkDevice newDevice);
            void cleanup();

            VkDescriptorSetLayout create_descriptor_layout(VkDescriptorSetLayoutCreateInfo* info);

            struct DescriptorLayoutInfo {
                //good idea to turn this into a inlined array
                std::vector<VkDescriptorSetLayoutBinding> bindings;

                bool operator==(const DescriptorLayoutInfo& other) const;

                size_t hash() const;
            };
    private:

        struct DescriptorLayoutHash		{

            std::size_t operator()(const DescriptorLayoutInfo& k) const{
                return k.hash();
            }
        };

        std::unordered_map<DescriptorLayoutInfo, VkDescriptorSetLayout, DescriptorLayoutHash> layoutCache;
        VkDevice device;
    };

    class DescriptorBuilder {
    public:
        static DescriptorBuilder begin(DescriptorLayoutCache* layoutCache, DescriptorAllocator* allocator );

        DescriptorBuilder& bind_buffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo, VkDescriptorType type, VkShaderStageFlags stageFlags);
        DescriptorBuilder& bind_image(uint32_t binding, VkDescriptorImageInfo* imageInfo, VkDescriptorType type, VkShaderStageFlags stageFlags);

        bool build(VkDescriptorSet& set, VkDescriptorSetLayout& layout);
        bool build(VkDescriptorSet& set);
    private:

        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorSetLayoutBinding> bindings;

        DescriptorLayoutCache* cache;
        DescriptorAllocator* alloc;
    };
};

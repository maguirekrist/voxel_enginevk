#pragma once

#include "vk_types.h"

namespace vkutil {

    //Transitions a written image to a image ready to be procesed by a compute shader
    void transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout targetLayout)
    {
        // Define an image memory barrier
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = currentLayout; // The layout used in the off-screen render pass
        barrier.newLayout = targetLayout; // The layout for compute shader access
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;

        // Define the aspect mask based on the image format (assumed to be color here)
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        // Set up access masks for synchronization
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // Previous access mask
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; // Access needed for compute shader

        // Insert the pipeline barrier
        vkCmdPipelineBarrier(
            cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // Previous stage
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,          // Stage where the compute shader reads the image
            0,                                             // No additional flags
            0, nullptr,                                    // No memory barriers
            0, nullptr,                                    // No buffer barriers
            1, &barrier                                    // One image memory barrier
        );
    }

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

        VkDevice device;
    private:
        VkDescriptorPool grab_pool();

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

#include "ui_renderer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

#include "shader_program.h"
#include "vk_initializers.h"
#include "vk_pipeline_builder.h"
#include "vk_util.h"

namespace
{
    [[nodiscard]] VkFormat atlas_format()
    {
        return VK_FORMAT_R8G8B8A8_UNORM;
    }

    [[nodiscard]] size_t hash_combine(const size_t seed, const size_t value)
    {
        return seed ^ (value + 0x9e3779b9u + (seed << 6) + (seed >> 2));
    }

    [[nodiscard]] size_t atlas_content_hash(const ui::GeneratedFontAtlas& atlas)
    {
        size_t hash = 1469598103934665603ull;
        hash = hash_combine(hash, std::hash<int>{}(atlas.bitmap.width));
        hash = hash_combine(hash, std::hash<int>{}(atlas.bitmap.height));
        hash = hash_combine(hash, std::hash<int>{}(atlas.bitmap.channels));
        hash = hash_combine(hash, std::hash<size_t>{}(atlas.bitmap.pixels.size()));
        hash = hash_combine(hash, std::hash<size_t>{}(atlas.glyphs.size()));

        for (const uint8_t byte : atlas.bitmap.pixels)
        {
            hash = hash_combine(hash, std::hash<uint8_t>{}(byte));
        }

        for (const ui::FontAtlasGlyph& glyph : atlas.glyphs)
        {
            hash = hash_combine(hash, std::hash<uint32_t>{}(glyph.codePoint));
            hash = hash_combine(hash, std::hash<uint32_t>{}(glyph.glyphIndex));
            hash = hash_combine(hash, std::hash<float>{}(glyph.advance));
            hash = hash_combine(hash, std::hash<float>{}(glyph.planeBounds.x));
            hash = hash_combine(hash, std::hash<float>{}(glyph.planeBounds.y));
            hash = hash_combine(hash, std::hash<float>{}(glyph.planeBounds.z));
            hash = hash_combine(hash, std::hash<float>{}(glyph.planeBounds.w));
            hash = hash_combine(hash, std::hash<float>{}(glyph.atlasBounds.x));
            hash = hash_combine(hash, std::hash<float>{}(glyph.atlasBounds.y));
            hash = hash_combine(hash, std::hash<float>{}(glyph.atlasBounds.z));
            hash = hash_combine(hash, std::hash<float>{}(glyph.atlasBounds.w));
        }

        return hash;
    }
}

void UiRenderer::init(const Context& context)
{
    _context = context;

    VkFenceCreateInfo fenceInfo = vkinit::fence_create_info();
    VK_CHECK(vkCreateFence(_context.device, &fenceInfo, nullptr, &_uploadContext._uploadFence));

    VkCommandPoolCreateInfo poolInfo = vkinit::command_pool_create_info(_context.uploadQueue._queueFamily);
    VK_CHECK(vkCreateCommandPool(_context.device, &poolInfo, nullptr, &_uploadContext._commandPool));

    VkCommandBufferAllocateInfo cmdAllocInfo = vkinit::command_buffer_allocate_info(_uploadContext._commandPool, 1);
    VK_CHECK(vkAllocateCommandBuffers(_context.device, &cmdAllocInfo, &_uploadContext._commandBuffer));

    VkSamplerCreateInfo samplerInfo = vkinit::sampler_create_info();
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(_context.device, &samplerInfo, nullptr, &_atlasSampler));
}

void UiRenderer::cleanup()
{
    _atlasResources.clear();
    _vertexBuffer.reset();
    _indexBuffer.reset();
    destroy_pipeline();

    if (_atlasSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(_context.device, _atlasSampler, nullptr);
        _atlasSampler = VK_NULL_HANDLE;
    }

    if (_uploadContext._commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(_context.device, _uploadContext._commandPool, nullptr);
        _uploadContext._commandPool = VK_NULL_HANDLE;
    }

    if (_uploadContext._uploadFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(_context.device, _uploadContext._uploadFence, nullptr);
        _uploadContext._uploadFence = VK_NULL_HANDLE;
    }

    _uploadContext._commandBuffer = VK_NULL_HANDLE;
    _pipelineExtent = {};
}

void UiRenderer::draw_runtime_ui(const VkCommandBuffer cmd, const ui::Runtime& runtime)
{
    const auto& preparedText = runtime.current_frame().preparedTextCommands;
    if (preparedText.empty())
    {
        return;
    }

    ensure_pipeline();

    std::vector<UiVertex> vertices{};
    std::vector<uint32_t> indices{};
    std::vector<DrawBatch> batches{};

    vertices.reserve(preparedText.size() * 4);
    indices.reserve(preparedText.size() * 6);

    std::optional<AtlasKey> activeAtlas{};
    DrawBatch* activeBatch = nullptr;

    for (const ui::PreparedTextCommand& command : preparedText)
    {
        const ui::GeneratedFontAtlas* atlas = runtime.text_system().find_generated_atlas(command.faceId, command.atlasType);
        if (atlas == nullptr)
        {
            continue;
        }

        AtlasGpuResource* atlasResource = ensure_atlas_resource(*atlas);
        if (atlasResource == nullptr || atlasResource->descriptorSet == VK_NULL_HANDLE)
        {
            continue;
        }

        const AtlasKey atlasKey{
            .faceId = command.faceId,
            .atlasType = command.atlasType
        };

        if (!activeAtlas.has_value() || activeAtlas.value() != atlasKey)
        {
            batches.push_back(DrawBatch{
                .atlasKey = atlasKey,
                .descriptorSet = atlasResource->descriptorSet,
                .firstIndex = static_cast<uint32_t>(indices.size()),
                .indexCount = 0
            });
            activeAtlas = atlasKey;
            activeBatch = &batches.back();
        }

        for (const ui::PreparedGlyphQuad& glyph : command.glyphQuads)
        {
            const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
            vertices.push_back(UiVertex{
                .position = glm::vec2(glyph.rect.min.x, glyph.rect.min.y),
                .uv = glm::vec2(glyph.uvBounds.x, glyph.uvBounds.y),
                .color = command.color
            });
            vertices.push_back(UiVertex{
                .position = glm::vec2(glyph.rect.max.x, glyph.rect.min.y),
                .uv = glm::vec2(glyph.uvBounds.z, glyph.uvBounds.y),
                .color = command.color
            });
            vertices.push_back(UiVertex{
                .position = glm::vec2(glyph.rect.max.x, glyph.rect.max.y),
                .uv = glm::vec2(glyph.uvBounds.z, glyph.uvBounds.w),
                .color = command.color
            });
            vertices.push_back(UiVertex{
                .position = glm::vec2(glyph.rect.min.x, glyph.rect.max.y),
                .uv = glm::vec2(glyph.uvBounds.x, glyph.uvBounds.w),
                .color = command.color
            });

            indices.push_back(baseVertex + 0);
            indices.push_back(baseVertex + 1);
            indices.push_back(baseVertex + 2);
            indices.push_back(baseVertex + 0);
            indices.push_back(baseVertex + 2);
            indices.push_back(baseVertex + 3);

            activeBatch->indexCount += 6;
        }
    }

    if (vertices.empty() || indices.empty() || batches.empty())
    {
        return;
    }

    ensure_geometry_capacity(vertices.size(), indices.size());
    upload_geometry(vertices, indices);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipeline);

    const VkBuffer vertexBuffer = _vertexBuffer->value.buffer._buffer;
    const VkBuffer indexBuffer = _indexBuffer->value.buffer._buffer;
    constexpr VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &vertexOffset);
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    const PushConstants pushConstants{
        .viewportSize = glm::vec2(static_cast<float>(_context.windowExtent->width), static_cast<float>(_context.windowExtent->height))
    };
    vkCmdPushConstants(cmd, _pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pushConstants);

    for (const DrawBatch& batch : batches)
    {
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            _pipelineLayout,
            0,
            1,
            &batch.descriptorSet,
            0,
            nullptr);
        vkCmdDrawIndexed(cmd, batch.indexCount, 1, batch.firstIndex, 0, 0);
    }
}

void UiRenderer::ensure_pipeline()
{
    if (_pipeline != VK_NULL_HANDLE &&
        _pipelineExtent.width == _context.windowExtent->width &&
        _pipelineExtent.height == _context.windowExtent->height)
    {
        return;
    }

    rebuild_pipeline();
}

void UiRenderer::rebuild_pipeline()
{
    destroy_pipeline();

    ShaderProgram shaderProgram = ShaderProgram::load_graphics(_context.device, "ui_text.vert.spv", "ui_text.frag.spv");
    const std::vector<VkDescriptorSetLayout> descriptorLayouts = shaderProgram.create_descriptor_set_layouts(*_context.descriptorLayoutCache);
    if (!descriptorLayouts.empty())
    {
        _descriptorSetLayout = descriptorLayouts.front();
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = vkinit::pipeline_layout_create_info();
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorLayouts.size());
    pipelineLayoutInfo.pSetLayouts = descriptorLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(shaderProgram.push_constant_ranges().size());
    pipelineLayoutInfo.pPushConstantRanges = shaderProgram.push_constant_ranges().data();
    VK_CHECK(vkCreatePipelineLayout(_context.device, &pipelineLayoutInfo, nullptr, &_pipelineLayout));

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(UiVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributes{};
    attributes[0] = VkVertexInputAttributeDescription{
        .location = 0,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = offsetof(UiVertex, position)
    };
    attributes[1] = VkVertexInputAttributeDescription{
        .location = 1,
        .binding = 0,
        .format = VK_FORMAT_R32G32_SFLOAT,
        .offset = offsetof(UiVertex, uv)
    };
    attributes[2] = VkVertexInputAttributeDescription{
        .location = 2,
        .binding = 0,
        .format = VK_FORMAT_R32G32B32A32_SFLOAT,
        .offset = offsetof(UiVertex, color)
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = vkinit::vertex_input_state_create_info();
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &binding;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributes.data();

    PipelineBuilder pipelineBuilder{};
    pipelineBuilder._vertexInputInfo = vertexInputInfo;
    pipelineBuilder._inputAssembly = vkinit::input_assembly_create_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    pipelineBuilder._viewport = VkViewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(_context.windowExtent->width),
        .height = static_cast<float>(_context.windowExtent->height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    pipelineBuilder._scissor = VkRect2D{
        .offset = { 0, 0 },
        .extent = *_context.windowExtent
    };
    pipelineBuilder._rasterizer = vkinit::rasterization_state_create_info(VK_POLYGON_MODE_FILL);
    pipelineBuilder._multisampling = vkinit::multisampling_state_create_info();
    pipelineBuilder._colorBlendAttachment = vkinit::color_blend_attachment_state_blending();
    pipelineBuilder._depthStencil = vkinit::depth_stencil_create_info(false, false, VK_COMPARE_OP_ALWAYS);
    pipelineBuilder._pipelineLayout = _pipelineLayout;
    pipelineBuilder._shaderStages = shaderProgram.shader_stages();

    _pipeline = pipelineBuilder.build_pipeline(_context.device, *_context.renderPass);
    _pipelineExtent = *_context.windowExtent;
}

void UiRenderer::destroy_pipeline()
{
    if (_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(_context.device, _pipeline, nullptr);
        _pipeline = VK_NULL_HANDLE;
    }

    if (_pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(_context.device, _pipelineLayout, nullptr);
        _pipelineLayout = VK_NULL_HANDLE;
    }

    _descriptorSetLayout = VK_NULL_HANDLE;
}

void UiRenderer::ensure_geometry_capacity(const size_t vertexCount, const size_t indexCount)
{
    const size_t requiredVertexBytes = std::max<size_t>(sizeof(UiVertex) * vertexCount, sizeof(UiVertex) * 4);
    if (_vertexBuffer == nullptr || requiredVertexBytes > _vertexCapacity)
    {
        _vertexCapacity = std::bit_ceil(requiredVertexBytes);
        _vertexBuffer = std::make_shared<Resource>(
            ResourceBackendContext{
                .device = _context.device,
                .allocator = _context.allocator
            },
            Resource::BUFFER,
            Resource::ResourceValue(vkutil::create_buffer(
                _context.allocator,
                _vertexCapacity,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU)));
    }

    const size_t requiredIndexBytes = std::max<size_t>(sizeof(uint32_t) * indexCount, sizeof(uint32_t) * 6);
    if (_indexBuffer == nullptr || requiredIndexBytes > _indexCapacity)
    {
        _indexCapacity = std::bit_ceil(requiredIndexBytes);
        _indexBuffer = std::make_shared<Resource>(
            ResourceBackendContext{
                .device = _context.device,
                .allocator = _context.allocator
            },
            Resource::BUFFER,
            Resource::ResourceValue(vkutil::create_buffer(
                _context.allocator,
                _indexCapacity,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VMA_MEMORY_USAGE_CPU_TO_GPU)));
    }
}

void UiRenderer::upload_geometry(const std::span<const UiVertex> vertices, const std::span<const uint32_t> indices)
{
    void* vertexData = nullptr;
    vmaMapMemory(_context.allocator, _vertexBuffer->value.buffer._allocation, &vertexData);
    std::memcpy(vertexData, vertices.data(), vertices.size_bytes());
    vmaUnmapMemory(_context.allocator, _vertexBuffer->value.buffer._allocation);

    void* indexData = nullptr;
    vmaMapMemory(_context.allocator, _indexBuffer->value.buffer._allocation, &indexData);
    std::memcpy(indexData, indices.data(), indices.size_bytes());
    vmaUnmapMemory(_context.allocator, _indexBuffer->value.buffer._allocation);
}

UiRenderer::AtlasGpuResource* UiRenderer::ensure_atlas_resource(const ui::GeneratedFontAtlas& atlas)
{
    const AtlasKey key{
        .faceId = atlas.faceId,
        .atlasType = atlas.atlasType
    };

    auto [it, inserted] = _atlasResources.try_emplace(key);
    AtlasGpuResource& resource = it->second;
    const size_t contentHash = atlas_content_hash(atlas);
    const bool changed =
        inserted ||
        resource.image == nullptr ||
        resource.width != atlas.bitmap.width ||
        resource.height != atlas.bitmap.height ||
        resource.channels != atlas.bitmap.channels ||
        resource.pixelCount != atlas.bitmap.pixels.size() ||
        resource.glyphCount != atlas.glyphs.size() ||
        resource.contentHash != contentHash;

    if (!changed)
    {
        return &resource;
    }

    resource.image = create_atlas_image_resource(atlas);
    resource.descriptorSet = allocate_atlas_descriptor_set(*resource.image);
    resource.width = atlas.bitmap.width;
    resource.height = atlas.bitmap.height;
    resource.channels = atlas.bitmap.channels;
    resource.pixelCount = atlas.bitmap.pixels.size();
    resource.glyphCount = atlas.glyphs.size();
    resource.contentHash = contentHash;
    return &resource;
}

std::shared_ptr<Resource> UiRenderer::create_atlas_image_resource(const ui::GeneratedFontAtlas& atlas) const
{
    std::vector<uint8_t> rgbaPixels{};
    copy_font_bitmap_rgba(atlas, rgbaPixels);

    const VkDeviceSize uploadSize = static_cast<VkDeviceSize>(rgbaPixels.size());
    const AllocatedBuffer stagingBuffer = vkutil::create_buffer(
        _context.allocator,
        uploadSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_ONLY);

    void* mapped = nullptr;
    vmaMapMemory(_context.allocator, stagingBuffer._allocation, &mapped);
    std::memcpy(mapped, rgbaPixels.data(), rgbaPixels.size());
    vmaUnmapMemory(_context.allocator, stagingBuffer._allocation);

    ImageResource imageResource{};
    imageResource.image = vkutil::create_image(
        _context.allocator,
        VkExtent3D{
            .width = static_cast<uint32_t>(atlas.bitmap.width),
            .height = static_cast<uint32_t>(atlas.bitmap.height),
            .depth = 1
        },
        atlas_format(),
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY);

    VkImageViewCreateInfo viewInfo = vkinit::imageview_create_info(atlas_format(), imageResource.image._image, VK_IMAGE_ASPECT_COLOR_BIT);
    VK_CHECK(vkCreateImageView(_context.device, &viewInfo, nullptr, &imageResource.view));
    imageResource.sampler = VK_NULL_HANDLE;

    immediate_submit([&](const VkCommandBuffer cmd)
    {
        const VkImageMemoryBarrier toTransfer = vkinit::make_image_barrier({
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .image = imageResource.image._image,
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
        });
        vkinit::cmd_image_barrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, toTransfer);

        VkBufferImageCopy copyRegion{};
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageExtent = VkExtent3D{
            .width = static_cast<uint32_t>(atlas.bitmap.width),
            .height = static_cast<uint32_t>(atlas.bitmap.height),
            .depth = 1
        };
        vkCmdCopyBufferToImage(
            cmd,
            stagingBuffer._buffer,
            imageResource.image._image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copyRegion);

        const VkImageMemoryBarrier toSample = vkinit::make_image_barrier({
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .image = imageResource.image._image,
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
        });
        vkinit::cmd_image_barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, toSample);
    });

    vmaDestroyBuffer(_context.allocator, stagingBuffer._buffer, stagingBuffer._allocation);

    return std::make_shared<Resource>(
        ResourceBackendContext{
            .device = _context.device,
            .allocator = _context.allocator
        },
        Resource::IMAGE,
        Resource::ResourceValue(imageResource));
}

VkDescriptorSet UiRenderer::allocate_atlas_descriptor_set(const Resource& imageResource)
{
    if (_descriptorSetLayout == VK_NULL_HANDLE)
    {
        ensure_pipeline();
    }

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (!_context.descriptorAllocator->allocate(&descriptorSet, _descriptorSetLayout))
    {
        throw std::runtime_error("Failed to allocate UI atlas descriptor set");
    }

    const VkDescriptorImageInfo imageInfo{
        .sampler = _atlasSampler,
        .imageView = imageResource.value.image.view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    const VkWriteDescriptorSet write{
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = descriptorSet,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo
    };
    vkUpdateDescriptorSets(_context.device, 1, &write, 0, nullptr);
    return descriptorSet;
}

void UiRenderer::immediate_submit(std::function<void(VkCommandBuffer cmd)>&& function) const
{
    const VkCommandBuffer cmd = _uploadContext._commandBuffer;
    const VkCommandBufferBeginInfo beginInfo = vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    function(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBuffer submitCommandBuffer = _uploadContext._commandBuffer;
    const VkSubmitInfo submit = vkinit::submit_info(&submitCommandBuffer);
    VK_CHECK(vkQueueSubmit(_context.uploadQueue._queue, 1, &submit, _uploadContext._uploadFence));
    vkWaitForFences(_context.device, 1, &_uploadContext._uploadFence, VK_TRUE, 9999999999);
    vkResetFences(_context.device, 1, &_uploadContext._uploadFence);
    vkResetCommandPool(_context.device, _uploadContext._commandPool, 0);
}

void UiRenderer::copy_font_bitmap_rgba(const ui::GeneratedFontAtlas& atlas, std::vector<uint8_t>& rgbaPixels) const
{
    const size_t pixelCount = static_cast<size_t>(atlas.bitmap.width) * static_cast<size_t>(atlas.bitmap.height);
    rgbaPixels.resize(pixelCount * 4);

    const int width = atlas.bitmap.width;
    const int height = atlas.bitmap.height;

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const size_t sourcePixelIndex =
                static_cast<size_t>(y) * static_cast<size_t>(width) +
                static_cast<size_t>(x);
            const size_t destinationPixelIndex =
                static_cast<size_t>(y) * static_cast<size_t>(width) +
                static_cast<size_t>(x);
            const size_t sourceBase = sourcePixelIndex * static_cast<size_t>(atlas.bitmap.channels);
            const size_t destinationBase = destinationPixelIndex * 4;

            switch (atlas.bitmap.channels)
            {
            case 1:
                rgbaPixels[destinationBase + 0] = atlas.bitmap.pixels[sourceBase];
                rgbaPixels[destinationBase + 1] = atlas.bitmap.pixels[sourceBase];
                rgbaPixels[destinationBase + 2] = atlas.bitmap.pixels[sourceBase];
                rgbaPixels[destinationBase + 3] = 255;
                break;
            case 3:
                rgbaPixels[destinationBase + 0] = atlas.bitmap.pixels[sourceBase + 0];
                rgbaPixels[destinationBase + 1] = atlas.bitmap.pixels[sourceBase + 1];
                rgbaPixels[destinationBase + 2] = atlas.bitmap.pixels[sourceBase + 2];
                rgbaPixels[destinationBase + 3] = 255;
                break;
            case 4:
                rgbaPixels[destinationBase + 0] = atlas.bitmap.pixels[sourceBase + 0];
                rgbaPixels[destinationBase + 1] = atlas.bitmap.pixels[sourceBase + 1];
                rgbaPixels[destinationBase + 2] = atlas.bitmap.pixels[sourceBase + 2];
                rgbaPixels[destinationBase + 3] = atlas.bitmap.pixels[sourceBase + 3];
                break;
            default:
                rgbaPixels[destinationBase + 0] = 255;
                rgbaPixels[destinationBase + 1] = 255;
                rgbaPixels[destinationBase + 2] = 255;
                rgbaPixels[destinationBase + 3] = 255;
                break;
            }
        }
    }
}

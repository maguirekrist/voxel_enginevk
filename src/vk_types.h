﻿// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>
#include <queue>
#include <fstream>
#include <iostream>
#include <chrono>
#include <random>
#include <filesystem>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <future>
#include <shared_mutex>
#include <tuple>
#include <condition_variable>
#include <barrier>
#include <bitset>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/gtx/transform.hpp>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <vulkan/vulkan_core.h>

struct AllocatedBuffer {
    VkBuffer _buffer;
    VmaAllocation _allocation;
};

struct AllocatedImage {
    VkImage _image;
    VmaAllocation _allocation;
};

struct QueueFamily {
	VkQueue _queue;
	uint32_t _queueFamily;
};

struct UploadContext {
	VkFence _uploadFence;
	VkCommandPool _commandPool;
	VkCommandBuffer _commandBuffer;
};

using Clock = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = std::chrono::duration<float>;

struct Material {
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	//TODO: Have a Material own its own resources like descriptor sets, etc.
	
};

struct FrameData {
	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;
	//std::vector<VkCommandBuffer> _secondaryCommandBuffers;
	AllocatedBuffer _chunkBuffer;
	VkDescriptorSet _chunkDescriptor;

	AllocatedBuffer _cameraBuffer;
	VkDescriptorSet _globalDescriptor;

};


//we will add our main reusable types here
#define VK_CHECK(x)                                                 \
	do                                                              \
	{                                                               \
		VkResult err = x;                                           \
		if (err)                                                    \
		{                                                           \
			std::cout << "Detected Vulkan error: {}" << err << std::endl;         \
			abort();                                                \
		}                                                           \
	} while (0)
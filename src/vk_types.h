// vulkan_guide.h : Include file for standard system include files,
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
#include <fstream>
#include <iostream>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>
#include <vk_mem_alloc.h>

#include <fmt/core.h>

#include <glm/gtx/transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

struct AllocatedBuffer {
    VkBuffer _buffer;
    VmaAllocation _allocation;
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
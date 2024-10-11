#pragma once

#include <vulkan/vulkan.h>
#include <vk_types.h>

class RenderPass {
public:
    RenderPass(VkDevice device, VkFormat swapchainImageFormat);
    ~RenderPass();

    VkRenderPass get_handle() const { return _renderPass; }

    // Define a type for the callback function
    using RenderPassExecuteCallback = std::function<void(VkCommandBuffer)>;

    // Method to set the callback
    void set_execute_callback(RenderPassExecuteCallback callback) {
        _executeCallback = std::move(callback);
    }

    // Method to execute the render pass
    void execute(VkCommandBuffer commandBuffer) {
        if (_executeCallback) {
            _executeCallback(commandBuffer);
        }
    }

private:
    RenderPassExecuteCallback _executeCallback;
    VkDevice _device;
    VkRenderPass _renderPass;
};

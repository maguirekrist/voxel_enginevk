﻿// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_mesh.h>
#include <cube_engine.h>
#include <camera.h>
#include <vulkan/vulkan_core.h>

class DeletionQueue {
public:
	
	void push_function(std::function<void()>&& function)
	{
		deletors.push_back(function);
	}

	void flush() {
		for(auto func : deletors) {
			func();
		}

		deletors.clear();
	}

private:
	std::deque<std::function<void()>> deletors;
};

constexpr unsigned int FRAME_OVERLAP = 2;

class VulkanEngine {
public:
	bool _isInitialized{ false };
	bool bUseValidationLayers{ false };
	int _frameNumber {0};

	bool bFocused = false;
	bool bQuit = false;
	
	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _chosenGPU;
	static VkDevice _device;
	VkSurfaceKHR _surface;

	VkSwapchainKHR _swapchain;
	VkFormat _swapchainImageFormat;

	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	//VkExtent2D _swapchainExtend;

	VkQueue _graphicsQueue;
	uint32_t _graphicsQueueFamily;

	VkRenderPass _renderPass;
	std::vector<VkFramebuffer> _framebuffers;

	std::unordered_map<std::string, Material> _materials;
    std::unordered_map<std::string, Mesh*> _meshes;

	std::vector<RenderObject> _renderObjects;
	CubeEngine _game;
	Camera _camera;
	std::optional<RaycastResult> _targetBlock;

	float _deltaTime;
	TimePoint _lastFrameTime;
	TimePoint _lastFpsTime;
	float _fps;
	
	VkImageView _depthImageView;
	AllocatedImage _depthImage;
	VkFormat _depthFormat;

	DeletionQueue _mainDeletionQueue;
	static VmaAllocator _allocator;

	FrameData _frames[FRAME_OVERLAP];

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	FrameData& get_current_frame();

	Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);

	Material* get_material(const std::string& name);

	static void upload_mesh(Mesh& mesh);
	static void unload_mesh(Mesh& mesh);

	//??
	//Mesh* get_mesh(const std::string& name);

	void draw_objects(VkCommandBuffer cmd, RenderObject* first, int count);

	void draw_chunks(VkCommandBuffer cmd);

	void calculate_fps();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	void handle_input();

	//draw loop
	void draw();

	//run main loop
	void run();
private:

	void init_vulkan();
	void init_swapchain();
	void init_commands();
	void init_default_renderpass();
	void init_framebuffers();
	void init_sync_structures();
	void init_pipelines();

	void build_material_default();
	void build_material_wireframe();

	//void init_scene();

	bool load_shader_module(const std::string& filePath, VkShaderModule* outShaderModule);

	void build_target_block_view(const glm::vec3& worldPos);
	RenderObject build_chunk_debug_view(const Chunk& chunk);
};

class PipelineBuilder {
public:
	std::vector<VkPipelineShaderStageCreateInfo> _shaderStages;
	VkPipelineVertexInputStateCreateInfo _vertexInputInfo;
	VkPipelineInputAssemblyStateCreateInfo _inputAssembly;
	VkViewport _viewport;
	VkRect2D _scissor;
	VkPipelineRasterizationStateCreateInfo _rasterizer;
	VkPipelineColorBlendAttachmentState _colorBlendAttachment;
	VkPipelineDepthStencilStateCreateInfo _depthStencil;
	VkPipelineMultisampleStateCreateInfo _multisampling;
	VkPipelineLayout _pipelineLayout;

	VkPipeline build_pipeline(VkDevice device, VkRenderPass pass);
};
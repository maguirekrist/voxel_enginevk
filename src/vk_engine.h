// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <vk_types.h>
#include <vk_mesh.h>
#include <camera.h>
#include <world.h>

using Clock = std::chrono::high_resolution_clock;
using TimePoint = std::chrono::time_point<Clock>;
using Duration = std::chrono::duration<float>;

struct Material {
	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;
};

struct RenderObject {
	Mesh* mesh;

	Material* material;

	glm::mat4 transformMatrix;
};

struct FrameData {
	VkSemaphore _presentSemaphore, _renderSemaphore;
	VkFence _renderFence;

	VkCommandPool _commandPool;
	VkCommandBuffer _mainCommandBuffer;
};

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

constexpr unsigned int FRAME_OVERLAP = 1;

class VulkanEngine {
public:
	bool _isInitialized{ false };
	bool bUseValidationLayers{ false };
	int _frameNumber {0};
	
	VkInstance _instance;
	VkDebugUtilsMessengerEXT _debug_messenger;
	VkPhysicalDevice _chosenGPU;
	VkDevice _device;
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

	World _world;
	std::vector<RenderObject> _renderables;
	std::unordered_map<std::string, Material> _materials;
	std::unordered_map<std::string, Mesh> _meshes;
	Camera _camera{glm::vec3(0.0f, static_cast<float>(CHUNK_HEIGHT), 0.0f)};

	float _deltaTime;
	TimePoint _lastFrameTime;
	TimePoint _lastFpsTime;

	VkImageView _depthImageView;
	AllocatedImage _depthImage;
	VkFormat _depthFormat;

	DeletionQueue _mainDeletionQueue;
	VmaAllocator _allocator;

	FrameData _frames[FRAME_OVERLAP];

	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	FrameData& get_current_frame();

	Material* create_material(VkPipeline pipeline, VkPipelineLayout layout, const std::string& name);

	Material* get_material(const std::string& name);

	Mesh* get_mesh(const std::string& name);

	void draw_objects(VkCommandBuffer cmd, RenderObject* first, int count);

	float _fps;

	void calculate_fps();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

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
	void init_scene();

	void load_meshes();
	void upload_mesh(Mesh& mesh);

	bool load_shader_module(const std::string& filePath, VkShaderModule* outShaderModule);
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
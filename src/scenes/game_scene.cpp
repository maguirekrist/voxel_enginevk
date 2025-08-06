#include "game_scene.h"
#include "vk_engine.h"
#include <tracy/Tracy.hpp>
#include <vk_initializers.h>
#include <vk_mesh.h>

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"

GameScene::GameScene() {
	auto fogUboBuffer = vkutil::create_buffer(VulkanEngine::instance()._allocator, sizeof(FogUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	auto cameraUboBuffer = vkutil::create_buffer(VulkanEngine::instance()._allocator, sizeof(CameraUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	_cameraUboResource = std::make_shared<Resource>(Resource::BUFFER, Resource::ResourceValue(cameraUboBuffer));
	_fogResource = std::make_shared<Resource>(Resource::BUFFER, Resource::ResourceValue(fogUboBuffer));

	VulkanEngine::instance()._materialManager.build_postprocess_pipeline(_fogResource);

	auto translate = PushConstant{
				.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
				.size = sizeof(ObjectPushConstants),
				.build_constant = [](const RenderObject& obj) -> ObjectPushConstants {
					ObjectPushConstants push{};
					push.chunk_translate = obj.xzPos;
					return push;
				}
			};

	//VulkanEngine::instance()._materialManager.build_material_default(_cameraUboResource, translate);
	VulkanEngine::instance()._materialManager.build_graphics_pipeline(
		{ _cameraUboResource },
		{ translate },
		{},
		"tri_mesh.vert.spv",
		"tri_mesh.frag.spv",
		"defaultmesh"
	);

	VulkanEngine::instance()._materialManager.build_graphics_pipeline(
		{_cameraUboResource, _fogResource}, // Order Matters here
		{ translate },
		{ .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .enableBlending = true},
		"water_mesh.vert.spv",
		"water_mesh.frag.spv",
		"watermesh"
	);
	//
	// VulkanEngine::instance()._materialManager.build_material_wireframe();
	//  VulkanEngine::instance()._materialManager.build_graphics_pipeline(
	//  	{_cameraUboResource},
	//  	{ translate },
	//  	{},
	//  	"default.vert.spv",
	//  	"default.frag.spv",
	//  	"defaultobj");


	VulkanEngine::instance()._materialManager.build_present_pipeline();
	//
	// //Load all the objects from the obj folder
	// for (const auto& file : std::filesystem::directory_iterator("models")) {
	// 	auto mesh = VulkanEngine::instance()._meshManager.queue_from_obj(file.path().string());
	// 	_gameObjects.push_back(std::make_shared<RenderObject>(RenderObject{std::make_shared<SharedResource<Mesh>>(mesh), VulkanEngine::instance()._materialManager.get_material("defaultobj"), glm::vec2(0, 0), RenderLayer::Opaque }));
	// 	fmt::println("Loaded object: {}", file.path().string());
	// }


	std::println("GameScene created!");
}

GameScene::~GameScene()
{
	std::println("GameScene::~GameScene");
}

void GameScene::queue_objects() {
	ZoneScopedN("Draw Chunks & Objects");
	update_uniform_buffer();
	update_fog_ubo();
}

void GameScene::update(const float deltaTime)
{
	_game._player._moveSpeed = GameConfig::DEFAULT_MOVE_SPEED * deltaTime;
	_camera.update_view(_game._player._position, _game._player._front, _game._player._up);
	_game.update();
}

void GameScene::cleanup()
{
    _game.cleanup();
}

void GameScene::handle_input(const SDL_Event& event)
{
	switch(event.type) {
		case SDL_MOUSEBUTTONDOWN:
			_targetBlock = _camera.get_target_block(_game._world, _game._player);
			if(_targetBlock.has_value())
			{
				auto block = _targetBlock.value()._block;
				//auto chunk = _targetBlock.value()._chunk;
				glm::vec3 worldBlockPos = _targetBlock.value()._worldPos;
				//build_target_block_view(worldBlockPos);
				//fmt::println("Current target block: Block(x{}, y{}, z{}, light: {}), at distance: {}", block->_position.x, block->_position.y, block->_position.z, block->_sunlight, _targetBlock.value()._distance);
				//fmt::println("Current chunk: Chunk(x: {}, y: {})", chunk->_position.x, chunk->_position.y);
			}
			break;
		case SDL_MOUSEMOTION:
			_game._player.handle_mouse_move(static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel));
			break;
		default:
			//no-op
			break;
	}
}

void GameScene::handle_keystate(const Uint8* state)
{
	if (state[SDL_SCANCODE_W])
	{
		_game._player.move_forward();
	}

	if (state[SDL_SCANCODE_S])
	{
		_game._player.move_backward();
	}

	if (state[SDL_SCANCODE_A])
	{
		_game._player.move_left();
	}

	if (state[SDL_SCANCODE_D])
	{
		_game._player.move_right();
	}
}

void GameScene::draw_imgui()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
	ImGui::ShowDemoWindow();

	{
		static float f = 0.0f;
		static int counter = 0;

		ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

		ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
		// ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
		// ImGui::Checkbox("Another Window", &show_another_window);

		ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
		//ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

		if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
			counter++;
		ImGui::SameLine();
		ImGui::Text("counter = %d", counter);

		//ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
		ImGui::End();
	}

	ImGuiIO& io = ImGui::GetIO();
	ImGui::Render();
}

void GameScene::update_fog_ubo() const
{
	FogUBO fogUBO;
	fogUBO.fogColor = static_cast<glm::vec3>(Colors::skyblueHigh);
	fogUBO.fogEndColor = static_cast<glm::vec3>(Colors::skyblueLow);

	fogUBO.fogCenter = _game._player._position;
	fogUBO.fogRadius = (CHUNK_SIZE * GameConfig::DEFAULT_VIEW_DISTANCE) - 60.0f;
	fogUBO.screenSize = glm::ivec2(VulkanEngine::instance()._windowExtent.width, VulkanEngine::instance()._windowExtent.height);
	fogUBO.invViewProject = glm::inverse(_camera._projection * _camera._view);

	void* data;
	vmaMapMemory(VulkanEngine::instance()._allocator, _fogResource->value.buffer._allocation, &data);
	memcpy(data, &fogUBO, sizeof(FogUBO));
	vmaUnmapMemory(VulkanEngine::instance()._allocator, _fogResource->value.buffer._allocation);
}

void GameScene::update_uniform_buffer() const
{
	CameraUBO cameraUBO;
	cameraUBO.projection = _camera._projection;
	cameraUBO.view = _camera._view;
	cameraUBO.viewproject = _camera._projection * _camera._view; 

	void* data;
	vmaMapMemory(VulkanEngine::instance()._allocator, _cameraUboResource->value.buffer._allocation, &data);
	memcpy(data, &cameraUBO, sizeof(CameraUBO));
	vmaUnmapMemory(VulkanEngine::instance()._allocator, _cameraUboResource->value.buffer._allocation);
}


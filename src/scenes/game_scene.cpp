#include "game_scene.h"
#include "vk_engine.h"
#include <tracy/Tracy.hpp>
#include <vk_initializers.h>
#include <vk_mesh.h>

#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"

GameScene::GameScene() :
	_camera([this](const glm::vec3 pos)
	{
		const auto block = _game._world.get_block(pos);
		return block->_solid;
	})
{
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

void GameScene::update_buffers() {
	ZoneScopedN("Draw Chunks & Objects");
	update_uniform_buffer();
	update_fog_ubo();
}

void GameScene::update(const float deltaTime)
{
	_game._player._moveSpeed = GameConfig::DEFAULT_MOVE_SPEED * deltaTime;
	_camera._moveSpeed = GameConfig::DEFAULT_MOVE_SPEED * deltaTime;
	_camera.update_view();
	_game.update();
}

void GameScene::handle_input(const SDL_Event& event)
{
	switch(event.type) {
		case SDL_MOUSEBUTTONDOWN:
			// _targetBlock = _camera.get_target_block(_game._world, _game._player);
			// if(_targetBlock.has_value())
			// {
			// 	auto block = _targetBlock.value()._block;
			// 	//auto chunk = _targetBlock.value()._chunk;
			// 	glm::vec3 worldBlockPos = _targetBlock.value()._worldPos;
			// 	//build_target_block_view(worldBlockPos);
			// 	//fmt::println("Current target block: Block(x{}, y{}, z{}, light: {}), at distance: {}", block->_position.x, block->_position.y, block->_position.z, block->_sunlight, _targetBlock.value()._distance);
			// 	//fmt::println("Current chunk: Chunk(x: {}, y: {})", chunk->_position.x, chunk->_position.y);
			// }
			break;
		case SDL_MOUSEMOTION:
			_game._player.handle_mouse_move(static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel));
			_camera.handle_mouse_move(static_cast<float>(event.motion.xrel), static_cast<float>(event.motion.yrel));
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
		_camera.move_forward();
	}

	if (state[SDL_SCANCODE_S])
	{
		_game._player.move_backward();
		_camera.move_backward();
	}

	if (state[SDL_SCANCODE_A])
	{
		_game._player.move_left();
		_camera.move_left();
	}

	if (state[SDL_SCANCODE_D])
	{
		_game._player.move_right();
		_camera.move_right();
	}
}

void GameScene::draw_imgui()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
	//ImGui::ShowDemoWindow();

	{
		ImGui::Begin("Chunk Debug");

		ChunkCoord playerChunk = World::get_chunk_coordinates(_game._player._position);
		if (!_game._current_chunk.expired())
		{
			ImGui::Text("Player Chunk: %d,%d", _game._current_chunk.lock()->_chunkCoord.x,  _game._current_chunk.lock()->_chunkCoord.z);
		}

		if (_game._current_block != nullptr)
		{
			ImGui::Text("Camera World Position: x: %f, z: %f, y: %f", _camera._position.x, _camera._position.z, _camera._position.y);
			ImGui::Text("Player World Position: x: %f, z: %f, y: %f", _game._player._position.x, _game._player._position.z, _game._player._position.y);
			ImGui::Text("Camera Front: x: %f, z: %f, y: %f", _camera._front.x, _camera._front.z, _camera._front.y);
			auto local_pos = World::get_local_coordinates(_game._player._position);
			ImGui::Text("Player Local Position: x: %d, z: %d, y: %d", local_pos.x, local_pos.z, local_pos.y);
		}

		const auto& render_set = VulkanEngine::instance()._opaqueSet.data();
		auto active_set = render_set | std::views::filter([](const auto& renderObj)
		{
			return renderObj.mesh->_isActive.load(std::memory_order::acquire) == true;
		});
		const auto active_count = std::ranges::distance(active_set);

		ImGui::Text("Active Renderables: %d", static_cast<size_t>(active_count));

		const int max_chunks = (GameConfig::DEFAULT_VIEW_DISTANCE * 2) + 1;
		if (ImGui::BeginTable("MyGrid", max_chunks)) {
			for (int row = 0; row < max_chunks; ++row) {
				ImGui::TableNextRow();
				for (int col = 0; col < max_chunks; ++col) {
					ImGui::TableSetColumnIndex(col);
					const ChunkCoord chunkCoord = {playerChunk.x + (row - GameConfig::DEFAULT_VIEW_DISTANCE), playerChunk.z + (col - GameConfig::DEFAULT_VIEW_DISTANCE)};
					const auto chunk = _game._chunkManager.get_chunk(chunkCoord);
					if (chunk.has_value())
					{
						switch (chunk.value()->_state.load(std::memory_order::acquire))
						{
						case ChunkState::Border:
							ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
							break;
						case ChunkState::Rendered:
							if (chunk.value()->_mesh->_isActive.load(std::memory_order::acquire) == true)
							{
								ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
							} else
							{
								ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
							}

							break;
						default:
							ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
							break;
						}
					} else
					{
						ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
					}
					ImGui::Text("[%d,%d]", chunkCoord.x, chunkCoord.z);
					ImGui::PopStyleColor();
				}
			}

			ImGui::EndTable();
		}

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


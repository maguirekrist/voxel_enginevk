#include "game_scene.h"
#include "vk_engine.h"
#include <tracy/Tracy.hpp>
#include <vk_initializers.h>
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"


GameScene::GameScene(const SceneServices& services): _services(services)
{
    const ResourceBackendContext resourceBackend{
        .device = _services.device,
        .allocator = _services.allocator
    };
	auto fogUboBuffer = vkutil::create_buffer(_services.allocator, sizeof(FogUBO),
	                                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	auto cameraUboBuffer = vkutil::create_buffer(_services.allocator, sizeof(CameraUBO),
	                                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
	_cameraUboResource = std::make_shared<Resource>(resourceBackend, Resource::BUFFER, Resource::ResourceValue(cameraUboBuffer));
	_fogResource = std::make_shared<Resource>(resourceBackend, Resource::BUFFER, Resource::ResourceValue(fogUboBuffer));

	build_pipelines();

	create_camera();
    sync_camera_to_game(0.0f);

	std::println("GameScene created!");
}

GameScene::~GameScene()
{
	_chunkRenderRegistry.clear(_renderState);
	std::println("GameScene::~GameScene");
}

void GameScene::update_buffers() {
	ZoneScopedN("Draw Chunks & Objects");
	_chunkRenderRegistry.sync(
		_game.chunk_manager(),
		*_services.meshManager,
		*_services.materialManager,
		_renderState);
	update_uniform_buffer();
	update_fog_ubo();
}

SceneRenderState& GameScene::get_render_state()
{
	return _renderState;
}

void GameScene::update(const float deltaTime)
{
	_game.set_player_input(_playerInput);
    _playerInput.lookDeltaX = 0.0f;
    _playerInput.lookDeltaY = 0.0f;
	_game.update(deltaTime);
    sync_camera_to_game(deltaTime);
}

void GameScene::handle_input(const SDL_Event& event)
{
	switch(event.type) {
		case SDL_MOUSEBUTTONDOWN:
			break;
		case SDL_MOUSEMOTION:
            _playerInput.lookDeltaX += static_cast<float>(event.motion.xrel);
            _playerInput.lookDeltaY += static_cast<float>(event.motion.yrel);
			break;
		default:
			//no-op
			break;
	}
}

void GameScene::handle_keystate(const Uint8* state)
{
    _playerInput.moveForward = state[SDL_SCANCODE_W];
    _playerInput.moveBackward = state[SDL_SCANCODE_S];
    _playerInput.moveLeft = state[SDL_SCANCODE_A];
    _playerInput.moveRight = state[SDL_SCANCODE_D];
}

void GameScene::draw_imgui()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();
	//ImGui::ShowDemoWindow();

	{
		draw_debug_map();
	}

	ImGuiIO& io = ImGui::GetIO();
	ImGui::Render();
}

void GameScene::rebuild_pipelines()
{
	_camera->resize(_services.current_window_extent());
	build_pipelines();
}

void GameScene::build_pipelines()
{
	auto translate = PushConstant{
	.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	.size = sizeof(ObjectPushConstants),
	.build_constant = [](const RenderObject& obj) -> ObjectPushConstants
		{
			ObjectPushConstants push{};
			push.chunk_translate = obj.xzPos;
			return push;
		}
	};
	_services.materialManager->build_graphics_pipeline(
		{ MaterialBinding::from_resource(0, 0, _cameraUboResource) },
		{ translate },
		{},
		"tri_mesh.vert.spv",
		"tri_mesh.frag.spv",
		"defaultmesh"
	);

	_services.materialManager->build_graphics_pipeline(
		{
            MaterialBinding::from_resource(0, 0, _cameraUboResource),
            MaterialBinding::from_resource(1, 0, _fogResource)
        },
		{ translate },
		{ .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .enableBlending = true },
		"water_mesh.vert.spv",
		"water_mesh.frag.spv",
		"watermesh"
	);

	_services.materialManager->build_postprocess_pipeline(_fogResource);
	_services.materialManager->build_present_pipeline();
}

void GameScene::draw_debug_map()
{
	ImGui::Begin("Chunk Debug");

	const VkExtent2D windowExtent = _services.current_window_extent();
	ImGui::Text("Window size: %d x %d", windowExtent.width, windowExtent.height);

    const GameSnapshot& snapshot = _game.snapshot();
	ChunkCoord playerChunk = snapshot.currentChunk.value_or(World::get_chunk_coordinates(snapshot.player.position));
	if (snapshot.currentChunk.has_value())
	{
		ImGui::Text("Player Chunk: %d,%d", snapshot.currentChunk->x,  snapshot.currentChunk->z);
	}

	if (snapshot.hasCurrentBlock)
	{
		ImGui::Text("Camera World Position: x: %f, z: %f, y: %f", _camera->_position.x, _camera->_position.z, _camera->_position.y);
		ImGui::Text("Player World Position: x: %f, z: %f, y: %f", snapshot.player.position.x, snapshot.player.position.z, snapshot.player.position.y);
		ImGui::Text("Camera Front: x: %f, z: %f, y: %f", _camera->_front.x, _camera->_front.z, _camera->_front.y);
		auto local_pos = World::get_local_coordinates(snapshot.player.position);
		ImGui::Text("Player Local Position: x: %d, z: %d, y: %d", local_pos.x, local_pos.z, local_pos.y);
	}

	const int max_chunks = (GameConfig::DEFAULT_VIEW_DISTANCE * 2) + 1;
	if (ImGui::BeginTable("MyGrid", max_chunks)) {
		for (int row = 0; row < max_chunks; ++row) {
			ImGui::TableNextRow();
			for (int col = 0; col < max_chunks; ++col) {
				ImGui::TableSetColumnIndex(col);
				const ChunkCoord chunkCoord = {playerChunk.x + (row - GameConfig::DEFAULT_VIEW_DISTANCE), playerChunk.z + (col - GameConfig::DEFAULT_VIEW_DISTANCE)};
				const auto chunk = _game.get_chunk(chunkCoord);
				if (chunk)
				{
					switch (chunk->_state.load(std::memory_order::acquire))
					{
					// case ChunkState::Border:
					// 	ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
					// 	break;
					case ChunkState::Generated:
						ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 255, 255));
						break;
					case ChunkState::Rendered:
						if (chunk->_meshData->mesh->_isActive.load(std::memory_order::acquire) == true)
						{
							ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
						} else
						{
							ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 20, 125, 255));
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
				ImGui::Text("[%d,%d,%d]", chunkCoord.x, chunkCoord.z, chunk != nullptr ? chunk->_gen.load(std::memory_order::acquire) : -1);
				ImGui::PopStyleColor();
			}
		}

		ImGui::EndTable();
	}
	ImGui::End();
}

void GameScene::update_fog_ubo() const
{
	FogUBO fogUBO{};
	fogUBO.fogColor = static_cast<glm::vec3>(Colors::skyblueHigh);
	fogUBO.fogEndColor = static_cast<glm::vec3>(Colors::skyblueLow);

	fogUBO.fogCenter = _game.snapshot().player.position;
	fogUBO.fogRadius = (CHUNK_SIZE * GameConfig::DEFAULT_VIEW_DISTANCE) - 60.0f;
	const VkExtent2D windowExtent = _services.current_window_extent();
	fogUBO.screenSize = glm::ivec2(windowExtent.width, windowExtent.height);
	fogUBO.invViewProject = glm::inverse(_camera->_projection * _camera->_view);

	void* data;
	vmaMapMemory(_services.allocator, _fogResource->value.buffer._allocation, &data);
	memcpy(data, &fogUBO, sizeof(FogUBO));
	vmaUnmapMemory(_services.allocator, _fogResource->value.buffer._allocation);
}

void GameScene::update_uniform_buffer() const
{
	CameraUBO cameraUBO{};
	cameraUBO.projection = _camera->_projection;
	cameraUBO.view = _camera->_view;
	cameraUBO.viewproject = _camera->_projection * _camera->_view;

	void* data;
	vmaMapMemory(_services.allocator, _cameraUboResource->value.buffer._allocation, &data);
	memcpy(data, &cameraUBO, sizeof(CameraUBO));
	vmaUnmapMemory(_services.allocator, _cameraUboResource->value.buffer._allocation);
}
void GameScene::create_camera()
{
	_camera = std::make_unique<Camera>(GameConfig::DEFAULT_POSITION, _services.current_window_extent());
}

void GameScene::sync_camera_to_game(const float deltaTime)
{
    const PlayerSnapshot& player = _game.snapshot().player;
    _camera->_position = player.position;
    _camera->_front = player.front;
    _camera->_up = player.up;
    _camera->_yaw = player.yaw;
    _camera->_pitch = player.pitch;
    _camera->update(deltaTime);
}

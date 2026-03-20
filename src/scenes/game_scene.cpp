#include "game_scene.h"
#include "vk_engine.h"
#include <tracy/Tracy.hpp>
#include <vk_initializers.h>
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_vulkan.h"
#include "render/mesh_release_queue.h"


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
    clear_target_block_outline();
    clear_chunk_boundary_debug();
	_chunkRenderRegistry.clear(_renderState);
    if (_chunkBoundaryMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_chunkBoundaryMesh));
    }
    if (_targetBlockOutlineMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_targetBlockOutlineMesh));
    }
	std::println("GameScene::~GameScene");
}

void GameScene::update_buffers() {
	ZoneScopedN("Draw Chunks & Objects");
	_chunkRenderRegistry.sync(
		_game.chunk_manager(),
		*_services.meshManager,
		*_services.materialManager,
		_renderState);
    sync_target_block_outline();
    sync_chunk_boundary_debug();
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
    sync_target_block();
}

void GameScene::handle_input(const SDL_Event& event)
{
	switch(event.type) {
        case SDL_KEYDOWN:
            if (!event.key.repeat && event.key.keysym.scancode == SDL_SCANCODE_G)
            {
                _showChunkBoundaries = !_showChunkBoundaries;
            }
            break;
		case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT && _targetBlock.has_value())
            {
                if (_targetBlock->_distance <= GameConfig::BLOCK_INTERACTION_DISTANCE)
                {
                    _game.apply_block_edit(BlockEdit{
                        .worldPos = _targetBlock->_worldPos,
                        .newBlock = Block{
                            ._solid = false,
                            ._sunlight = MAX_LIGHT_LEVEL,
                            ._type = BlockType::AIR
                        },
                        .source = EditSource::LocalPlayer
                    });
                }
            }
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

    _services.materialManager->build_graphics_pipeline(
        { MaterialBinding::from_resource(0, 0, _cameraUboResource) },
        { translate },
        { .depthTest = true, .depthWrite = false, .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL, .enableBlending = false, .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST },
        "tri_mesh.vert.spv",
        "tri_mesh.frag.spv",
        "chunkboundary"
    );

	_services.materialManager->build_postprocess_pipeline(_fogResource);
	_services.materialManager->build_present_pipeline();
}

void GameScene::draw_debug_map()
{
    auto data_state_label = [](const DataState state) -> const char*
    {
        switch (state)
        {
        case DataState::Empty: return "E";
        case DataState::GenerateQueued: return "GQ";
        case DataState::Generating: return "GG";
        case DataState::Ready: return "R";
        case DataState::Dirty: return "D";
        }
        return "?";
    };

    auto mesh_state_label = [](const MeshState state) -> const char*
    {
        switch (state)
        {
        case MeshState::Missing: return "M";
        case MeshState::MeshQueued: return "MQ";
        case MeshState::Meshing: return "MS";
        case MeshState::MeshReady: return "MR";
        case MeshState::Uploaded: return "U";
        case MeshState::Stale: return "S";
        }
        return "?";
    };

	ImGui::Begin("Chunk Debug");
    ImGui::Checkbox("Show Chunk Boundaries (G)", &_showChunkBoundaries);

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
                const auto debugState = _game.chunk_manager().debug_state(chunkCoord);
				if (chunk)
				{
					switch (chunk->_state.load(std::memory_order::acquire))
					{
					case ChunkState::Generated:
                        if (debugState.has_value() && debugState->meshState == MeshState::Stale)
                        {
						    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 180, 0, 255));
                        }
                        else
                        {
						    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 255, 255));
                        }
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
                if (debugState.has_value())
                {
				    ImGui::Text("[%d,%d g%u d%u %s/%s]", chunkCoord.x, chunkCoord.z, debugState->generationId, debugState->dataVersion, data_state_label(debugState->dataState), mesh_state_label(debugState->meshState));
                }
                else
                {
				    ImGui::Text("[%d,%d,%d]", chunkCoord.x, chunkCoord.z, chunk != nullptr ? chunk->_gen.load(std::memory_order::acquire) : -1);
                }
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

void GameScene::sync_target_block()
{
    _targetBlock = _game.raycast_target_block(GameConfig::BLOCK_INTERACTION_DISTANCE);
}

void GameScene::sync_target_block_outline()
{
    if (!_targetBlock.has_value())
    {
        clear_target_block_outline();
        _outlinedBlockWorldPos.reset();
        return;
    }

    const glm::ivec3 worldPos = _targetBlock->_worldPos;
    if (_outlinedBlockWorldPos.has_value() && _outlinedBlockWorldPos.value() == worldPos)
    {
        return;
    }

    clear_target_block_outline();
    const glm::ivec2 chunkOrigin = World::get_chunk_origin(glm::vec3(worldPos));
    const glm::ivec3 localPos = World::get_local_coordinates(glm::vec3(worldPos));

    if (_targetBlockOutlineMesh != nullptr)
    {
        render::enqueue_mesh_release(std::move(_targetBlockOutlineMesh));
    }

    _targetBlockOutlineMesh = Mesh::create_block_outline_mesh(glm::vec3(localPos));
    _services.meshManager->UploadQueue.enqueue(_targetBlockOutlineMesh);
    _outlinedBlockWorldPos = worldPos;

    _targetBlockOutlineHandle = _renderState.opaqueObjects.insert(RenderObject{
        .mesh = _targetBlockOutlineMesh,
        .material = _services.materialManager->get_material("chunkboundary"),
        .xzPos = chunkOrigin,
        .layer = RenderLayer::Opaque
    });
}

void GameScene::sync_chunk_boundary_debug()
{
    clear_chunk_boundary_debug();
    if (!_showChunkBoundaries)
    {
        return;
    }

    if (_chunkBoundaryMesh == nullptr)
    {
        _chunkBoundaryMesh = Mesh::create_chunk_boundary_mesh();
        _services.meshManager->UploadQueue.enqueue(_chunkBoundaryMesh);
    }

    const auto debugMaterial = _services.materialManager->get_material("chunkboundary");
    const GameSnapshot& snapshot = _game.snapshot();
    const ChunkCoord playerChunk = snapshot.currentChunk.value_or(World::get_chunk_coordinates(snapshot.player.position));
    const int viewDistance = GameConfig::DEFAULT_VIEW_DISTANCE;
    _chunkBoundaryHandles.reserve(static_cast<size_t>((viewDistance * 2 + 1) * (viewDistance * 2 + 1)));

    for (int chunkZ = playerChunk.z - viewDistance; chunkZ <= playerChunk.z + viewDistance; ++chunkZ)
    {
        for (int chunkX = playerChunk.x - viewDistance; chunkX <= playerChunk.x + viewDistance; ++chunkX)
        {
            const ChunkCoord chunkCoord{ chunkX, chunkZ };
            const Chunk* const chunk = _game.get_chunk(chunkCoord);
            if (chunk == nullptr)
            {
                continue;
            }

            _chunkBoundaryHandles.push_back(_renderState.opaqueObjects.insert(RenderObject{
            .mesh = _chunkBoundaryMesh,
            .material = debugMaterial,
            .xzPos = glm::ivec2(chunk->_data->position.x, chunk->_data->position.y),
            .layer = RenderLayer::Opaque
        }));
        }
    }
}

void GameScene::clear_chunk_boundary_debug()
{
    for (const auto handle : _chunkBoundaryHandles)
    {
        _renderState.opaqueObjects.remove(handle);
    }

    _chunkBoundaryHandles.clear();
}

void GameScene::clear_target_block_outline()
{
    if (_targetBlockOutlineHandle.has_value())
    {
        _renderState.opaqueObjects.remove(_targetBlockOutlineHandle.value());
        _targetBlockOutlineHandle.reset();
    }
}

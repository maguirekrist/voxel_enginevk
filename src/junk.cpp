// void VulkanEngine::build_target_block_view(const glm::vec3& worldPos)
// {
// 	RenderObject target_block;
// 	target_block.material = get_material("defaultmesh");
// 	if(!_meshes.contains("cubeMesh")) {
// 		std::unique_ptr<Mesh> cubeMesh = std::make_unique<Mesh>(Mesh::create_cube_mesh());
// 		_meshes["cubeMesh"] = std::move(cubeMesh);
// 		upload_mesh(*_meshes["cubeMesh"]);
// 	}

// 	target_block.mesh = std::make_unique<SharedResource<Mesh>>(_meshes["cubeMesh"]);
// 	glm::mat4 translate = glm::translate(glm::mat4{ 1.0 }, worldPos);
// 	//target_block.transformMatrix = glm::scale(translate, glm::vec3(1.1f, 1.1f, 1.1f));
// 	_renderObjects.push_back(target_block);
// }

// RenderObject VulkanEngine::build_chunk_debug_view(const Chunk& chunk)
// {
// 	RenderObject target_chunk;
// 	target_chunk.material = get_material("wireframe");
// 	if(!_meshes.contains("cubeMesh")) {
// 		std::unique_ptr<Mesh> cubeMesh = std::make_unique<Mesh>(Mesh::create_cube_mesh());
// 		_meshes["cubeMesh"] = std::move(cubeMesh);
// 		upload_mesh(*_meshes["cubeMesh"]);
// 	}

// 	target_chunk.mesh = std::make_unique<SharedResource<Mesh>>(_meshes["cubeMesh"]);
// 	glm::mat4 translate = glm::translate(glm::mat4{ 1.0 }, glm::vec3(chunk._position.x, 0, chunk._position.y));
// 	//target_chunk.transformMatrix = glm::scale(translate, glm::vec3(CHUNK_SIZE, CHUNK_HEIGHT, CHUNK_SIZE));
// 	return target_chunk;
// 	// _renderObjects.push_back(target_chunk);
// }
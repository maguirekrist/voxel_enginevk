#include <vk_mesh.h>

VertexInputDescription Vertex::get_vertex_description()
{
    VertexInputDescription description;

    //we will have just 1 vertex buffer binding, with a per-vertex rate
	VkVertexInputBindingDescription mainBinding = {};
	mainBinding.binding = 0;
	mainBinding.stride = sizeof(Vertex);
	mainBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	description.bindings.push_back(mainBinding);

	//Position will be stored at Location 0
	VkVertexInputAttributeDescription positionAttribute = {};
	positionAttribute.binding = 0;
	positionAttribute.location = 0;
	positionAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	positionAttribute.offset = offsetof(Vertex, position);

	 //Normal will be stored at Location 1
	VkVertexInputAttributeDescription normalAttribute = {};
	normalAttribute.binding = 0;
	normalAttribute.location = 1;
	normalAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	normalAttribute.offset = offsetof(Vertex, normal);

	//Color will be stored at Location 2
	VkVertexInputAttributeDescription colorAttribute = {};
	colorAttribute.binding = 0;
	colorAttribute.location = 2;
	colorAttribute.format = VK_FORMAT_R32G32B32_SFLOAT;
	colorAttribute.offset = offsetof(Vertex, color);

	description.attributes.push_back(positionAttribute);
	description.attributes.push_back(normalAttribute);
	description.attributes.push_back(colorAttribute);
	return description;
}

Mesh Mesh::create_cube_mesh()
{
    Mesh cubeMesh;

    // Define the color green (R = 0, G = 1, B = 0)
    glm::vec3 greenColor(0.0f, 1.0f, 0.0f);

    // Define the vertices for a cube (2 triangles per face, 6 vertices per face)
    cubeMesh._vertices = {
        // Front face
        {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, greenColor},  // Bottom-left
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, greenColor},  // Bottom-right
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, greenColor},  // Top-right
        {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f,  1.0f}, greenColor},  // Top-left

        // Back face
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, greenColor},  // Bottom-left
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, greenColor},  // Bottom-right
        {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, greenColor},  // Top-right
        {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, greenColor},  // Top-left

        // Left face
        {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f,  0.0f}, greenColor}, // Bottom-left
        {{-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f,  0.0f}, greenColor}, // Bottom-right
        {{-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f,  0.0f}, greenColor}, // Top-right
        {{-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f,  0.0f}, greenColor}, // Top-left

        // Right face
        {{ 0.5f, -0.5f, -0.5f}, { 1.0f, 0.0f,  0.0f}, greenColor}, // Bottom-left
        {{ 0.5f, -0.5f,  0.5f}, { 1.0f, 0.0f,  0.0f}, greenColor}, // Bottom-right
        {{ 0.5f,  0.5f,  0.5f}, { 1.0f, 0.0f,  0.0f}, greenColor}, // Top-right
        {{ 0.5f,  0.5f, -0.5f}, { 1.0f, 0.0f,  0.0f}, greenColor}, // Top-left

        // Top face
        {{-0.5f,  0.5f,  0.5f}, {0.0f,  1.0f, 0.0f}, greenColor},  // Bottom-left
        {{ 0.5f,  0.5f,  0.5f}, {0.0f,  1.0f, 0.0f}, greenColor},  // Bottom-right
        {{ 0.5f,  0.5f, -0.5f}, {0.0f,  1.0f, 0.0f}, greenColor},  // Top-right
        {{-0.5f,  0.5f, -0.5f}, {0.0f,  1.0f, 0.0f}, greenColor},  // Top-left

        // Bottom face
        {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, greenColor},  // Bottom-left
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, greenColor},  // Bottom-right
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, greenColor},  // Top-right
        {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, greenColor},  // Top-left
    };

    // Define the indices for the cube (two triangles per face)
    cubeMesh._indices = {
        0, 1, 2, 2, 3, 0,   // Front face
        4, 5, 6, 6, 7, 4,   // Back face
        8, 9, 10, 10, 11, 8, // Left face
        12, 13, 14, 14, 15, 12, // Right face
        16, 17, 18, 18, 19, 16, // Top face
        20, 21, 22, 22, 23, 20  // Bottom face
    };

    return cubeMesh;
}
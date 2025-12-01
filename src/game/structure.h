#pragma once


//TODO: Define structure class
// Structures are a graph of chunks that are connected to each other.
// They are used to create buildings, rooms, etc.
#include <vk_types.h>
#include <aabb.h>

enum class StructureType {
	TREE
};

struct Anchor {
	StructureType type;
	glm::ivec3 position; // relative position in chunk
	AABB bounds;
};

class Structure {
public:

};
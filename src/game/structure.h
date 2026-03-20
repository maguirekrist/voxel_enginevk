#pragma once

#include <vk_types.h>
#include <functional>

#include "block.h"

enum class StructureType {
	TREE
};

enum class TreeVariant {
    Oak,
    Giant
};

struct StructureAnchor {
	StructureType type;
	glm::ivec3 worldOrigin{};
	uint64_t seed{0};
    TreeVariant treeVariant{TreeVariant::Oak};
};

struct StructureBlockEdit {
    glm::ivec3 worldPosition{};
    Block block{};
};

struct StructureGenerationContext {
    glm::ivec2 chunkCoord{};
    glm::ivec2 chunkOrigin{};
};

using StructureGenerator = std::function<std::vector<StructureBlockEdit>(const StructureAnchor&, const StructureGenerationContext&)>;

class StructureRegistry {
public:
    static StructureRegistry& instance();

    [[nodiscard]] std::vector<StructureBlockEdit> generate(const StructureAnchor& anchor, const StructureGenerationContext& context) const;
    [[nodiscard]] std::vector<StructureBlockEdit> generate_overlapping(const StructureGenerationContext& context) const;

private:
    StructureRegistry();

    std::unordered_map<StructureType, StructureGenerator> _generators;
};

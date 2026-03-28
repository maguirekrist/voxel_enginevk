#pragma once

#include <vk_types.h>
#include <memory>
#include <unordered_map>
#include <vector>

#include "game/block.h"
#include "world/terrain_gen.h"

enum class StructureType {
	TREE,
    CLOUD
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
    const TerrainGenerator* terrainGenerator{nullptr};
    const ChunkTerrainData* terrainScaffold{nullptr};
    const TerrainFeatureInstanceSet* terrainFeatures{nullptr};
    const AppearanceBuffer* terrainAppearance{nullptr};
};

class IStructureGenerator
{
public:
    virtual ~IStructureGenerator() = default;

    [[nodiscard]] virtual StructureType type() const noexcept = 0;
    [[nodiscard]] virtual std::vector<StructureBlockEdit> generate(const StructureAnchor& anchor, const StructureGenerationContext& context) const = 0;
};

class IStructurePlacementStrategy
{
public:
    virtual ~IStructurePlacementStrategy() = default;

    [[nodiscard]] virtual StructureType type() const noexcept = 0;
    virtual void collect_anchors(const StructureGenerationContext& context, std::vector<StructureAnchor>& anchors) const = 0;
};

class StructureRegistry {
public:
    static StructureRegistry& instance();

    [[nodiscard]] std::vector<StructureBlockEdit> generate(const StructureAnchor& anchor, const StructureGenerationContext& context) const;
    [[nodiscard]] std::vector<StructureBlockEdit> generate_overlapping(const StructureGenerationContext& context) const;

private:
    StructureRegistry();

    struct RegisteredStructure
    {
        std::unique_ptr<IStructureGenerator> generator{};
        std::unique_ptr<IStructurePlacementStrategy> placement{};
    };

    std::unordered_map<StructureType, RegisteredStructure> _structures;
};

#pragma once

#include <memory>
#include <unordered_map>

#include "world/structures/structure.h"

class ITreeVariantGenerator
{
public:
    virtual ~ITreeVariantGenerator() = default;

    [[nodiscard]] virtual TreeVariant variant() const noexcept = 0;
    [[nodiscard]] virtual float max_radius_world() const noexcept = 0;
    [[nodiscard]] virtual float max_height_world() const noexcept = 0;
    virtual void append_structure(
        const StructureAnchor& anchor,
        float blockWorldSize,
        std::vector<StructureBlockEdit>& edits) const = 0;
};

class TreeStructureGenerator final : public IStructureGenerator
{
public:
    TreeStructureGenerator();

    [[nodiscard]] StructureType type() const noexcept override;
    [[nodiscard]] std::vector<StructureBlockEdit> generate(const StructureAnchor& anchor, const StructureGenerationContext& context) const override;
    [[nodiscard]] int max_variant_radius(float blockWorldSize) const noexcept;
    [[nodiscard]] int max_variant_height(float blockWorldSize) const noexcept;

private:
    std::unordered_map<TreeVariant, std::unique_ptr<ITreeVariantGenerator>> _variantGenerators;
};

class TreePlacementStrategy final : public IStructurePlacementStrategy
{
public:
    explicit TreePlacementStrategy(const TreeStructureGenerator& generator);

    [[nodiscard]] StructureType type() const noexcept override;
    void collect_anchors(const StructureGenerationContext& context, std::vector<StructureAnchor>& anchors) const override;

private:
    const TreeStructureGenerator& _generator;
};

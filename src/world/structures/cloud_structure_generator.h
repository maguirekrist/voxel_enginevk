#pragma once

#include "world/structures/structure.h"

class CloudStructureGenerator final : public IStructureGenerator
{
public:
    [[nodiscard]] StructureType type() const noexcept override;
    [[nodiscard]] std::vector<StructureBlockEdit> generate(const StructureAnchor& anchor, const StructureGenerationContext& context) const override;
    [[nodiscard]] int max_radius() const noexcept;
    [[nodiscard]] int max_height() const noexcept;
};

class CloudPlacementStrategy final : public IStructurePlacementStrategy
{
public:
    explicit CloudPlacementStrategy(const CloudStructureGenerator& generator);

    [[nodiscard]] StructureType type() const noexcept override;
    void collect_anchors(const StructureGenerationContext& context, std::vector<StructureAnchor>& anchors) const override;

private:
    const CloudStructureGenerator& _generator;
};

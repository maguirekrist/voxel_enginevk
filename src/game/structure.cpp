#include "structure.h"

#include "cloud_structure_generator.h"
#include "tree_structure_generator.h"

StructureRegistry& StructureRegistry::instance()
{
    static StructureRegistry instance;
    return instance;
}

std::vector<StructureBlockEdit> StructureRegistry::generate(const StructureAnchor& anchor, const StructureGenerationContext& context) const
{
    const auto it = _structures.find(anchor.type);
    if (it == _structures.end() || it->second.generator == nullptr)
    {
        return {};
    }

    return it->second.generator->generate(anchor, context);
}

std::vector<StructureBlockEdit> StructureRegistry::generate_overlapping(const StructureGenerationContext& context) const
{
    std::vector<StructureBlockEdit> edits;
    std::vector<StructureAnchor> anchors;

    for (const auto& [type, registered] : _structures)
    {
        if (registered.generator == nullptr || registered.placement == nullptr)
        {
            continue;
        }

        anchors.clear();
        registered.placement->collect_anchors(context, anchors);

        for (const StructureAnchor& anchor : anchors)
        {
            std::vector<StructureBlockEdit> generated = registered.generator->generate(anchor, context);
            edits.insert(edits.end(), generated.begin(), generated.end());
        }
    }

    return edits;
}

StructureRegistry::StructureRegistry()
{
    auto treeGenerator = std::make_unique<TreeStructureGenerator>();
    auto treePlacement = std::make_unique<TreePlacementStrategy>(*treeGenerator);
    auto cloudGenerator = std::make_unique<CloudStructureGenerator>();
    auto cloudPlacement = std::make_unique<CloudPlacementStrategy>(*cloudGenerator);

    _structures.emplace(
        StructureType::TREE,
        RegisteredStructure{
            .generator = std::move(treeGenerator),
            .placement = std::move(treePlacement)
        });
    _structures.emplace(
        StructureType::CLOUD,
        RegisteredStructure{
            .generator = std::move(cloudGenerator),
            .placement = std::move(cloudPlacement)
        });
}

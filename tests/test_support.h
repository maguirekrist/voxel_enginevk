#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <ranges>
#include <vector>

#include "constants.h"
#include "config/json_document_store.h"
#include "game/block.h"
#include "game/chunk.h"
#include "game/structure.h"
#include "world/chunk_neighborhood.h"

namespace test_support
{
    class TestJsonDocumentStore final : public config::IJsonDocumentStore
    {
    public:
        explicit TestJsonDocumentStore(std::filesystem::path rootPath) : _rootPath(std::move(rootPath))
        {
        }

        [[nodiscard]] std::optional<nlohmann::json> load(const std::filesystem::path& path) const override
        {
            const std::filesystem::path resolved = _rootPath / path;
            if (!std::filesystem::exists(resolved))
            {
                return std::nullopt;
            }

            std::ifstream input(resolved);
            nlohmann::json document{};
            input >> document;
            return document;
        }

        void save(const std::filesystem::path& path, const nlohmann::json& document) const override
        {
            const std::filesystem::path resolved = _rootPath / path;
            std::filesystem::create_directories(resolved.parent_path());
            std::ofstream output(resolved, std::ios::trunc);
            output << document.dump(2) << '\n';
        }

    private:
        std::filesystem::path _rootPath{};
    };

    inline std::shared_ptr<ChunkData> make_empty_chunk(const ChunkCoord coord)
    {
        auto chunk = std::make_shared<ChunkData>();
        chunk->coord = coord;
        chunk->position = glm::ivec2(coord.x * CHUNK_SIZE, coord.z * CHUNK_SIZE);

        for (int x = 0; x < CHUNK_SIZE; ++x)
        {
            for (int y = 0; y < CHUNK_HEIGHT; ++y)
            {
                for (int z = 0; z < CHUNK_SIZE; ++z)
                {
                    chunk->blocks[x][y][z] = Block{
                        ._solid = false,
                        ._sunlight = 0,
                        ._type = BlockType::AIR
                    };
                }
            }
        }

        return chunk;
    }

    inline ChunkNeighborhood make_empty_neighborhood(const std::shared_ptr<ChunkData>& center)
    {
        return ChunkNeighborhood{
            .center = center,
            .north = make_empty_chunk({0, 1}),
            .south = make_empty_chunk({0, -1}),
            .east = make_empty_chunk({-1, 0}),
            .west = make_empty_chunk({1, 0}),
            .northEast = make_empty_chunk({-1, 1}),
            .northWest = make_empty_chunk({1, 1}),
            .southEast = make_empty_chunk({-1, -1}),
            .southWest = make_empty_chunk({1, -1})
        };
    }

    inline bool contains_block(
        const std::vector<StructureBlockEdit>& edits,
        const glm::ivec3 worldPosition,
        const BlockType blockType)
    {
        return std::ranges::any_of(edits, [&](const StructureBlockEdit& edit)
        {
            return edit.worldPosition == worldPosition && edit.block._type == blockType;
        });
    }
}

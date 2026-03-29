#pragma once

#include <any>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ui_draw_commands.h"
#include "ui_signal.h"
#include "ui_world.h"

namespace ui
{
    struct FrameDescriptor
    {
        Extent2D viewport{};
        double deltaTimeSeconds{0.0};
        uint64_t frameIndex{0};
    };

    struct ScreenSubmission
    {
        ScreenId id{};
        std::string debugName{};
        bool visible{true};
        bool modal{false};
    };

    struct LayerSubmission
    {
        LayerId id{};
        std::string debugName{};
        LayerInputPolicy inputPolicy{LayerInputPolicy::Passthrough};
        int sortKey{0};
    };

    struct SubmittedModel
    {
        ModelId id{};
        ElementId ownerElement{};
        std::string debugName{};
        std::any payload{};
    };

    struct FrameSnapshot
    {
        FrameDescriptor descriptor{};
        std::vector<ScreenSubmission> screens{};
        std::vector<LayerSubmission> layers{};
        std::vector<SubmittedModel> models{};
        std::vector<DrawCommand> drawCommands{};
        std::vector<PreparedTextCommand> preparedTextCommands{};
        std::vector<Signal> signals{};
        std::vector<WorldLabel> worldLabels{};
    };

    class FrameBuilder
    {
    public:
        FrameBuilder() = default;
        explicit FrameBuilder(FrameSnapshot* snapshot) : _snapshot(snapshot) {}

        [[nodiscard]] const FrameDescriptor& descriptor() const
        {
            return _snapshot->descriptor;
        }

        void declare_screen(const ScreenId id, std::string_view debugName, const bool visible = true, const bool modal = false) const
        {
            if (_snapshot == nullptr)
            {
                return;
            }

            _snapshot->screens.push_back(ScreenSubmission{
                .id = id,
                .debugName = std::string(debugName),
                .visible = visible,
                .modal = modal
            });
        }

        void declare_layer(
            const LayerId id,
            std::string_view debugName,
            const LayerInputPolicy inputPolicy = LayerInputPolicy::Passthrough,
            const int sortKey = 0) const
        {
            if (_snapshot == nullptr)
            {
                return;
            }

            _snapshot->layers.push_back(LayerSubmission{
                .id = id,
                .debugName = std::string(debugName),
                .inputPolicy = inputPolicy,
                .sortKey = sortKey
            });
        }

        template <typename T>
        void submit_model(const ModelId id, const ElementId ownerElement, std::string_view debugName, T&& payload) const
        {
            if (_snapshot == nullptr)
            {
                return;
            }

            _snapshot->models.push_back(SubmittedModel{
                .id = id,
                .ownerElement = ownerElement,
                .debugName = std::string(debugName),
                .payload = std::any(std::forward<T>(payload))
            });
        }

        void submit_draw_command(const DrawCommand& command) const
        {
            if (_snapshot == nullptr)
            {
                return;
            }

            _snapshot->drawCommands.push_back(command);
        }

    private:
        FrameSnapshot* _snapshot{nullptr};
    };
}

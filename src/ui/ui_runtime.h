#pragma once

#include <optional>
#include <unordered_map>
#include <vector>

#include "ui_frame_builder.h"
#include "ui_text_system.h"

namespace ui
{
    struct RetainedElementState
    {
        bool visible{true};
        bool expanded{false};
        float animationProgress{0.0f};
    };

    struct InteractionState
    {
        std::optional<ElementId> focusedElement{};
        std::optional<ElementId> hoveredElement{};
        std::vector<ScreenId> modalStack{};
        std::optional<ElementId> activeDragElement{};
        std::optional<ElementId> textInputTarget{};
        std::unordered_map<ElementId, RetainedElementState> retainedElements{};
    };

    class Runtime
    {
    public:
        void begin_frame(const FrameDescriptor& descriptor);
        [[nodiscard]] FrameBuilder frame_builder();
        [[nodiscard]] WorldLabelCollector world_label_collector();
        void finalize_frame();

        void submit_signal(const Signal& signal);
        void set_focused_element(std::optional<ElementId> element);
        void set_hovered_element(std::optional<ElementId> element);
        void push_modal(ScreenId screenId);
        void pop_modal();

        [[nodiscard]] RetainedElementState& retained_state(ElementId elementId);
        [[nodiscard]] const InteractionState& interaction_state() const noexcept;
        [[nodiscard]] InteractionState& interaction_state() noexcept;
        [[nodiscard]] const FrameSnapshot& current_frame() const noexcept;
        [[nodiscard]] bool is_frame_active() const noexcept;
        [[nodiscard]] TextSystem& text_system() noexcept;
        [[nodiscard]] const TextSystem& text_system() const noexcept;

    private:
        FrameSnapshot _currentFrame{};
        InteractionState _interactionState{};
        std::vector<Signal> _pendingSignals{};
        TextSystem _textSystem{};
        bool _frameActive{false};
    };
}

#include "ui_runtime.h"

namespace ui
{
    void Runtime::begin_frame(const FrameDescriptor& descriptor)
    {
        _frameActive = true;
        _currentFrame = {};
        _currentFrame.descriptor = descriptor;
        _currentFrame.signals = std::move(_pendingSignals);
        _pendingSignals.clear();

        // Hover is frame-local, while focus and modal ownership are retained.
        _interactionState.hoveredElement.reset();
    }

    FrameBuilder Runtime::frame_builder()
    {
        return FrameBuilder{ &_currentFrame };
    }

    WorldLabelCollector Runtime::world_label_collector()
    {
        return WorldLabelCollector{ &_currentFrame.worldLabels };
    }

    void Runtime::finalize_frame()
    {
        _currentFrame.preparedTextCommands.clear();
        for (const DrawCommand& drawCommand : _currentFrame.drawCommands)
        {
            if (const TextCommand* textCommand = std::get_if<TextCommand>(&drawCommand.payload))
            {
                if (std::optional<PreparedTextCommand> prepared = _textSystem.prepare_text_command(*textCommand); prepared.has_value())
                {
                    _currentFrame.preparedTextCommands.push_back(std::move(prepared.value()));
                }
            }
        }

        _frameActive = false;
    }

    void Runtime::submit_signal(const Signal& signal)
    {
        if (_frameActive)
        {
            _currentFrame.signals.push_back(signal);
            return;
        }

        _pendingSignals.push_back(signal);
    }

    void Runtime::set_focused_element(const std::optional<ElementId> element)
    {
        _interactionState.focusedElement = element;
    }

    void Runtime::set_hovered_element(const std::optional<ElementId> element)
    {
        _interactionState.hoveredElement = element;
    }

    void Runtime::push_modal(const ScreenId screenId)
    {
        _interactionState.modalStack.push_back(screenId);
    }

    void Runtime::pop_modal()
    {
        if (!_interactionState.modalStack.empty())
        {
            _interactionState.modalStack.pop_back();
        }
    }

    RetainedElementState& Runtime::retained_state(const ElementId elementId)
    {
        return _interactionState.retainedElements[elementId];
    }

    const InteractionState& Runtime::interaction_state() const noexcept
    {
        return _interactionState;
    }

    InteractionState& Runtime::interaction_state() noexcept
    {
        return _interactionState;
    }

    const FrameSnapshot& Runtime::current_frame() const noexcept
    {
        return _currentFrame;
    }

    bool Runtime::is_frame_active() const noexcept
    {
        return _frameActive;
    }

    TextSystem& Runtime::text_system() noexcept
    {
        return _textSystem;
    }

    const TextSystem& Runtime::text_system() const noexcept
    {
        return _textSystem;
    }
}

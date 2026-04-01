#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <utility>

#include "editing/document_command_history.h"

namespace editing
{
    template <typename TDocument>
    class DocumentEditorSession
    {
    public:
        DocumentEditorSession(
            DocumentCommandHistory<TDocument>& history,
            TDocument& document,
            std::string_view documentLabel) :
            _history(history),
            _document(document),
            _documentLabel(documentLabel)
        {
        }

        template <typename TMutator, typename TOnChanged>
        bool apply(
            std::string_view description,
            TMutator&& mutator,
            TOnChanged&& onChanged,
            std::string* const statusMessage = nullptr)
        {
            if (!editing::apply_snapshot_edit(
                    _history,
                    _document,
                    std::string(description),
                    std::forward<TMutator>(mutator)))
            {
                return false;
            }

            std::forward<TOnChanged>(onChanged)();
            if (statusMessage != nullptr)
            {
                *statusMessage = std::string("Edited ") + std::string(_documentLabel) + ": " + std::string(description);
            }
            return true;
        }

        template <typename TOnChanged>
        bool undo(TOnChanged&& onChanged, std::string* const statusMessage = nullptr)
        {
            if (!_history.undo(_document))
            {
                if (statusMessage != nullptr)
                {
                    *statusMessage = "Nothing to undo";
                }
                return false;
            }

            std::forward<TOnChanged>(onChanged)();
            if (statusMessage != nullptr)
            {
                *statusMessage = std::string("Undo: ") + std::string(_history.redo_description());
            }
            return true;
        }

        template <typename TOnChanged>
        bool redo(TOnChanged&& onChanged, std::string* const statusMessage = nullptr)
        {
            if (!_history.redo(_document))
            {
                if (statusMessage != nullptr)
                {
                    *statusMessage = "Nothing to redo";
                }
                return false;
            }

            std::forward<TOnChanged>(onChanged)();
            if (statusMessage != nullptr)
            {
                *statusMessage = std::string("Redo: ") + std::string(_history.undo_description());
            }
            return true;
        }

        [[nodiscard]] DocumentCommandHistory<TDocument>& history() noexcept
        {
            return _history;
        }

        [[nodiscard]] const DocumentCommandHistory<TDocument>& history() const noexcept
        {
            return _history;
        }

    private:
        DocumentCommandHistory<TDocument>& _history;
        TDocument& _document;
        std::string_view _documentLabel;
    };
}

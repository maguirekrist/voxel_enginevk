#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace editing
{
    template <typename TDocument>
    class IDocumentCommand
    {
    public:
        virtual ~IDocumentCommand() = default;

        [[nodiscard]] virtual std::string_view description() const noexcept = 0;
        virtual void undo(TDocument& document) const = 0;
        virtual void redo(TDocument& document) const = 0;
    };

    template <typename TDocument>
    class SnapshotDocumentCommand final : public IDocumentCommand<TDocument>
    {
    public:
        SnapshotDocumentCommand(std::string description, TDocument before, TDocument after) :
            _description(std::move(description)),
            _before(std::move(before)),
            _after(std::move(after))
        {
        }

        [[nodiscard]] std::string_view description() const noexcept override
        {
            return _description;
        }

        void undo(TDocument& document) const override
        {
            document = _before;
        }

        void redo(TDocument& document) const override
        {
            document = _after;
        }

    private:
        std::string _description{};
        TDocument _before{};
        TDocument _after{};
    };

    template <typename TDocument>
    class DocumentCommandHistory
    {
    public:
        explicit DocumentCommandHistory(const size_t maxHistory = 50) :
            _maxHistory(std::max<size_t>(1, maxHistory))
        {
        }

        [[nodiscard]] size_t max_history() const noexcept
        {
            return _maxHistory;
        }

        void set_max_history(const size_t maxHistory)
        {
            _maxHistory = std::max<size_t>(1, maxHistory);
            trim_to_capacity();
        }

        void clear()
        {
            _undoStack.clear();
            _redoStack.clear();
        }

        [[nodiscard]] bool can_undo() const noexcept
        {
            return !_undoStack.empty();
        }

        [[nodiscard]] bool can_redo() const noexcept
        {
            return !_redoStack.empty();
        }

        [[nodiscard]] size_t undo_count() const noexcept
        {
            return _undoStack.size();
        }

        [[nodiscard]] size_t redo_count() const noexcept
        {
            return _redoStack.size();
        }

        [[nodiscard]] std::string_view undo_description() const noexcept
        {
            return can_undo() ? _undoStack.back()->description() : std::string_view{};
        }

        [[nodiscard]] std::string_view redo_description() const noexcept
        {
            return can_redo() ? _redoStack.back()->description() : std::string_view{};
        }

        void push_executed(std::unique_ptr<IDocumentCommand<TDocument>> command)
        {
            if (command == nullptr)
            {
                return;
            }

            _redoStack.clear();
            _undoStack.push_back(std::move(command));
            trim_to_capacity();
        }

        bool undo(TDocument& document)
        {
            if (!can_undo())
            {
                return false;
            }

            std::unique_ptr<IDocumentCommand<TDocument>> command = std::move(_undoStack.back());
            _undoStack.pop_back();
            command->undo(document);
            _redoStack.push_back(std::move(command));
            return true;
        }

        bool redo(TDocument& document)
        {
            if (!can_redo())
            {
                return false;
            }

            std::unique_ptr<IDocumentCommand<TDocument>> command = std::move(_redoStack.back());
            _redoStack.pop_back();
            command->redo(document);
            _undoStack.push_back(std::move(command));
            trim_to_capacity();
            return true;
        }

    private:
        void trim_to_capacity()
        {
            if (_undoStack.size() <= _maxHistory)
            {
                return;
            }

            const size_t overflow = _undoStack.size() - _maxHistory;
            _undoStack.erase(_undoStack.begin(), _undoStack.begin() + static_cast<std::ptrdiff_t>(overflow));
        }

        size_t _maxHistory{50};
        std::vector<std::unique_ptr<IDocumentCommand<TDocument>>> _undoStack{};
        std::vector<std::unique_ptr<IDocumentCommand<TDocument>>> _redoStack{};
    };

    template <typename TDocument>
    [[nodiscard]] std::unique_ptr<IDocumentCommand<TDocument>> make_snapshot_command(
        std::string description,
        TDocument before,
        TDocument after)
    {
        return std::make_unique<SnapshotDocumentCommand<TDocument>>(
            std::move(description),
            std::move(before),
            std::move(after));
    }

    template <typename TDocument, typename TMutator>
    bool apply_snapshot_edit(
        DocumentCommandHistory<TDocument>& history,
        TDocument& document,
        std::string description,
        TMutator&& mutator)
    {
        TDocument before = document;
        mutator(document);

        if (document == before)
        {
            return false;
        }

        history.push_executed(make_snapshot_command<TDocument>(
            std::move(description),
            std::move(before),
            document));
        return true;
    }
}

#pragma once

#include <vk_mesh.h>

class RenderQueue {
public:

    void add(std::unique_ptr<RenderObject>&& renderObject)
    {
        switch(renderObject->layer)
        {
            case RenderLayer::Opaque:
                _opaqueQueue.push_back(std::move(renderObject));
                break;
            case RenderLayer::Transparent:
                _transparentQueue.push_back(std::move(renderObject));
                break;
        }
    }

    void add(std::vector<std::unique_ptr<RenderObject>>&& renderObjects, const RenderLayer type)
    {
        auto& targetQueue = (type == RenderLayer::Opaque)
                            ? _opaqueQueue
                            : _transparentQueue;

        for (auto& obj : renderObjects) {
            targetQueue.push_back(std::move(obj));
        }
    }

    void clear() {
        _opaqueQueue.clear();
        _transparentQueue.clear();
    }

    const std::vector<std::unique_ptr<RenderObject>>& getOpaqueQueue() const {
        return _opaqueQueue;
    }

    const std::vector<std::unique_ptr<RenderObject>>& getTransparentQueue() const {
        return _transparentQueue;
    }

private:
    std::vector<std::unique_ptr<RenderObject>> _opaqueQueue{};
    std::vector<std::unique_ptr<RenderObject>> _transparentQueue{};
};
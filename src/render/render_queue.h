#pragma once

#include <vk_mesh.h>

class RenderQueue {
public:

    void add(const RenderObject* renderObject)
    {
        switch(renderObject->layer)
        {
            case RenderLayer::Opaque:
                _opaqueQueue.push_back(renderObject);
                break;
            case RenderLayer::Transparent:
                _transparentQueue.push_back(renderObject);
                break;
        }
    }

    void add(const std::vector<std::unique_ptr<RenderObject>>& renderObjects, const RenderLayer type)
    {
        auto& targetQueue = (type == RenderLayer::Opaque)
                            ? _opaqueQueue
                            : _transparentQueue;

        for (auto& obj : renderObjects) {
            targetQueue.push_back(obj.get());
        }
    }

    void remove(const RenderObject* renderObject)
    {
        auto& targetQueue = (renderObject->layer == RenderLayer::Opaque)
                            ? _opaqueQueue
                            : _transparentQueue;

        auto it = std::ranges::find(targetQueue, renderObject);
        if (it != targetQueue.end()) {
            targetQueue.erase(it);
        }
    }

    void remove(const std::vector<std::unique_ptr<RenderObject>>& objs, const RenderLayer type)
    {
        auto& targetQueue = (type == RenderLayer::Opaque)
                            ? _opaqueQueue
                            : _transparentQueue;
        for (auto& obj : objs) {
            auto it = std::ranges::find(targetQueue, obj.get());
            if (it != targetQueue.end()) {
                targetQueue.erase(it);
            }
        }
    }

    void clear() {
        _opaqueQueue.clear();
        _transparentQueue.clear();
    }

    const std::vector<const RenderObject*>& getOpaqueQueue() const {
        return _opaqueQueue;
    }

    const std::vector<const RenderObject*>& getTransparentQueue() const {
        return _transparentQueue;
    }

private:
    std::vector<const RenderObject*> _opaqueQueue{};
    std::vector<const RenderObject*> _transparentQueue{};
};
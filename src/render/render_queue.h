#pragma once

#include <vk_mesh.h>

class RenderQueue {
public:

    void add(const std::shared_ptr<RenderObject>& renderObject)
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

    void add(const std::vector<std::shared_ptr<RenderObject>>& renderObjects, RenderLayer type)
    {
        switch(type)
        {
            case RenderLayer::Opaque:
                _opaqueQueue.insert(_opaqueQueue.end(), renderObjects.begin(), renderObjects.end());
                break;
            case RenderLayer::Transparent:
                _transparentQueue.insert(_transparentQueue.end(), renderObjects.begin(), renderObjects.end());
                break;
        }
    }

    void clear() {
        _opaqueQueue.clear();
        _transparentQueue.clear();
    }

    const std::vector<std::shared_ptr<RenderObject>>& getOpaqueQueue() const {
        return _opaqueQueue;
    }

    const std::vector<std::shared_ptr<RenderObject>>& getTransparentQueue() const {
        return _transparentQueue;
    }

private:
    std::vector<std::shared_ptr<RenderObject>> _opaqueQueue;
    std::vector<std::shared_ptr<RenderObject>> _transparentQueue;
};
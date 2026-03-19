#pragma once

#include "collections/spare_set.h"
#include "render_primitives.h"

struct SceneRenderState
{
    dev_collections::sparse_set<RenderObject> opaqueObjects{};
    dev_collections::sparse_set<RenderObject> transparentObjects{};
};

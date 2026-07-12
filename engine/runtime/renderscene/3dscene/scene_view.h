#pragma once
#include "core/math/math.h"

namespace toy3d
{
    class SceneView
    {
    public:
        SceneView(){};
        ~SceneView(){};

        vec2 get_view_size(){return view_size;}
    private:
        vec2 view_size;
    };
}

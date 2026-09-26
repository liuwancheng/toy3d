#pragma once

#include "math/matrix4.h"

namespace toy3d
{
    class SceneComponent;

    class EditorViewportGizmo
    {
      public:
        void begin_frame();
        void draw_toolbar();
        void handle_shortcuts(bool viewport_hovered);
        bool manipulate(SceneComponent& root, const Matrix4& view, const Matrix4& projection,
                        float x, float y, float width, float height);

      private:
        enum class Operation
        {
            Translate,
            Rotate,
            Scale
        };

        Operation operation_ = Operation::Translate;
        bool local_mode_ = false;
    };
} // namespace toy3d

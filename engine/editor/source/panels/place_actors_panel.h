#pragma once

#include "imgui.h"

namespace toy3d
{
    class PlaceActorsPanel final
    {
      public:
        void draw();
        void clear() { filter_.Clear(); }

      private:
        ImGuiTextFilter filter_;
    };
}

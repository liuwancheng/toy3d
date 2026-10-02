#pragma once

#include "imgui.h"

namespace toy3d
{
    class ActorTypeRegistry;
    class PlaceActorsPanel final
    {
      public:
        void draw(const ActorTypeRegistry* types = nullptr);
        void clear() { filter_.Clear(); }

      private:
        ImGuiTextFilter filter_;
    };
}

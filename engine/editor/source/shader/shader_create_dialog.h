#pragma once

#include <array>
#include <string>

namespace toy3d
{
    class ShaderWorkflow;
    class EditorNotifications;

    class ShaderCreateDialog final
    {
      public:
        void request();
        void draw(ShaderWorkflow& shaders, EditorNotifications& notifications);
        bool active() const
        {
            return active_;
        }

      private:
        std::array<char, 256u> name_{};
        std::array<char, 512u> path_{};
        std::string error_;
        std::string created_name_;
        bool phong_ = false;
        bool active_ = false;
        bool open_ = false;
    };
} // namespace toy3d

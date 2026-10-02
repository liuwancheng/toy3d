#pragma once

#include "platform/window_interface.h"
#include "rendercore/hit_proxy.h"
#include "rendercore/view/scene_view.h"
#include "ui/ui_texture_work.h"
#include "rendercore/material/material_shader_map_validation.h"
#include "rendercore/shader/builtin_shader_update.h"

#include <vector>
#include <memory>

namespace toy3d
{
    class Engine;
    class World;
    class SceneInterface;
    class TaskGraphInterface;
    struct SceneRenderFeedback;

    // Application owns project-level policy and state. Engine owns the World
    // and Window and binds them once before World initialization; gameplay
    // updates remain in Actor/Component tick rather than a World-wide callback.
    class Application
    {
      public:
        virtual ~Application() = default;

      protected:
        World& world();
        const World& world() const;
        IWindow& window();
        const IWindow& window() const;

        virtual bool on_initialize() = 0;
        virtual bool starts_world_play() const
        {
            return true;
        }
        virtual void on_tick(double)
        {
        }
        // Called only while the GT Dear ImGui frame is active. Applications
        // may build widgets through ImGui core but receive no renderer service.
        virtual void on_build_ui()
        {
        }
        // An embedded scene viewport supplies its own pixel extent after UI layout.
        virtual bool on_scene_viewport_extent(Extent& extent) const
        {
            return false;
        }
        virtual bool on_hit_proxy_request(HitProxyRequest& request)
        {
            return false;
        }
        virtual void on_hit_proxy_result(const HitProxyResult& result)
        {
        }
        virtual void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const = 0;
        virtual void on_shutdown()
        {
        }
        // Editor may keep the frame loop alive while resolving unsaved work.
        virtual bool on_close_requested()
        {
            return true;
        }
        virtual bool uses_preview_scene() const
        {
            return false;
        }
        virtual bool uses_play_scene() const
        {
            return false;
        }
        virtual void on_initialize_play_scene(SceneInterface&)
        {
        }
        virtual bool renders_play_scene() const
        {
            return false;
        }
        virtual std::shared_ptr<SceneRenderFeedback> scene_render_feedback() const
        {
            return {};
        }
        // A game viewport may accept input despite ImGui owning its Image widget.
        // Return false to retain normal UI capture. Text/modal always wins.
        virtual bool game_viewport_input(bool& mouse, bool& keyboard) const
        {
            return false;
        }
        virtual bool on_initialize_preview_scene(SceneInterface&, TaskGraphInterface&)
        {
            return true;
        }
        virtual void on_collect_ui_render_work(UiRenderWork&)
        {
        }
        virtual void on_collect_material_validation(std::vector<MaterialShaderMapValidationRef>&)
        {
        }
        virtual void on_collect_builtin_shader_updates(std::vector<BuiltinShaderUpdateRef>&)
        {
        }
        virtual void on_ui_texture_result(UiTextureResult)
        {
        }
        virtual std::vector<ImGuiTextureId> ui_texture_ids() const
        {
            return {};
        }

      private:
        friend class Engine;

        bool initialize(World& world, IWindow& window);
        void tick(double delta_time);
        void build_ui();
        bool scene_viewport_extent(Extent& extent) const;
        bool hit_proxy_request(HitProxyRequest& request);
        void hit_proxy_result(const HitProxyResult& result);
        void build_scene_views(std::vector<SceneView>& views, const Extent& extent) const;
        void shutdown();

        // Engine owns both observers and keeps them alive for the complete
        // initialize/tick/view/shutdown interval.
        World* world_ = nullptr;
        IWindow* window_ = nullptr;
    };
} // namespace toy3d

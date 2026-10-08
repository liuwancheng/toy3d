#pragma once

#include "gamescene/scene_assembly.h"
#include "gamescene/scene_geometry.h"
#include "rendercore/material/material_library.h"
#include "rendercore/scene_interface.h"

#include <functional>
#include <memory>
#include <string>

namespace toy3d
{
    class EditorWorkspace;
    class AssetLoader;
    enum class EditorPlayState
    {
        Stopped,
        Starting,
        Playing,
        Paused
    };
    enum class EditorPlayAction
    {
        None,
        Play,
        Pause,
        Resume,
        Stop
    };

    // Editor-owned GT gameplay domain. Author session/history never bind this
    // World. RenderScene ownership stays in Renderer; FIFO drain precedes release.
    class EditorPlaySession final
    {
      public:
        ~EditorPlaySession();
        // Assembly resolves environments through the shared loader instead of decoding on the GT.
        void set_asset_loader(AssetLoader& assets)
        {
            assets_ = &assets;
        }
        bool start(const SceneAssetData& data, EditorWorkspace& workspace, const ActorTypeRegistry& actors,
                   const std::function<ShaderMapCollectionRef(
                       const std::string&, const std::vector<shader::ShaderPermutationSelection>&)>& programs,
                   SceneInterface& scene);
        void tick(double delta_seconds);
        void pause();
        void resume();
        bool stop();
        bool active() const;
        EditorPlayState state() const;
        const World* world() const;
        const std::string& error() const;
        std::shared_ptr<SceneRenderFeedback> feedback() const;
        void request(EditorPlayAction action);
        EditorPlayAction take_action();

      private:
        std::unique_ptr<World> world_;
        SceneGeometry geometry_;
        std::unique_ptr<MaterialLibrary> materials_;
        AssetLoader* assets_ = nullptr;
        std::shared_ptr<SceneRenderFeedback> feedback_;
        EditorPlayState state_ = EditorPlayState::Stopped;
        EditorPlayAction action_ = EditorPlayAction::None;
        double preparation_seconds_ = 0.0;
        std::string error_;
        bool input_session_owned_ = false;
    };
} // namespace toy3d

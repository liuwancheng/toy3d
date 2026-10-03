#include "application/application.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "imgui.h"
#include "platform/platform_defines.h"

#if WITH_WIN
#include <windows.h>
#include <shlobj.h>
#include "platform/win/win32_window.h"
#endif

#include "config/command_line_parser.h"
#include "engine.h"
#include "file_system/native_platform_file.h"
#include "image/png_codec.h"
#include "scene/placement/actor_factory.h"
#include "scene/placement/asset_placement.h"
#include "scene/editor_command_history.h"
#include "gamescene/world/world.h"
#include "gamescene/actor/actor.h"
#include "gamescene/component/static_mesh_component.h"
#include "panels/content_browser_panel.h"
#include "assets/mesh/static_mesh_import_dialog.h"
#include "scene/editor_selection.h"
#include "rendercore/frame_synchronization.h"
#include "assets/thumbnails/asset_thumbnail_pool.h"
#include "workspace/editor_workspace.h"
#include "rendercore/shader/loaders/shader_map_entry_loader.h"
#include "rendercore/shader/shader_map_collection.h"
#include "rendercore/material/material_asset_builder.h"
bool check_editor_play_integration(toy3d::EditorWorkspace& workspace, void* platform_context);
#if WITH_MODEL_IMPORT
#include "assets/mesh/static_mesh_asset_tools.h"
#include "asset_pipeline/static_mesh_import.h"
#endif

namespace
{
    using namespace toy3d;

    struct TestState
    {
        bool complete = false;
        std::string error;
        int captures = 0;
        int uploads = 0;
    };

    class ThumbnailTestApplication final : public Application
    {
      public:
        ThumbnailTestApplication(EditorWorkspace& workspace, AssetId first, AssetId second, TestState& state)
            : workspace_(workspace), pool_(workspace), history_(factory_), first_(first), second_(second), state_(state)
        {
        }

      private:
        bool on_initialize() override
        {
            ImGui::GetIO().IniFilename = nullptr;
#if WITH_WIN
            if (!verify_native_drop())
            {
                return false;
            }
#endif
            return factory_.initialize();
        }
        bool starts_world_play() const override
        {
            return false;
        }
        bool uses_preview_scene() const override
        {
            return true;
        }
        bool on_initialize_preview_scene(SceneInterface& scene, TaskGraphInterface& tasks) override
        {
            return pool_.initialize(scene, factory_.default_material(), tasks);
        }
        void on_build_scene_views(std::vector<SceneView>& views, const Extent& extent) const override
        {
            views.emplace_back(Vector3(0, 0, -3), Quaternion::identity(), Vector3(0, 0, 1),
                               IntRect{0, 0, extent.width, extent.height}, extent, CameraProjectionMode::Perspective,
                               Radians(0.785398163f), 0.1f, 100.0f);
        }
        void on_collect_ui_render_work(UiRenderWork& work) override
        {
            pool_.collect_render_work(work);
            if (phase_ == 14 && work.preview.request_id)
            {
                stale_texture_ = work.preview.texture_id;
                preview_settings_.environment_rotation = -90.0f;
                // Advance the desired settings while the old frame is already dispatched.
                pool_.request_material_preview(preview_material_, 2u, preview_settings_);
                phase_ = 15;
            }
        }
        std::vector<ImGuiTextureId> ui_texture_ids() const override
        {
            return pool_.texture_ids();
        }
        void on_ui_texture_result(UiTextureResult result) override
        {
            const bool capture = !result.bgra_pixels.empty();
            if (result.succeeded())
            {
                if (capture)
                {
                    ++state_.captures;
                }
                else
                {
                    ++state_.uploads;
                }
            }
            const bool stale_capture = result.texture_id == stale_texture_;
            if (capture && phase_ == 1)
            {
                second_thumbnail_hash_ = sha256(result.bgra_pixels);
            }
            if (capture && phase_ == 12 && result.extent.width == thumbnail_default_size)
            {
                if (sha256(result.bgra_pixels) != second_thumbnail_hash_)
                {
                    stop("Material preview floor, lighting, or environment leaked into a subsequent thumbnail.");
                    return;
                }
                thumbnail_after_preview_ = true;
            }
            if (capture && phase_ >= 5 && !stale_capture && result.extent.width != thumbnail_default_size)
            {
                if (const char* capture_root = std::getenv("TOY3D_TEST_CAPTURE_DIR"))
                {
                    Rgba8Image image;
                    image.width = static_cast<std::uint32_t>(result.extent.width);
                    image.height = static_cast<std::uint32_t>(result.extent.height);
                    image.pixels = result.bgra_pixels;
                    for (std::size_t pixel = 0u; pixel < image.pixels.size(); pixel += 4u)
                    {
                        std::swap(image.pixels[pixel], image.pixels[pixel + 2u]);
                    }
                    std::vector<std::uint8_t> png;
                    const PhysicalPath root(capture_root);
                    NativePlatformFile files;
                    const std::string name = "/pbr-preview-phase-" + std::to_string(phase_) + ".png";
                    if (!encode_png(image, png).succeeded() || !files.create_directories(root).succeeded() ||
                        !files.write_binary(PhysicalPath(root.utf8() + name), png, FileWriteMode::Truncate).succeeded())
                    {
                        stop("Could not save the optional production PBR preview capture.");
                        return;
                    }
                }
                material_image_hash_ = sha256(result.bgra_pixels);
                material_pixels_ = result.bgra_pixels;
                material_extent_ = result.extent;
                material_has_color_ = std::any_of(result.bgra_pixels.begin(), result.bgra_pixels.end(),
                                                  [](std::uint8_t byte)
                                                  {
                                                      return byte > 0u && byte < 255u;
                                                  });
            }
            pool_.on_texture_result(std::move(result));
            if (stale_capture)
            {
                stale_rejected_ = pool_.request_material_preview(preview_material_, 2u, preview_settings_).texture_id ==
                                  prior_texture_;
                if (!stale_rejected_)
                {
                    stop("A stale preview settings revision replaced the current image.");
                }
            }
            if (phase_ == 3 && capture)
            {
                const auto path = VirtualPath::parse("/Project/first.asset");
                const auto published = workspace_.asset_pairs().read(path.value());
                if (!published.succeeded())
                {
                    stop(published.status().message);
                    return;
                }
                auto segments = published.value().meta.segments;
                for (auto& segment : segments)
                {
                    if (segment.name == "future_editor_data")
                    {
                        segment.bytes = {9, 8, 7};
                    }
                }
                const auto changed = encode_asset_pair(workspace_.types(), published.value().description.index,
                                                       published.value().description.type_data, std::move(segments));
                if (!changed.succeeded())
                {
                    stop(changed.status().message);
                    return;
                }
                const auto saved =
                    workspace_.asset_pairs().publish(path.value(), changed.value(), FilePublishMode::Replace);
                if (!saved.succeeded())
                {
                    stop(saved.message);
                    return;
                }
                conflict_snapshot_ = sha256(changed.value().asset);
            }
        }
        void on_tick(double delta) override
        {
            elapsed_ += delta;
            if (elapsed_ > 60.0)
            {
                stop("Thumbnail integration timed out.");
                return;
            }
            pool_.tick();
            if (!started_)
            {
                started_ = true;
                if (!verify_asset_placement())
                {
                    return;
                }
            }
            const auto a = request(first_);
            const auto b = phase_ > 0 ? request(second_) : AssetThumbnailView{};
            if (phase_ != 3 && !a.error.empty())
            {
                stop(a.error);
                return;
            }
            if (phase_ > 0 && !b.error.empty())
            {
                stop(b.error);
                return;
            }
            if (phase_ == 0 && a.texture_id.valid() && !a.busy)
            {
                if (!verify_saved("/Project/first.asset", {1, 2, 3}))
                {
                    return;
                }
                pool_.generate(second_);
                phase_ = 1;
            }
            else if (phase_ == 1 && b.texture_id.valid() && !b.busy)
            {
                if (!verify_saved("/Project/second.asset", {1, 2, 3}))
                {
                    return;
                }
                pool_.invalidate();
                phase_ = 2;
            }
            else if (phase_ == 2 && a.texture_id.valid() && b.texture_id.valid() && !a.busy && !b.busy)
            {
                if (state_.captures < 2 || state_.uploads != 2)
                {
                    stop("Saved PNG reload did not use the image upload path: captures=" +
                         std::to_string(state_.captures) + ", uploads=" + std::to_string(state_.uploads));
                    return;
                }
                pool_.generate(first_);
                phase_ = 3;
            }
            else if (phase_ == 3 && !a.busy && !a.error.empty())
            {
                if (a.error.find("conflict") == std::string::npos)
                {
                    stop("Expected an asset save conflict.");
                    return;
                }
                const auto path = VirtualPath::parse("/Project/first.asset");
                const auto bytes = workspace_.files().read_binary(path.value());
                if (!bytes.succeeded() || sha256(bytes.value()) != conflict_snapshot_)
                {
                    stop("A stale thumbnail overwrote the changed asset.");
                    return;
                }
                pool_.generate(first_);
                phase_ = 4;
            }
            else if (phase_ == 4 && a.texture_id.valid() && !a.busy)
            {
                if (!verify_saved("/Project/first.asset", {9, 8, 7}))
                {
                    return;
                }
                ShaderMapEntryLoader loader(PhysicalPath(std::string(TOY3D_EDITOR_DEPLOY_ROOT) + "/shader/pbr"));
                const auto program = ShaderMapCollection::create_candidate(
                    loader.load_default_collection("Toy3d/Surface/PBR", ShaderPlatform::VulkanES31));
                if (!program.succeeded())
                {
                    stop(program.error);
                    return;
                }
                MaterialTextureValues textures;
                const auto defaults = resolve_builtin_material_texture_defaults(
                    workspace_.files(), workspace_.catalog().index,
                    program.collection->programs().front()->data().parameter_schema, textures);
                MaterialAssetData data;
                data.shader_name = "Toy3d/Surface/PBR";
                const auto made = create_material_from_asset(data, program.collection, textures);
                if (!defaults.succeeded() || !made.succeeded())
                {
                    stop(defaults.succeeded() ? made.status().message : defaults.message);
                    return;
                }
                preview_material_ = made.value();
                level_actor_count_ = world().actor_count();
                phase_ = 5;
            }
            if (phase_ >= 5 && preview_material_)
            {
                const auto preview =
                    pool_.request_material_preview(preview_material_, phase_ == 5 ? 1u : 2u, preview_settings_);
                if (phase_ == 16 && !preview.error.empty())
                {
                    if (preview.texture_id != prior_texture_ || world().actor_count() != level_actor_count_)
                    {
                        stop("A failed preview environment replaced the old image or mutated the level.");
                        return;
                    }
                    preview_settings_ = MaterialPreviewSettings{};
                    pool_.request_material_preview(preview_material_, 3u, preview_settings_);
                    phase_ = 17;
                }
                else if (!preview.error.empty())
                {
                    stop(preview.error);
                    return;
                }
                if (phase_ == 5 && preview.texture_id.valid() && !preview.busy)
                {
                    if (!material_has_color_)
                    {
                        stop("Production PBR preview sphere has no visible shaded pixels.");
                        return;
                    }
                    original_material_image_hash_ = material_image_hash_;
                    if (!preview_material_->set_scalar("metallic", 1.0f) ||
                        !preview_material_->set_scalar("roughness", 0.05f) ||
                        !preview_material_->set_vector("base_color", vec4(0.9f, 0.6f, 0.1f, 1.0f)))
                    {
                        stop("PBR preview parameter update failed.");
                        return;
                    }
                    phase_ = 6;
                }
                else if (phase_ == 6 && preview.texture_id.valid() && !preview.busy)
                {
                    if (material_image_hash_ == original_material_image_hash_ ||
                        world().actor_count() != level_actor_count_)
                    {
                        stop("PBR preview did not update or mutated the level World.");
                        return;
                    }
                    preview_settings_.show_floor = false;
                    preview_settings_.show_shadows = false;
                    phase_ = 7;
                }
                else if (phase_ >= 7 && phase_ <= 13 && preview.texture_id.valid() && !preview.busy)
                {
                    if (!verify_preview_pixels())
                    {
                        return;
                    }
                    if (phase_ == 7)
                    {
                        background_pixels_ = material_pixels_;
                        preview_settings_.show_environment = false;
                    }
                    else if (phase_ == 8)
                    {
                        preview_settings_.show_environment = true;
                        preview_settings_.environment_rotation = 90.0f;
                    }
                    else if (phase_ == 9)
                    {
                        prior_preview_pixels_ = material_pixels_;
                        preview_settings_.exposure_ev = 1.0f;
                    }
                    else if (phase_ == 10)
                    {
                        prior_preview_pixels_ = material_pixels_;
                        preview_settings_.show_floor = true;
                    }
                    else if (phase_ == 11)
                    {
                        prior_preview_pixels_ = material_pixels_;
                        preview_settings_.show_shadows = true;
                        pool_.generate(second_);
                    }
                    else if (phase_ == 12)
                    {
                        preview_settings_.extent = {320u, 256u};
                        preview_settings_.camera_yaw = 20.0f;
                        preview_settings_.camera_pitch = 15.0f;
                    }
                    else
                    {
                        prior_texture_ = preview.texture_id;
                        preview_settings_.environment_rotation = 120.0f;
                    }
                    ++phase_;
                }
                else if (phase_ == 15 && preview.texture_id.valid() && !preview.busy)
                {
                    if (!stale_rejected_ || !thumbnail_after_preview_)
                    {
                        stop("Stale preview rejection or subsequent thumbnail isolation was not verified.");
                        return;
                    }
                    prior_texture_ = preview.texture_id;
                    AssetId::parse("26e14823067241ee84676de813b2e8c3", preview_settings_.environment);
                    phase_ = 16;
                }
                else if (phase_ == 17 && preview.busy)
                {
                    // Close with a replacement still queued or in flight, then let shutdown drain it.
                    pool_.clear_material_preview();
                    MaterialInstance::release(preview_material_);
                    state_.complete = true;
                    window().close();
                }
            }
        }
        bool verify_preview_pixels()
        {
            if (material_extent_ != preview_settings_.extent ||
                material_pixels_.size() !=
                    static_cast<std::size_t>(material_extent_.width) * material_extent_.height * 4u)
            {
                stop("Material preview did not render at the requested extent.");
                return false;
            }
            if (phase_ == 8)
            {
                const auto width = material_extent_.width;
                const auto height = material_extent_.height;
                bool corner_changed = false;
                for (std::uint32_t y = 0; y < height; ++y)
                {
                    for (std::uint32_t x = 0; x < width; ++x)
                    {
                        const auto pixel = (static_cast<std::size_t>(y) * width + x) * 4u;
                        const bool center =
                            x > width * 2u / 5u && x < width * 3u / 5u && y > height * 2u / 5u && y < height * 3u / 5u;
                        for (std::size_t channel = 0; channel < 3u; ++channel)
                        {
                            if (center && material_pixels_[pixel + channel] != background_pixels_[pixel + channel])
                            {
                                stop("Background visibility changed foreground pixels: depth masking or reflection "
                                     "isolation failed.");
                                return false;
                            }
                            if (x < width / 8u && y < height / 8u)
                            {
                                corner_changed = corner_changed || material_pixels_[pixel + channel] !=
                                                                       background_pixels_[pixel + channel];
                            }
                        }
                    }
                }
                if (!corner_changed)
                {
                    stop("HDR background was not visible outside the sphere.");
                    return false;
                }
            }
            if (phase_ == 9 && material_pixels_ == background_pixels_)
            {
                stop("Environment rotation did not change the preview.");
                return false;
            }
            if (phase_ >= 10 && phase_ <= 12 && material_pixels_ == prior_preview_pixels_)
            {
                stop("Exposure, floor, or shadow setting did not change the rendered preview.");
                return false;
            }
            if (phase_ == 10)
            {
                std::uint64_t before = 0, after = 0;
                for (std::size_t i = 0; i < material_pixels_.size(); i += 4)
                {
                    before += prior_preview_pixels_[i] + prior_preview_pixels_[i + 1] + prior_preview_pixels_[i + 2];
                    after += material_pixels_[i] + material_pixels_[i + 1] + material_pixels_[i + 2];
                }
                if (after <= before)
                {
                    stop("Positive exposure did not increase image brightness.");
                    return false;
                }
            }
            return true;
        }
        AssetThumbnailView request(const AssetId& id)
        {
            for (const auto& asset : workspace_.catalog().entries)
            {
                if (asset.file.asset_id == id)
                {
                    return pool_.request(asset);
                }
            }
            stop("Test asset vanished from the catalog.");
            return {};
        }
        bool verify_saved(const std::string& name, const std::vector<std::uint8_t>& opaque)
        {
            const auto path = VirtualPath::parse(name);
            const auto pair = workspace_.asset_pairs().read(path.value());
            if (!pair.succeeded())
            {
                stop(pair.status().message);
                return false;
            }
            const auto& index = pair.value().description.index;
            bool found_opaque = false;
            for (const auto& segment : pair.value().meta.segments)
            {
                if (segment.name == "thumbnail" || segment.name == "thumbnail_source")
                {
                    stop("Thumbnail data was written into the asset.");
                    return false;
                }
                if (segment.name != "future_editor_data")
                {
                    continue;
                }
                found_opaque = segment.kind == 2u && !segment.required && segment.bytes == opaque;
            }
            const auto source = calculate_static_mesh_thumbnail_source(pair.value());
            if (!source.succeeded())
            {
                stop("Thumbnail fixture source: " + source.status().message);
                return false;
            }
            const auto cache = VirtualPath::parse("/Saved/AssetThumbnails/" + index.asset_id.hex() + "-" +
                                                  sha256_to_hex(source.value().content_hash) + "-v" +
                                                  std::to_string(thumbnail_generator_version) + ".png");
            const auto png = workspace_.files().read_binary(cache.value(), thumbnail_max_bytes);
            Rgba8Image image;
            if (!png.succeeded() || !decode_png(png.value(), image).succeeded())
            {
                stop("Saved thumbnail cache is not a valid PNG.");
                return false;
            }
            int brightest = 0;
            int darkest = 255;
            for (std::size_t i = 0; i < image.pixels.size(); i += 4)
            {
                brightest = std::max(brightest, static_cast<int>(image.pixels[i]));
                darkest = std::min(darkest, static_cast<int>(image.pixels[i]));
                if (image.pixels[i + 3] != 255)
                {
                    stop("Thumbnail alpha must be opaque.");
                    return false;
                }
            }
            if (!found_opaque || image.width != thumbnail_default_size || image.height != thumbnail_default_size ||
                brightest - darkest < 30)
            {
                stop("Thumbnail cache or opaque asset data is invalid.");
                return false;
            }
            return true;
        }
        void stop(std::string error)
        {
            state_.error = std::move(error);
            window().close();
        }
        bool verify_asset_placement()
        {
            AssetPlacementRequest request;
            request.asset_id = first_;
            request.on_ground = true;
            std::string error;
            const auto id = place_static_mesh_asset(workspace_, world(), factory_, history_, request, error);
            Actor* actor = world().find_actor_by_id(id);
            if (!actor || actor->root_component()->local_transform().translation.y != 1 || !history_.undo(world()) ||
                world().actor_count() != 0 || !history_.redo(world()) || world().actor_count() != 1)
            {
                stop("Asset placement, imported ground offset or undo/redo failed: " + error);
                return false;
            }
            request.asset_id = {};
            if (place_static_mesh_asset(workspace_, world(), factory_, history_, request, error) ||
                world().actor_count() != 1)
            {
                stop("A stale or invalid asset identity mutated the scene.");
                return false;
            }
            actor = world().find_actor_by_id(world().actor_ids().front());
            auto state = factory_.capture(*actor);
            auto candidate = state.components.front();
            const auto original_mesh = candidate.mesh;
            auto& mesh_data = std::get<SceneMeshData>(candidate.data.properties);
            mesh_data.builtin_mesh.clear();
            mesh_data.resources.clear();
            candidate.mesh.reset();
            if (!history_.replace_mesh(world(), actor->actor_id(), std::move(candidate), error) ||
                !flush_rendering_commands().succeeded() || !history_.undo(world()))
            {
                stop("Static Mesh clear/undo after render retirement failed: " + error);
                return false;
            }
            const auto* component = dynamic_cast<StaticMeshComponent*>(actor->root_component());
            if (!component || !component->static_mesh() || component->static_mesh() == original_mesh ||
                component->static_mesh()->material_slot_names() != original_mesh->material_slot_names() ||
                !history_.redo(world()) || component->static_mesh() || !history_.undo(world()))
            {
                stop("Static Mesh history must restore CPU data with fresh render resources.");
                return false;
            }
            if (!history_.undo(world()) || world().actor_count() != 0)
            {
                stop("Rejected asset placement changed the command history.");
                return false;
            }
            history_.clear();
            return true;
        }
#if WITH_WIN
        bool verify_native_drop()
        {
            auto* native = dynamic_cast<Win32Window*>(&window());
            if (!native || !window().enable_file_drop(true))
            {
                stop("Native file drop could not be enabled.");
                return false;
            }
            const wchar_t first[] = L"D:\\mesh.obj";
            const wchar_t second[] = L"D:\\模型.fbx";
            const std::size_t bytes = sizeof(DROPFILES) + sizeof(first) + sizeof(second) + sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
            void* data = memory ? GlobalLock(memory) : nullptr;
            if (!data)
            {
                if (memory)
                {
                    GlobalFree(memory);
                }
                stop("Native drop fixture allocation failed.");
                return false;
            }
            DROPFILES drop{};
            drop.pFiles = sizeof(DROPFILES);
            drop.pt = POINT{140, 90};
            drop.fWide = TRUE;
            std::memcpy(data, &drop, sizeof(drop));
            auto* names = static_cast<unsigned char*>(data) + sizeof(DROPFILES);
            std::memcpy(names, first, sizeof(first));
            std::memcpy(names + sizeof(first), second, sizeof(second));
            GlobalUnlock(memory);
            // WM_DROPFILES transfers ownership; Window's DragFinish releases it.
            SendMessageW(native->get_native_hwnd(), WM_DROPFILES, reinterpret_cast<WPARAM>(memory), 0);
            FileDropEvent event;
            if (!window().take_file_drop(event) || event.position != Vector2(140, 90) || event.paths.size() != 2 ||
                event.paths[0] != "D:\\mesh.obj" || event.paths[1] != u8"D:\\模型.fbx" ||
                window().take_file_drop(event))
            {
                stop("Native drop did not preserve owned UTF-8 paths and client coordinates.");
                return false;
            }
            if (!window().enable_file_drop(false))
            {
                stop("Native drop disable failed.");
                return false;
            }
            return true;
        }
#endif
        void on_build_ui() override
        {
            ImGui::SetNextWindowSize(ImVec2(620, 320), ImGuiCond_Always);
            browser_.draw(workspace_, selection_, folder_, show_engine_, pool_);
            ImGui::SetNextWindowSize(ImVec2(620, 320), ImGuiCond_Always);
            ImGui::Begin("Thumbnail integration");
            for (const auto& asset : workspace_.catalog().entries)
            {
                if (!(asset.file.asset_id == first_) && !(asset.file.asset_id == second_))
                {
                    continue;
                }
                const auto view = pool_.request(asset);
                if (view.texture_id.valid())
                {
                    ImGui::Image(reinterpret_cast<ImTextureID>(static_cast<std::uintptr_t>(view.texture_id.value())),
                                 ImVec2(thumbnail_default_size, thumbnail_default_size));
                    ImGui::SameLine();
                }
            }
            ImGui::End();
            if (!import_dialog_shown_)
            {
                import_dialog_shown_ = true;
                if (!import_dialog_.request("/Project", {"not-confirmed.obj"}))
                {
                    stop("Import confirmation dialog did not open.");
                    return;
                }
            }
            // Draw the actual settings modal at a small window size. Requesting
            // or drawing it must never import before an explicit confirmation.
            import_dialog_.draw(window(), workspace_, selection_, pool_);
            const auto unconfirmed = VirtualPath::parse("/Project/not-confirmed.asset");
            const auto state = workspace_.files().stat(unconfirmed.value());
            if (state.succeeded() || state.status().code != FileErrorCode::NotFound)
            {
                stop("Opening the import dialog wrote an unconfirmed asset.");
            }
        }
        void on_shutdown() override
        {
            import_dialog_.clear();
            pool_.shutdown();
            if (preview_material_)
            {
                MaterialInstance::release(preview_material_);
            }
            if (!flush_rendering_commands().succeeded())
            {
                state_.error = "Preview teardown did not drain.";
            }
            factory_.release();
        }

        EditorWorkspace& workspace_;
        AssetThumbnailPool pool_;
        ActorFactory factory_;
        EditorCommandHistory history_;
        ContentBrowserPanel browser_;
        EditorSelection selection_;
        StaticMeshImportDialog import_dialog_;
        bool import_dialog_shown_ = false;
        std::string folder_ = "/Project";
        bool show_engine_ = false;
        AssetId first_;
        AssetId second_;
        TestState& state_;
        MaterialInstanceRef preview_material_;
        MaterialPreviewSettings preview_settings_;
        Extent material_extent_;
        std::vector<std::uint8_t> material_pixels_;
        std::vector<std::uint8_t> background_pixels_;
        std::vector<std::uint8_t> prior_preview_pixels_;
        ImGuiTextureId stale_texture_;
        ImGuiTextureId prior_texture_;
        bool stale_rejected_ = false;
        bool thumbnail_after_preview_ = false;
        Sha256Hash second_thumbnail_hash_{};
        Sha256Hash material_image_hash_{};
        Sha256Hash original_material_image_hash_{};
        std::size_t level_actor_count_ = 0u;
        bool material_has_color_ = false;
        Sha256Hash conflict_snapshot_{};
        bool started_ = false;
        int phase_ = 0;
        double elapsed_ = 0;
    };
} // namespace

int main(int argc, char** argv)
{
    using namespace toy3d;
    std::string destination = "unchanged", error;
    if (!static_mesh_import_destination("C:/模型.FBX", "/Project/Models", u8"模型", 1, destination, error) ||
        destination != u8"/Project/Models/模型.asset" ||
        static_mesh_import_destination("a.obj", "/Engine", "mesh", 1, destination, error) ||
        static_mesh_import_destination("a.obj", "/Project2", "mesh", 1, destination, error) ||
        static_mesh_import_destination("a.png", "/Project", "mesh", 1, destination, error) ||
        static_mesh_import_destination("a.obj", "/Project", "../mesh", 1, destination, error) ||
        static_mesh_import_destination("a.obj", "/Project", "mesh", 0, destination, error))
    {
        std::cerr << "Import settings validation failed: " << error;
        return EXIT_FAILURE;
    }
    StaticMeshImportDialog dialog;
    if (dialog.request("/Engine", {"a.obj"}) || dialog.active() ||
        dialog.request("/Project", std::vector<std::string>(maximum_file_drop_paths + 1, "a.obj")) ||
        !dialog.request("/Project", {"a.obj"}) || !dialog.active() || dialog.request("/Project", {"b.obj"}))
    {
        std::cerr << "Import transaction boundary failed.";
        return EXIT_FAILURE;
    }
    dialog.clear();
    if (dialog.active())
    {
        return EXIT_FAILURE;
    }
    NativePlatformFile platform;
    AssetId first, second;
    if (!AssetId::try_generate(first) || !AssetId::try_generate(second))
    {
        return EXIT_FAILURE;
    }
    const auto root = platform.join_relative(PhysicalPath(TOY3D_THUMBNAIL_TEST_ROOT), first.hex());
    if (!root.succeeded() || !platform.create_directories(root.value()).succeeded())
    {
        return EXIT_FAILURE;
    }
    EditorWorkspace workspace;
    EditorWorkspacePaths paths;
    paths.project_assets = root.value();
    paths.engine_assets = PhysicalPath(TOY3D_EDITOR_ENGINE_ASSET_ROOT);
    paths.editor_resources = PhysicalPath(TOY3D_EDITOR_RESOURCE_ROOT);
    paths.deployment = PhysicalPath(TOY3D_EDITOR_DEPLOY_ROOT);
    if (!workspace.initialize(paths))
    {
        std::cerr << workspace.error();
        return EXIT_FAILURE;
    }
    StaticMeshAssetGeometry geometry;
    const std::vector<Vector3> positions{{-1, -1, -1}, {1, -1, -1}, {0, 1, -1}, {0, 0, 1}};
    for (const auto& position : positions)
    {
        Vector3 normal;
        if (!try_normalize(position, normal))
        {
            return EXIT_FAILURE;
        }
        geometry.vertices.push_back({position, normal, Vector2(0)});
    }
    geometry.indices = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    geometry.sections = {{0, 12, 0}};
    geometry.material_slots = {"Preview"};
    const auto a = encode_static_mesh_asset_pair(workspace.types(), first, geometry,
                                                 {{"future_editor_data", 2u, false, {1, 2, 3}}});
    const auto b = encode_static_mesh_asset_pair(workspace.types(), second, geometry,
                                                 {{"future_editor_data", 2u, false, {1, 2, 3}}});
    const auto path_a = VirtualPath::parse("/Project/first.asset");
    const auto path_b = VirtualPath::parse("/Project/second.asset");
    if (!a.succeeded() || !b.succeeded() ||
        !workspace.asset_pairs().publish(path_a.value(), a.value(), FilePublishMode::CreateNew).succeeded() ||
        !workspace.asset_pairs().publish(path_b.value(), b.value(), FilePublishMode::CreateNew).succeeded() ||
        !workspace.refresh())
    {
        return EXIT_FAILURE;
    }
#if WITH_MODEL_IMPORT
    const auto source = platform.join_relative(root.value(), "import-smoke.obj");
    if (!source.succeeded() ||
        !platform.write_text_utf8(source.value(), "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", FileWriteMode::CreateNew)
             .succeeded())
    {
        return EXIT_FAILURE;
    }
    AssetId imported;
    StaticMeshImportOptions options;
    const std::size_t before = workspace.catalog().entries.size();
    if (!import_static_mesh_to_workspace(workspace, source.value(), "/Project/import-smoke.asset", options, imported,
                                         error) ||
        !imported.valid() || workspace.catalog().entries.size() != before + 1)
    {
        std::cerr << "Import fixture failed: " << error << '\n';
        return EXIT_FAILURE;
    }
    AssetId rejected;
    if (import_static_mesh_to_workspace(workspace, source.value(), "/Project/import-smoke.asset", options, rejected,
                                        error) ||
        rejected.valid() ||
        import_static_mesh_to_workspace(workspace, source.value(), "/Engine/import-smoke.asset", options, rejected,
                                        error))
    {
        std::cerr << "Import overwrite or read-only boundary failed.";
        return EXIT_FAILURE;
    }
    const auto imported_path = VirtualPath::parse("/Project/import-smoke.asset");
    if (!workspace.asset_pairs().remove(imported_path.value()).succeeded() || !workspace.refresh())
    {
        return EXIT_FAILURE;
    }
#endif
    CommandLineParser::get_instance().parser_args(
        {"ThumbnailTests", "--Window.Width=720", "--Window.Height=480", "--Window.Title=Thumbnail Tests"});
    if (argc == 2 && std::string(argv[1]) == "--pie-integration")
    {
#if WITH_WIN
        return check_editor_play_integration(workspace, static_cast<void*>(GetModuleHandleW(nullptr)))
#else
        return check_editor_play_integration(workspace, nullptr)
#endif
                   ? EXIT_SUCCESS
                   : EXIT_FAILURE;
    }
    TestState state;
    {
        Engine engine;
        engine.set_application(std::make_unique<ThumbnailTestApplication>(workspace, first, second, state));
#if WITH_WIN
        engine.init(static_cast<void*>(GetModuleHandleW(nullptr)));
#else
        engine.init(nullptr);
#endif
        engine.main_loop();
        engine.exit();
    }
    std::cout << "Thumbnail artifacts: " << root.value().utf8() << '\n';
    if (!state.complete || !state.error.empty())
    {
        std::cerr << "Thumbnail integration failed: " << state.error << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Preview render, PNG persistence, opaque preservation, multi-image reload and save conflict passed.\n";
    return EXIT_SUCCESS;
}

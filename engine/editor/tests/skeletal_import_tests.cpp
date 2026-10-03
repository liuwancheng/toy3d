#include "assets/animation/skeletal_mesh_asset_tools.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "asset/asset_descriptor_path.h"
#include "misc/sha256.h"
#include "threading/task_graph/task_graph.h"
#include "threading/thread_manager.h"
#include "workspace/editor_workspace.h"

namespace
{
    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    toy3d::VirtualPath path(const std::string& text)
    {
        const auto result = toy3d::VirtualPath::parse(text);
        check(result.succeeded(), "test path: " + text);
        return result.value();
    }

    // Decode only the known test fixture's data URI to exercise external-buffer source invalidation.
    std::vector<std::uint8_t> fixture_buffer(const std::string& text)
    {
        const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::vector<std::uint8_t> bytes;
        std::uint32_t bits = 0;
        std::uint32_t count = 0;
        for (const char c : text)
        {
            if (c == '=')
            {
                break;
            }
            const auto digit = alphabet.find(c);
            check(digit != std::string::npos, "fixture base64 digit");
            bits = (bits << 6) | static_cast<std::uint32_t>(digit);
            count += 6;
            if (count >= 8)
            {
                count -= 8;
                bytes.push_back(static_cast<std::uint8_t>(bits >> count));
            }
        }
        return bytes;
    }

    void run_tests(const toy3d::PhysicalPath& root)
    {
        using namespace toy3d;
        NativePlatformFile platform;
        auto physical = [&root](const std::string& relative)
        {
            return PhysicalPath(root.utf8() + "/" + relative);
        };
        EditorWorkspacePaths roots;
        roots.project_assets = physical("project/asset");
        roots.engine_assets = physical("engine/asset");
        roots.editor_resources = physical("editor/resources");
        roots.deployment = physical("bin");
        roots.saved = physical("saved");
        for (const auto& directory : {roots.project_assets, roots.engine_assets, roots.editor_resources,
                                      roots.deployment, roots.saved, physical("source")})
        {
            check(platform.create_directories(directory).succeeded(), "test directory");
        }
        const auto original = platform.read_text_utf8(PhysicalPath(TOY3D_EDITOR_SKELETAL_FIXTURE));
        check(original.succeeded(), "read eight-bone animation fixture");
        std::string source_text = original.value();
        const auto uri = source_text.find("data:application/octet-stream;base64,");
        check(uri != std::string::npos, "fixture buffer URI");
        const auto data = uri + std::string("data:application/octet-stream;base64,").size();
        const auto end = source_text.find('"', data);
        const auto source_bytes = fixture_buffer(source_text.substr(data, end - data));
        source_text.replace(uri, end - uri, "skin.bin");
        const auto source = physical("source/character.gltf");
        check(
            platform.write_text_utf8(source, source_text, FileWriteMode::CreateNew).succeeded() &&
                platform.write_binary(physical("source/skin.bin"), source_bytes, FileWriteMode::CreateNew).succeeded(),
            "external source fixture");
        EditorWorkspace workspace;
        check(workspace.initialize(roots), "workspace initialize: " + workspace.error());
        SkeletalMeshImportOptions options;
        options.coordinates.use_file_unit = false;
        options.coordinates.source_unit_in_centimeters = 100.0f;
        std::string error;
        SkeletalImportRequest request;
        check(!capture_skeletal_import(workspace, source, "/Engine/denied.asset", SkeletalImportMode::MeshAndAnimations,
                                       {}, options, {}, request, error),
              "Engine import rejected");
        check(!capture_skeletal_import(workspace, source, "/Project/clip.asset", SkeletalImportMode::AnimationOnly, {},
                                       options, {}, request, error),
              "animation requires Skeleton");
        auto capture = [&](const std::string& destination, SkeletalImportMode mode, const AssetId& skeleton,
                           const AssetId& reimport = AssetId{})
        {
            SkeletalImportRequest input;
            check(capture_skeletal_import(workspace, source, destination, mode, skeleton, options, reimport, input,
                                          error),
                  "capture: " + error);
            return input;
        };
        auto prepare = [](const SkeletalImportRequest& input)
        {
            PreparedSkeletalImport result;
            check(prepare_skeletal_import(input, result), "prepare: " + result.error);
            return result;
        };
        auto new_request = capture("/Project/character.asset", SkeletalImportMode::MeshAndAnimations, {});
        auto prepared = prepare(new_request);
        check(prepared.outputs.size() == 3 && prepared.sources.size() == 2,
              "complete Skeleton/mesh/clip and both external source baselines");
        std::vector<AssetId> committed;
        check(publish_skeletal_import(workspace, new_request, prepared, committed, error) && committed.size() == 3,
              "publish complete character: " + error);
        const auto skeleton_id = committed[0];
        const auto mesh_id = committed[1];
        const auto clip_id = committed[2];
        check(workspace.catalog().entries.size() == 3, "catalog refreshed after commit");
        check(!platform.stat(physical("bin/character.asset")).succeeded(), "no deployed copy writes");
        const auto skeleton_pair = workspace.asset_pairs().read(path("/Project/character_Skeleton.asset"));
        const auto mesh_pair = workspace.asset_pairs().read(path("/Project/character.asset"));
        const auto clip_pair = workspace.asset_pairs().read(path("/Project/character_Animation_0.asset"));
        check(skeleton_pair.succeeded() && mesh_pair.succeeded() && clip_pair.succeeded(), "formal pair reload");
        const auto skeleton_before = skeleton_pair.value().description_bytes;
        const auto mesh_before = mesh_pair.value().description_bytes;
        const auto clip_before = clip_pair.value().description_bytes;

        auto animation_request = capture("/Project/idle.asset", SkeletalImportMode::AnimationOnly, skeleton_id);
        auto animation = prepare(animation_request);
        check(animation.outputs.size() == 1, "animation-only creates one asset");
        check(publish_skeletal_import(workspace, animation_request, animation, committed, error), "publish clip");
        check(workspace.asset_pairs().read(path("/Project/character_Skeleton.asset")).value().description_bytes ==
                  skeleton_before,
              "shared Skeleton untouched");

        auto mesh_request = capture("/Project/character.asset", SkeletalImportMode::MeshAndAnimations, {}, mesh_id);
        auto mesh_reimport = prepare(mesh_request);
        check(mesh_reimport.outputs.size() == 1 && mesh_reimport.outputs.front().id == mesh_id,
              "mesh reimport preserves identity and excludes clips/Skeleton");
        check(publish_skeletal_import(workspace, mesh_request, mesh_reimport, committed, error), "mesh reimport");
        check(workspace.asset_pairs().read(path("/Project/character_Animation_0.asset")).value().description_bytes ==
                  clip_before,
              "mesh reimport preserves other clips");
        auto clip_request =
            capture("/Project/character_Animation_0.asset", SkeletalImportMode::AnimationOnly, {}, clip_id);
        clip_request.options.sample_rate = 60;
        auto clip_reimport = prepare(clip_request);
        check(publish_skeletal_import(workspace, clip_request, clip_reimport, committed, error), "clip reimport");
        const auto changed_clip = workspace.asset_pairs().read(path("/Project/character_Animation_0.asset"));
        check(changed_clip.succeeded() &&
                  decode_animation_sequence_asset_pair(changed_clip.value()).value().data.sample_rate == 60 &&
                  changed_clip.value().description.index.asset_id == clip_id,
              "clip keeps ID and changes samples");

        auto stale_target = prepare(clip_request);
        check(
            !publish_skeletal_import(workspace, clip_request, stale_target, committed, error) && committed.empty() &&
                workspace.asset_pairs().read(path("/Project/character_Animation_0.asset")).value().description_bytes ==
                    changed_clip.value().description_bytes,
            "stale target rejected without overwrite");

        auto stale_request = capture("/Project/stale.asset", SkeletalImportMode::AnimationOnly, skeleton_id);
        auto stale_source = prepare(stale_request);
        auto changed_bytes = source_bytes;
        changed_bytes.back() ^= 1u;
        check(platform.write_binary(physical("source/skin.bin"), changed_bytes, FileWriteMode::Truncate).succeeded(),
              "mutate external buffer");
        check(!publish_skeletal_import(workspace, stale_request, stale_source, committed, error) && committed.empty() &&
                  !workspace.files().stat(path("/Project/stale.asset")).succeeded(),
              "stale external buffer rejected");
        check(platform.write_binary(physical("source/skin.bin"), source_bytes, FileWriteMode::Truncate).succeeded(),
              "restore external buffer");
        auto stale_skeleton = prepare(stale_request);
        auto changed_skeleton = decode_skeleton_asset_pair(skeleton_pair.value()).value();
        changed_skeleton.bones.back().name = "RenamedBone";
        const auto modified = encode_skeleton_asset_pair(workspace.types(), skeleton_id, changed_skeleton);
        check(modified.succeeded() &&
                  workspace.asset_pairs()
                      .publish(path("/Project/character_Skeleton.asset"), modified.value(), FilePublishMode::Replace)
                      .succeeded(),
              "change Skeleton during import");
        check(!publish_skeletal_import(workspace, stale_request, stale_skeleton, committed, error) && committed.empty(),
              "stale Skeleton rejected");
        const auto restored = encode_skeleton_asset_pair(workspace.types(), skeleton_id,
                                                         decode_skeleton_asset_pair(skeleton_pair.value()).value());
        check(workspace.asset_pairs()
                      .publish(path("/Project/character_Skeleton.asset"), restored.value(), FilePublishMode::Replace)
                      .succeeded() &&
                  workspace.refresh(),
              "restore Skeleton");

        auto collision_request = capture("/Project/collision.asset", SkeletalImportMode::MeshAndAnimations, {});
        auto collision = prepare(collision_request);
        check(workspace.files()
                  .write_binary(path("/Project/collision_Animation_0.asset"), {42}, FileWriteMode::CreateNew)
                  .succeeded(),
              "late output collision");
        check(!publish_skeletal_import(workspace, collision_request, collision, committed, error) &&
                  committed.empty() && !workspace.files().stat(path("/Project/collision_Skeleton.asset")).succeeded(),
              "preflight prevents orphan dependency");
        check(workspace.files().remove_file(path("/Project/collision_Animation_0.asset")).succeeded(),
              "remove collision fixture");

        auto partial_request = capture("/Project/partial.asset", SkeletalImportMode::MeshAndAnimations, {});
        auto partial = prepare(partial_request);
        check(workspace.files().create_directories(path("/Project/partial.asset.new")).succeeded(),
              "inject mesh staging failure");
        check(!publish_skeletal_import(workspace, partial_request, partial, committed, error) &&
                  committed.size() == 1 && workspace.catalog().index.find(committed.front()) &&
                  !workspace.files().stat(path("/Project/partial.asset")).succeeded(),
              "partial failure retains committed Skeleton and refreshes catalog");
        check(workspace.files().remove_empty_directory(path("/Project/partial.asset.new")).succeeded(),
              "remove staging fixture");

        auto incompatible = capture("/Project/character.asset", SkeletalImportMode::MeshAndAnimations, {}, mesh_id);
        incompatible.options.coordinates.source_unit_in_centimeters = 50.0f;
        // This fixture has identity reference transforms; change the required Skeleton reference to exercise rejection.
        incompatible.skeleton.bones.back().reference_local_transform.translation.x += 10.0f;
        PreparedSkeletalImport invalid;
        check(!prepare_skeletal_import(incompatible, invalid) &&
                  workspace.asset_pairs().read(path("/Project/character.asset")).value().description_bytes ==
                      mesh_before,
              "incompatible candidate preserves old mesh");

        auto refresh_request = capture("/Project/refresh.asset", SkeletalImportMode::AnimationOnly, skeleton_id);
        auto refresh = prepare(refresh_request);
        check(workspace.files().write_binary(path("/Project/broken.asset"), {42}, FileWriteMode::CreateNew).succeeded(),
              "inject catalog refresh failure");
        check(!publish_skeletal_import(workspace, refresh_request, refresh, committed, error) &&
                  committed.size() == 1 && workspace.asset_pairs().read(path("/Project/refresh.asset")).succeeded(),
              "commit remains visible when refresh fails");
        check(workspace.files().remove_file(path("/Project/broken.asset")).succeeded() && workspace.refresh(),
              "recover catalog");

        ThreadManager threads;
        auto created = create_task_graph({1, 64, true}, threads);
        check(created.succeeded(), "create worker scheduler");
        auto graph = created.take_task_graph();
        auto finish = [&](SkeletalImportJob& job)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (job.busy() && std::chrono::steady_clock::now() < deadline)
            {
                job.update(workspace);
                std::this_thread::yield();
            }
            check(!job.busy(), "worker completion timeout");
        };
        {
            SkeletalImportJob job;
            check(job.start(capture("/Project/async.asset", SkeletalImportMode::AnimationOnly, skeleton_id), error),
                  "start async import");
            finish(job);
            check(job.error().empty() && job.committed().size() == 1 &&
                      workspace.files().stat(path("/Project/async.asset")).succeeded(),
                  "worker builds and GT publishes");
            check(job.start(capture("/Project/cancelled.asset", SkeletalImportMode::AnimationOnly, skeleton_id), error),
                  "start cancelled job");
            job.cancel();
            finish(job);
            check(job.committed().empty() && !workspace.files().stat(path("/Project/cancelled.asset")).succeeded(),
                  "cancel drops result");
            check(job.start(capture("/Project/shutdown.asset", SkeletalImportMode::AnimationOnly, skeleton_id), error),
                  "start shutdown job");
            job.shutdown();
            check(!job.busy() && !workspace.files().stat(path("/Project/shutdown.asset")).succeeded(),
                  "shutdown joins without publishing");
        }
        check(graph->shutdown(TaskGraphShutdownMode::Drain).succeeded(), "scheduler shutdown");

        auto other_roots = roots;
        other_roots.project_assets = physical("other/asset");
        EditorWorkspace other_workspace;
        check(platform.create_directories(other_roots.project_assets).succeeded() &&
                  other_workspace.initialize(other_roots),
              "switch workspace");
        check(!publish_skeletal_import(other_workspace, stale_request, stale_source, committed, error) &&
                  committed.empty() && !other_workspace.files().stat(path("/Project/stale.asset")).succeeded(),
              "old workspace result rejected");
    }
} // namespace

int main()
{
    using namespace toy3d;
    // C++17 filesystem composes only the isolated test directory; asset I/O uses shared services.
    const auto fixture =
        std::filesystem::temp_directory_path() /
        ("toy3d_skeletal_editor_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const PhysicalPath root(fixture.u8string());
    int result = 0;
    try
    {
        run_tests(root);
        std::cout << "Skeletal editor import checks passed.\n";
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        result = 1;
    }
    NativePlatformFile platform;
    if (!platform.remove_directory_tree(root).succeeded())
    {
        std::cerr << "Could not remove test directory: " << root.utf8() << '\n';
        result = 1;
    }
    return result;
}

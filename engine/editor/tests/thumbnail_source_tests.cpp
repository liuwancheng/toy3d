#include "assets/thumbnails/thumbnail_source.h"

#include <cstdlib>
#include <chrono>
#include <iostream>
#include <utility>

#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"
#include "threading/task_graph/graph_task.h"
#include "threading/event.h"
#include "threading/task_graph/task_graph.h"
#include "threading/thread_manager.h"

int main(int argc, char** argv)
{
    using namespace toy3d;
    const bool multithreaded = argc == 1 || std::string(argv[1]) != "--singlethread";
    NativePlatformFile platform;
    AssetId id;
    if (!AssetId::try_generate(id))
    {
        return EXIT_FAILURE;
    }
    const PhysicalPath root(std::string(TOY3D_THUMBNAIL_SOURCE_TEST_ROOT) + "/" + id.hex());
    if (!platform.create_directories(root).succeeded())
    {
        return EXIT_FAILURE;
    }
    const auto store = DirectoryFileStore::create(platform, {root, true, DirectorySymlinkPolicy::Deny, "thumbnail"});
    if (!store.succeeded())
    {
        return EXIT_FAILURE;
    }
    FileSystem files;
    for (const std::string name : {"Project", "Saved"})
    {
        FileMountDesc mount;
        mount.virtual_root = VirtualPath::parse("/" + name).value();
        mount.store = store.value();
        mount.store_root = StorePath::parse(name).value();
        mount.access = MountAccess::ReadWrite;
        mount.allow_enumeration = true;
        if (!files.add_mount(mount).succeeded())
        {
            return EXIT_FAILURE;
        }
    }
    if (!files.freeze().succeeded() || !files.create_directories(VirtualPath::parse("/Project").value()).succeeded())
    {
        return EXIT_FAILURE;
    }
    TypeRegistry types;
    if (!register_static_mesh_asset_types(types).succeeded() || !types.freeze().succeeded())
    {
        return EXIT_FAILURE;
    }
    AssetPairStore pairs(types, files);
    StaticMeshAssetGeometry geometry;
    geometry.vertices = {
        {{-1, 0, 0}, {0, 0, 1}, {0, 0}}, {{1, 0, 0}, {0, 0, 1}, {1, 0}}, {{0, 1, 0}, {0, 0, 1}, {0.5f, 1}}};
    geometry.indices = {0, 1, 2};
    geometry.sections = {{0, 3, 0}};
    geometry.material_slots = {"Preview"};
    const auto path = VirtualPath::parse("/Project/mesh.asset").value();
    const auto encoded = encode_static_mesh_asset_pair(types, id, geometry, {{"opaque", 2u, false, {1, 2, 3}}});
    if (!encoded.succeeded() || !pairs.publish(path, encoded.value(), FilePublishMode::CreateNew).succeeded())
    {
        return EXIT_FAILURE;
    }
    const auto scanned = scan_asset_catalog(types, files, {VirtualPath::parse("/Project").value()}, false);
    if (!scanned.succeeded() || scanned.value().entries.size() != 1u)
    {
        return EXIT_FAILURE;
    }
    const auto catalog = scanned.value();
    const auto asset = catalog.entries.front();
    ThreadManager manager;
    TaskGraphConfig config;
    config.multithreaded = multithreaded;
    config.worker_thread_count = 1;
    auto created = create_task_graph(config, manager);
    if (!created.succeeded())
    {
        return EXIT_FAILURE;
    }
    auto tasks = created.take_task_graph();
    if (!tasks->attach_to_thread(NamedThread::GameThread).succeeded())
    {
        tasks->shutdown(TaskGraphShutdownMode::CancelPending);
        return EXIT_FAILURE;
    }
    bool passed = false;
    std::string error;
    Event started(EventMode::ManualReset);
    Event continue_job(EventMode::ManualReset);
    const auto job = dispatch_graph_task(
        *tasks, "Thumbnail CPU lifecycle test",
        [&](NamedThread thread, const GraphEventRef&)
        {
            started.trigger();
            continue_job.wait();
            if (thread != (multithreaded ? NamedThread::AnyWorker : NamedThread::GameThread))
            {
                error = "Thumbnail CPU task executed on the wrong thread.";
                return;
            }
            auto source = load_thumbnail_source(files, pairs, catalog, asset, false);
            if (!source.succeeded() || source.value().geometry.indices != geometry.indices)
            {
                error = "Mesh thumbnail source preparation failed.";
                return;
            }
            std::vector<std::uint8_t> pixels(static_cast<std::size_t>(thumbnail_default_size) * thumbnail_default_size *
                                             4u);
            for (std::size_t i = 0; i < pixels.size(); i += 4)
            {
                pixels[i] = 17;
                pixels[i + 1] = 83;
                pixels[i + 2] = 151;
                pixels[i + 3] = 255;
            }
            const auto saved = save_thumbnail_cache(files, pairs, catalog, id, path, source.value(), pixels);
            const auto cache_path = thumbnail_cache_path(id, source.value().source);
            const auto png = files.read_binary(cache_path);
            auto cached = load_thumbnail_source(files, pairs, catalog, asset, false);
            if (!saved.succeeded() || !png.succeeded() || !cached.succeeded() || cached.value().pixels != pixels ||
                !cached.value().geometry.vertices.empty())
            {
                error = "Saved PNG did not reload through the thumbnail cache.";
                return;
            }
            auto forced = load_thumbnail_source(files, pairs, catalog, asset, true);
            if (!forced.succeeded() || !forced.value().pixels.empty() || forced.value().geometry.vertices.empty())
            {
                error = "Forced generation did not bypass the PNG cache.";
                return;
            }
            if (!files.write_binary_atomic(cache_path, {1, 2, 3}, FilePublishMode::Replace).succeeded())
            {
                error = "Could not corrupt the isolated cache fixture.";
                return;
            }
            auto corrupt = load_thumbnail_source(files, pairs, catalog, asset, false);
            if (!corrupt.succeeded() || corrupt.value().warning.empty() || corrupt.value().geometry.vertices.empty() ||
                !save_thumbnail_cache(files, pairs, catalog, id, path, source.value(), pixels).succeeded())
            {
                error = "Corrupt cache did not regenerate from the original geometry.";
                return;
            }
            const auto changed = encode_static_mesh_asset_pair(types, id, geometry, {{"opaque", 2u, false, {9, 8, 7}}});
            if (!changed.succeeded() || !pairs.publish(path, changed.value(), FilePublishMode::Replace).succeeded())
            {
                error = "Could not change the isolated asset fixture.";
                return;
            }
            const auto rejected = save_thumbnail_cache(files, pairs, catalog, id, path, source.value(), pixels);
            const auto after = files.read_binary(cache_path);
            const auto current = pairs.read(path);
            if (rejected.succeeded() || rejected.message.find("conflict") == std::string::npos || !after.succeeded() ||
                after.value() != png.value() || !current.succeeded() ||
                current.value().description_bytes != changed.value().asset)
            {
                error = "A stale thumbnail replaced its cache or changed the author asset.";
                return;
            }
            auto moved = catalog;
            const auto new_path = VirtualPath::parse("/Project/moved.asset").value();
            if (!moved.index.move(id, new_path).succeeded() ||
                thumbnail_catalog_current(moved, id, path.utf8(), nullptr))
            {
                error = "GT catalog validation accepted a moved asset.";
                return;
            }
            passed = true;
        });
    // Observe the worker before draining: GT waits may help AnyWorker tasks.
    // ST instead executes the accepted job during Drain after this gate is released.
    const bool worker_started = !multithreaded || started.wait_for(std::chrono::seconds(10));
    continue_job.trigger();
    // All file services and inputs stay alive until the running job completes.
    const auto stopped = tasks->shutdown(TaskGraphShutdownMode::Drain);
    std::size_t live_workers = 0;
    manager.for_each_thread(
        [&](const ThreadInfo&)
        {
            ++live_workers;
        });
    if (!worker_started || !stopped.succeeded() || !job->is_complete() ||
        job->get_outcome() != TaskOutcome::Succeeded || !passed || live_workers != 0u)
    {
        std::cerr << "Thumbnail source test failed: " << error << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Thumbnail worker preparation, cache reload/corruption, conflicts and shutdown passed.\n";
    return EXIT_SUCCESS;
}

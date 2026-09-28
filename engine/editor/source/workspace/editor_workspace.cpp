#include "workspace/editor_workspace.h"

#include "file_system/directory_file_store.h"
#include "file_system/virtual_path.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace toy3d
{
    namespace
    {
        std::string comparable_path(std::string path)
        {
            std::replace(path.begin(), path.end(), '\\', '/');
            while (path.size() > 1 && path.back() == '/') path.pop_back();
#if defined(_WIN32)
            std::transform(path.begin(), path.end(), path.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
#endif
            return path;
        }

        bool paths_overlap(const PhysicalPath& first, const PhysicalPath& second)
        {
            const std::string left = comparable_path(first.utf8());
            const std::string right = comparable_path(second.utf8());
            return left == right || left.compare(0, right.size() + 1u, right + "/") == 0 ||
                   right.compare(0, left.size() + 1u, left + "/") == 0;
        }
    } // namespace

    bool EditorWorkspace::initialize(const EditorWorkspacePaths& paths)
    {
        if (ready_)
        {
            error_ = "Editor workspace is already initialized.";
            return false;
        }
        const auto source = platform_file_.canonical(paths.project_assets);
        const auto deployed = platform_file_.canonical(paths.deployment);
        const auto engine = platform_file_.canonical(paths.engine_assets);
        const auto resources = platform_file_.canonical(paths.editor_resources);
        if (!source.succeeded() || !deployed.succeeded() || !engine.succeeded() || !resources.succeeded())
        {
            error_ = "Editor project, engine, interface resource or deployment root could not be resolved.";
            return false;
        }
        if (paths_overlap(source.value(), deployed.value()) || paths_overlap(source.value(), engine.value()) ||
            paths_overlap(source.value(), resources.value()))
        {
            error_ = "Project asset source must not overlap deployment, engine assets or Editor interface resources.";
            return false;
        }

        auto mount_directory = [&](const PhysicalPath& physical_root, const char* virtual_root, bool writable)
        {
            DirectoryFileStoreDesc descriptor;
            descriptor.physical_root = physical_root;
            descriptor.writable = writable;
            descriptor.debug_name = virtual_root;
            const auto store = DirectoryFileStore::create(platform_file_, descriptor);
            if (!store.succeeded()) return store.status();
            const auto root = VirtualPath::parse(virtual_root);
            if (!root.succeeded()) return root.status();
            FileMountDesc mount;
            mount.virtual_root = root.value();
            mount.store = store.value();
            mount.access = writable ? MountAccess::ReadWrite : MountAccess::ReadOnly;
            mount.allow_enumeration = true;
            mount.debug_name = virtual_root;
            return files_.add_mount(mount);
        };
        FileStatus mounted = mount_directory(source.value(), "/Project", true);
        if (mounted.succeeded()) mounted = mount_directory(engine.value(), "/Engine", false);
        if (mounted.succeeded()) mounted = mount_directory(resources.value(), "/Editor/Resources", false);
        if (!mounted.succeeded()) { error_ = mounted.message; return false; }
        const FileStatus frozen = files_.freeze();
        if (!frozen.succeeded())
        {
            error_ = frozen.message;
            return false;
        }
        source_root_ = source.value();
        ready_ = true;
        return refresh();
    }

    bool EditorWorkspace::refresh()
    {
        if (!ready_)
        {
            error_ = "Editor asset workspace is not initialized.";
            return false;
        }
        const auto project_root = VirtualPath::parse("/Project");
        const auto engine_root = VirtualPath::parse("/Engine");
        if (!project_root.succeeded() || !engine_root.succeeded())
        {
            error_ = "Editor asset catalog roots could not be parsed.";
            return false;
        }
        auto scanned = scan_asset_catalog(files_, std::vector<VirtualPath>{project_root.value(), engine_root.value()});
        if (!scanned.succeeded())
        {
            error_ = scanned.status().virtual_path + ": " + scanned.status().message;
            return false;
        }
        catalog_ = scanned.value();
        error_.clear();
        return true;
    }
} // namespace toy3d

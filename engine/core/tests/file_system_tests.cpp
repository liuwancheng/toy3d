#include "file_system/directory_file_store.h"
#include "file_system/file_system.h"
#include "file_system/native_platform_file.h"
#include "file_system/store_path.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    // filesystem creates isolated host fixtures for integration tests; product
    // code continues to use the FileSystem contract instead of these paths.
    namespace fs = std::filesystem;

    int failure_count = 0;

    void check(bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failure_count;
        }
    }

    toy3d::PhysicalPath physical(const fs::path& path)
    {
        return toy3d::PhysicalPath(path.u8string());
    }

    toy3d::VirtualPath virtual_path(const std::string& path)
    {
        toy3d::FileResult<toy3d::VirtualPath> parsed = toy3d::VirtualPath::parse(path);
        check(parsed.succeeded(), "test virtual path must parse: " + path);
        return parsed.succeeded() ? parsed.value() : toy3d::VirtualPath{};
    }

    struct TestDirectory
    {
        fs::path path;

        explicit TestDirectory(fs::path value) : path(std::move(value)) { fs::create_directory(path); }

        TestDirectory(const TestDirectory&) = delete;
        TestDirectory& operator=(const TestDirectory&) = delete;

        TestDirectory(TestDirectory&& other) noexcept : path(std::move(other.path)) { other.path.clear(); }

        ~TestDirectory()
        {
            std::error_code error;
            if (!path.empty())
            {
                fs::remove_all(path, error);
            }
        }
    };

    TestDirectory make_test_directory()
    {
        const auto timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        return TestDirectory(fs::temp_directory_path() / ("toy3d_file_system_" + std::to_string(timestamp)));
    }

    void test_read_write_and_errors()
    {
        TestDirectory directory = make_test_directory();
        toy3d::NativePlatformFile files;
        const toy3d::PhysicalPath path = physical(directory.path / fs::u8path(u8"数据.bin"));
        const std::vector<std::uint8_t> initial{0, 1, 2, 127, 255};

        check(files.write_binary(path, initial, toy3d::FileWriteMode::CreateNew).succeeded(),
              "CreateNew must write a new UTF-8 path");
        check(files.write_binary(path, initial, toy3d::FileWriteMode::CreateNew).code ==
                  toy3d::FileErrorCode::AlreadyExists,
              "CreateNew must reject an existing path");

        const toy3d::FileResult<std::vector<std::uint8_t>> read = files.read_binary(path);
        check(read.succeeded() && read.value() == initial, "binary read must preserve every byte");

        const std::vector<std::uint8_t> replacement{'h', 'e', 'l', 'l', 'o'};
        check(files.write_binary(path, replacement, toy3d::FileWriteMode::Truncate).succeeded(),
              "Truncate must replace existing content");
        const toy3d::FileResult<std::string> text = files.read_text_utf8(path);
        check(text.succeeded() && text.value() == "hello", "valid UTF-8 text must be returned");
        check(files.write_text_utf8(path, u8"着色器", toy3d::FileWriteMode::Truncate).succeeded() &&
                  files.read_text_utf8(path).value() == u8"着色器",
              "UTF-8 text writes must validate and preserve text");
        check(files.write_text_utf8(path, std::string("bad\xc0\xaf", 5), toy3d::FileWriteMode::Truncate).code ==
                  toy3d::FileErrorCode::InvalidData,
              "text writes must reject invalid UTF-8 before touching the file");

        const std::vector<std::uint8_t> invalid_utf8{0xc0, 0xaf};
        check(files.write_binary(path, invalid_utf8, toy3d::FileWriteMode::Truncate).succeeded(),
              "invalid UTF-8 bytes are still valid binary data");
        check(files.read_text_utf8(path).status().code == toy3d::FileErrorCode::InvalidData,
              "text reads must reject invalid UTF-8");

        const toy3d::PhysicalPath missing = physical(directory.path / "missing.bin");
        const toy3d::FileResult<bool> missing_exists = files.exists(missing);
        check(missing_exists.succeeded() && !missing_exists.value(),
              "exists must report a missing path without losing status information");
        check(files.read_binary(missing).status().code == toy3d::FileErrorCode::NotFound,
              "missing reads must return NotFound");

        const toy3d::PhysicalPath invalid(std::string("bad\0path", 8));
        check(files.exists(invalid).status().code == toy3d::FileErrorCode::InvalidPath,
              "embedded nulls must be rejected at the platform boundary");
    }

    void test_concurrent_reads()
    {
        TestDirectory directory = make_test_directory();
        toy3d::NativePlatformFile files;
        const toy3d::PhysicalPath path = physical(directory.path / "shared.bin");
        const std::vector<std::uint8_t> expected(4096, 0x5a);
        check(files.write_binary(path, expected, toy3d::FileWriteMode::CreateNew).succeeded(),
              "concurrent read setup must succeed");

        std::atomic<bool> reads_succeeded{true};
        std::vector<std::thread> threads;
        constexpr int reader_count = 8;
        for (int index = 0; index < reader_count; ++index)
        {
            threads.emplace_back(
                [&files, &path, &expected, &reads_succeeded]()
                {
                    for (int iteration = 0; iteration < 32; ++iteration)
                    {
                        const toy3d::FileResult<std::vector<std::uint8_t>> result = files.read_binary(path);
                        if (!result.succeeded() || result.value() != expected)
                        {
                            reads_succeeded.store(false);
                            return;
                        }
                    }
                });
        }
        for (std::thread& thread : threads)
        {
            thread.join();
        }
        check(reads_succeeded.load(), "concurrent reads through one platform file must be safe");
    }

    void test_file_handles()
    {
        TestDirectory directory = make_test_directory();
        toy3d::NativePlatformFile files;
        const toy3d::PhysicalPath path = physical(directory.path / "handle.bin");
        const std::vector<std::uint8_t> expected{10, 20, 30, 40, 50, 60};
        check(files.write_binary(path, expected, toy3d::FileWriteMode::CreateNew).succeeded(),
              "handle fixture must be written");

        auto opened = files.open(path, toy3d::FileOpenMode::Read);
        check(opened.succeeded(), "read handle must open an existing file");
        if (!opened.succeeded())
            return;
        std::unique_ptr<toy3d::FileHandle> handle = std::move(opened.value());
        check(handle->size().succeeded() && handle->size().value() == expected.size(),
              "handle size must report the file byte count");

        std::uint8_t sequential[2]{};
        auto first_read = handle->read(sequential, sizeof(sequential));
        check(first_read.succeeded() && first_read.value() == sizeof(sequential) && sequential[0] == 10 &&
                  sequential[1] == 20,
              "sequential read must advance the cursor");
        check(handle->tell().succeeded() && handle->tell().value() == 2, "tell must report the sequential cursor");

        std::uint8_t ranged[3]{};
        auto range_read = handle->read_at(3, ranged, sizeof(ranged));
        check(range_read.succeeded() && range_read.value() == sizeof(ranged) && ranged[0] == 40 && ranged[2] == 60,
              "read_at must read an explicit range");
        check(handle->tell().succeeded() && handle->tell().value() == 2,
              "read_at must not change the sequential cursor");

        check(handle->seek(expected.size()).succeeded(), "seek to EOF must succeed");
        std::uint8_t at_eof = 0;
        auto eof_read = handle->read(&at_eof, 1);
        check(eof_read.succeeded() && eof_read.value() == 0, "EOF must be a successful zero-byte read");
        check(handle->write(expected.data(), expected.size()).status().code == toy3d::FileErrorCode::AccessDenied,
              "a read-only handle must reject writes");
        check(handle->close().succeeded() && handle->close().succeeded(), "close must be idempotent");
        check(handle->tell().status().code == toy3d::FileErrorCode::InvalidState,
              "operations after close must return InvalidState");

        auto writable = files.open(physical(directory.path / "written.bin"), toy3d::FileOpenMode::WriteNew);
        check(writable.succeeded(), "write-new handle must create a file");
        if (writable.succeeded())
        {
            std::unique_ptr<toy3d::FileHandle> write_handle = std::move(writable.value());
            std::size_t offset = 0;
            while (offset < expected.size())
            {
                auto written = write_handle->write(expected.data() + offset, expected.size() - offset);
                check(written.succeeded() && written.value() != 0, "handle writes must make progress");
                if (!written.succeeded() || written.value() == 0)
                    break;
                offset += written.value();
            }
            check(write_handle->flush().succeeded(), "writable handles must flush explicitly");
            check(write_handle->close().succeeded(), "writable handles must close explicitly");
        }
    }

    void test_concurrent_range_reads()
    {
        TestDirectory directory = make_test_directory();
        toy3d::NativePlatformFile files;
        const toy3d::PhysicalPath path = physical(directory.path / "range.bin");
        std::vector<std::uint8_t> expected(4096);
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            expected[index] = static_cast<std::uint8_t>(index % 251);
        }
        check(files.write_binary(path, expected, toy3d::FileWriteMode::CreateNew).succeeded(),
              "range-read fixture must be written");
        auto opened = files.open(path, toy3d::FileOpenMode::Read);
        check(opened.succeeded(), "shared range-read handle must open");
        if (!opened.succeeded())
            return;
        std::unique_ptr<toy3d::FileHandle> handle = std::move(opened.value());

        std::atomic<bool> succeeded{true};
        std::vector<std::thread> threads;
        for (std::size_t reader = 0; reader < 8; ++reader)
        {
            threads.emplace_back(
                [reader, &handle, &expected, &succeeded]()
                {
                    const std::size_t offset = reader * 257;
                    std::vector<std::uint8_t> bytes(257);
                    for (int iteration = 0; iteration < 32; ++iteration)
                    {
                        auto result = handle->read_at(offset, bytes.data(), bytes.size());
                        if (!result.succeeded() || result.value() != bytes.size() ||
                            !std::equal(bytes.begin(), bytes.end(), expected.begin() + offset))
                        {
                            succeeded.store(false);
                            return;
                        }
                    }
                });
        }
        for (std::thread& thread : threads)
            thread.join();
        check(succeeded.load(), "read_at calls on one read handle must be concurrency-safe");
    }

    void test_directories_and_paths()
    {
        TestDirectory directory = make_test_directory();
        toy3d::NativePlatformFile files;
        const toy3d::PhysicalPath nested = physical(directory.path / "a" / "b");
        check(files.create_directories(nested).succeeded(), "recursive directory creation must succeed");
        check(files.create_directories(nested).succeeded(), "recursive creation must be idempotent");
        check(files.stat(nested).succeeded() && files.stat(nested).value().type == toy3d::FileType::Directory,
              "stat must identify directories");

        const toy3d::PhysicalPath file = physical(directory.path / "a" / "entry.bin");
        check(files.write_binary(file, {}, toy3d::FileWriteMode::CreateNew).succeeded(),
              "empty files must be supported");
        const toy3d::FileResult<toy3d::FileStat> file_stat = files.stat(file);
        check(file_stat.succeeded() && file_stat.value().type == toy3d::FileType::File && file_stat.value().size == 0,
              "stat must identify an empty file and its size");

        const toy3d::FileResult<std::vector<toy3d::DirectoryEntry>> entries =
            files.enumerate_directory(physical(directory.path / "a"));
        check(entries.succeeded() && entries.value().size() == 2,
              "directory enumeration must return immediate children");

        const toy3d::PhysicalPath lexical_input = physical(directory.path / "a" / "b" / ".." / "entry.bin");
        const toy3d::FileResult<toy3d::PhysicalPath> normalized = files.lexically_normal(lexical_input);
        check(normalized.succeeded() && normalized.value() == file, "lexical normalization must remove dot segments");
        check(files.absolute(file).succeeded(), "absolute path conversion must succeed");
        check(files.canonical(file).succeeded(), "canonical path conversion must succeed for existing files");
        check(files.parent_path(file).succeeded() && files.parent_path(file).value() == physical(directory.path / "a"),
              "parent path queries must preserve typed physical paths");
        check(files.canonical(physical(directory.path / "unknown")).status().code == toy3d::FileErrorCode::NotFound,
              "canonical must reject missing paths");
    }

    void test_no_replace_rename_and_removal()
    {
        TestDirectory directory = make_test_directory();
        toy3d::NativePlatformFile files;
        const toy3d::PhysicalPath source = physical(directory.path / "source.bin");
        const toy3d::PhysicalPath destination = physical(directory.path / "destination.bin");
        const toy3d::PhysicalPath occupied = physical(directory.path / "occupied.bin");
        const std::vector<std::uint8_t> source_bytes{1};
        const std::vector<std::uint8_t> occupied_bytes{2};
        check(files.write_binary(source, source_bytes, toy3d::FileWriteMode::CreateNew).succeeded(),
              "rename source setup must succeed");
        check(files.rename_no_replace(source, destination).succeeded(), "same-parent no-replace rename must succeed");
        check(files.write_binary(source, source_bytes, toy3d::FileWriteMode::CreateNew).succeeded(),
              "second rename source setup must succeed");
        check(files.write_binary(occupied, occupied_bytes, toy3d::FileWriteMode::CreateNew).succeeded(),
              "occupied destination setup must succeed");
        check(files.rename_no_replace(source, occupied).code == toy3d::FileErrorCode::AlreadyExists,
              "rename must never replace an existing destination");
        check(files.read_binary(occupied).succeeded() && files.read_binary(occupied).value() == occupied_bytes,
              "failed rename must preserve destination content");

        const toy3d::PhysicalPath replacement = physical(directory.path / "replacement.bin");
        const std::vector<std::uint8_t> replacement_bytes{3, 4};
        check(files.write_binary(replacement, replacement_bytes, toy3d::FileWriteMode::CreateNew).succeeded(),
              "replace source setup must succeed");
        check(files.replace(replacement, occupied).succeeded(),
              "replace must atomically replace an existing destination");
        check(files.read_binary(occupied).succeeded() && files.read_binary(occupied).value() == replacement_bytes,
              "replace must publish source content at the destination");

        const toy3d::PhysicalPath other_parent = physical(directory.path / "other");
        check(files.create_directory(other_parent).succeeded(), "other parent setup must succeed");
        check(files.rename_no_replace(source, physical(directory.path / "other" / "moved.bin")).code ==
                  toy3d::FileErrorCode::InvalidPath,
              "rename must reject a different parent directory");

        check(files.remove_file(destination).succeeded(), "remove_file must remove a file");
        check(files.remove_file(other_parent).code == toy3d::FileErrorCode::IsDirectory,
              "remove_file must reject a directory");
        check(files.remove_empty_directory(other_parent).succeeded(),
              "remove_empty_directory must remove an empty directory");
        check(files.remove_directory_tree(physical(directory.path)).succeeded(),
              "explicit cleanup may remove a constrained directory tree");
    }

    void test_virtual_path_validation()
    {
        check(toy3d::VirtualPath::parse("/Engine/ShaderIncludes/").succeeded() &&
                  toy3d::VirtualPath::parse("/Engine/ShaderIncludes/").value().utf8() == "/Engine/ShaderIncludes",
              "one trailing separator must normalize away");
        check(toy3d::VirtualPath::parse("Engine/file").status().code == toy3d::FileErrorCode::InvalidPath,
              "relative virtual paths must be rejected");
        check(toy3d::VirtualPath::parse("/Engine/../Saved/file").status().code == toy3d::FileErrorCode::InvalidPath,
              "virtual path escape must be rejected instead of normalized");
        check(toy3d::VirtualPath::parse("/Engine//file").status().code == toy3d::FileErrorCode::InvalidPath,
              "empty virtual path segments must be rejected");
        check(toy3d::VirtualPath::parse("/Engine//").status().code == toy3d::FileErrorCode::InvalidPath,
              "multiple trailing separators must not normalize into a different identity");
        check(toy3d::VirtualPath::parse("/Engine\\file").status().code == toy3d::FileErrorCode::InvalidPath,
              "native separators must not enter virtual paths");
    }

    toy3d::StorePath store_path(const std::string& path)
    {
        toy3d::FileResult<toy3d::StorePath> parsed = toy3d::StorePath::parse(path);
        check(parsed.succeeded(), "test store path must parse: " + path);
        return parsed.succeeded() ? parsed.value() : toy3d::StorePath{};
    }

    std::shared_ptr<toy3d::DirectoryFileStore> directory_store(
        toy3d::PlatformFile& files, const fs::path& root, bool writable = true,
        toy3d::DirectorySymlinkPolicy symlink_policy = toy3d::DirectorySymlinkPolicy::Deny)
    {
        toy3d::DirectoryFileStoreDesc descriptor;
        descriptor.physical_root = physical(root);
        descriptor.writable = writable;
        descriptor.symlink_policy = symlink_policy;
        descriptor.debug_name = root.u8string();
        toy3d::FileResult<std::shared_ptr<toy3d::DirectoryFileStore>> created =
            toy3d::DirectoryFileStore::create(files, descriptor);
        check(created.succeeded(), "directory store must be created: " + root.u8string());
        return created.succeeded() ? created.value() : nullptr;
    }

    void test_directory_store_symlink_boundaries()
    {
        TestDirectory directory = make_test_directory();
        const fs::path mount_root = directory.path / "mount";
        const fs::path inside_target = mount_root / "inside";
        const fs::path outside_target = directory.path / "outside";
        fs::create_directories(inside_target);
        fs::create_directories(outside_target);

        std::error_code link_error;
        fs::create_directory_symlink(inside_target, mount_root / "inside_link", link_error);
        if (link_error)
        {
            std::cout << "SKIPPED: symlink boundary test unavailable: " << link_error.message() << '\n';
            return;
        }
        fs::create_directory_symlink(outside_target, mount_root / "outside_link", link_error);
        if (link_error)
        {
            std::cout << "SKIPPED: outside symlink test unavailable: " << link_error.message() << '\n';
            return;
        }

        toy3d::NativePlatformFile files;
        const auto deny = directory_store(files, mount_root, false, toy3d::DirectorySymlinkPolicy::Deny);
        const auto allow = directory_store(files, mount_root, false, toy3d::DirectorySymlinkPolicy::AllowWithinRoot);
        if (deny == nullptr || allow == nullptr)
            return;

        check(deny->stat(store_path("inside_link")).status().code == toy3d::FileErrorCode::OutsideMount,
              "deny policy must reject symlink traversal");
        check(allow->stat(store_path("inside_link")).succeeded(),
              "allow-within policy must accept a target inside the store root");
        check(allow->stat(store_path("outside_link")).status().code == toy3d::FileErrorCode::OutsideMount,
              "allow-within policy must reject a target outside the store root");
    }

    toy3d::FileMountDesc file_mount(const std::string& root, const std::shared_ptr<toy3d::FileStore>& store,
                                    int priority, toy3d::MountAccess access = toy3d::MountAccess::ReadOnly,
                                    bool allow_enumeration = true)
    {
        toy3d::FileMountDesc descriptor;
        descriptor.virtual_root = virtual_path(root);
        descriptor.store = store;
        descriptor.priority = priority;
        descriptor.access = access;
        descriptor.allow_enumeration = allow_enumeration;
        descriptor.debug_name = root + "@" + std::to_string(priority);
        return descriptor;
    }

    class FailingFileStore final : public toy3d::FileStore
    {
      public:
        explicit FailingFileStore(toy3d::FileErrorCode code) : code_(code) {}

        toy3d::FileStoreCapabilities capabilities() const override
        {
            toy3d::FileStoreCapabilities result;
            result.enumerable = true;
            return result;
        }

        toy3d::FileResult<toy3d::FileStat> stat(const toy3d::StorePath&) const override
        {
            return toy3d::FileResult<toy3d::FileStat>(failure("stat"));
        }

        toy3d::FileResult<std::unique_ptr<toy3d::FileHandle>> open(const toy3d::StorePath&,
                                                                   toy3d::FileOpenMode) override
        {
            return toy3d::FileResult<std::unique_ptr<toy3d::FileHandle>>(failure("open"));
        }

        toy3d::FileResult<std::vector<toy3d::StoreDirectoryEntry>> enumerate(const toy3d::StorePath&) const override
        {
            return toy3d::FileResult<std::vector<toy3d::StoreDirectoryEntry>>(failure("enumerate"));
        }

        toy3d::FileStatus create_directories(const toy3d::StorePath&) override { return failure("create_directories"); }

        toy3d::FileStatus remove_file(const toy3d::StorePath&) override { return failure("remove_file"); }

        toy3d::FileStatus remove_empty_directory(const toy3d::StorePath&) override
        {
            return failure("remove_empty_directory");
        }

        toy3d::FileStatus rename_no_replace(const toy3d::StorePath&, const toy3d::StorePath&) override
        {
            return failure("rename_no_replace");
        }

        toy3d::FileStatus replace(const toy3d::StorePath&, const toy3d::StorePath&) override
        {
            return failure("replace");
        }

      private:
        toy3d::FileStatus failure(const char* operation) const
        {
            toy3d::FileStatus status;
            status.code = code_;
            status.operation = operation;
            status.message = "injected store failure";
            return status;
        }

        toy3d::FileErrorCode code_;
    };

    void test_store_path_and_directory_store_contract()
    {
        check(toy3d::StorePath::parse("").succeeded(), "empty store path must represent the store root");
        check(toy3d::StorePath::parse("folder/file.bin/").succeeded() &&
                  toy3d::StorePath::parse("folder/file.bin/").value().utf8() == "folder/file.bin",
              "one trailing store separator must normalize away");
        check(toy3d::StorePath::parse("/absolute").status().code == toy3d::FileErrorCode::InvalidPath,
              "store paths must reject absolute paths");
        check(toy3d::StorePath::parse("folder/../escape").status().code == toy3d::FileErrorCode::InvalidPath,
              "store paths must reject traversal");
        check(toy3d::StorePath::parse("folder\\native").status().code == toy3d::FileErrorCode::InvalidPath,
              "store paths must reject native separators");
        check(toy3d::StorePath::join(store_path("prefix"), store_path("child/file")).succeeded() &&
                  toy3d::StorePath::join(store_path("prefix"), store_path("child/file")).value().utf8() ==
                      "prefix/child/file",
              "store path join must preserve the relative namespace");

        TestDirectory directory = make_test_directory();
        const fs::path root = directory.path / "store";
        fs::create_directories(root / "folder");
        toy3d::NativePlatformFile files;
        check(files.write_binary(physical(root / "folder" / "entry.bin"), {1, 2, 3}, toy3d::FileWriteMode::CreateNew)
                  .succeeded(),
              "directory store fixture must be written");

        const std::shared_ptr<toy3d::DirectoryFileStore> writable = directory_store(files, root, true);
        if (writable == nullptr)
            return;
        check(writable->stat(store_path("folder/entry.bin")).succeeded(),
              "directory store stat must resolve store-relative paths");
        const toy3d::FileResult<std::vector<toy3d::StoreDirectoryEntry>> entries =
            writable->enumerate(store_path("folder"));
        check(entries.succeeded() && entries.value().size() == 1 && entries.value()[0].name == "entry.bin",
              "directory store enumeration must expose names instead of physical paths");
        check(writable->remove_empty_directory(store_path("")).code == toy3d::FileErrorCode::AccessDenied,
              "directory store root must not be removable");

        const std::shared_ptr<toy3d::DirectoryFileStore> read_only = directory_store(files, root, false);
        if (read_only == nullptr)
            return;
        check(read_only->open(store_path("new.bin"), toy3d::FileOpenMode::WriteNew).status().code ==
                  toy3d::FileErrorCode::ReadOnly,
              "read-only stores must reject writable handles");
        check(read_only->create_directories(store_path("new_directory")).code == toy3d::FileErrorCode::ReadOnly,
              "read-only stores must reject directory creation");
    }

    void test_file_system_overlay_and_snapshot()
    {
        TestDirectory directory = make_test_directory();
        const fs::path upper_root = directory.path / "upper";
        const fs::path writable_root = directory.path / "writable";
        const fs::path lower_root = directory.path / "lower";
        const fs::path nested_root = directory.path / "nested";
        fs::create_directories(upper_root);
        fs::create_directories(writable_root);
        fs::create_directories(lower_root);
        fs::create_directories(nested_root);
        toy3d::NativePlatformFile files;
        check(
            files.write_binary(physical(upper_root / "common.bin"), {'u'}, toy3d::FileWriteMode::CreateNew).succeeded(),
            "upper fixture must be written");
        check(
            files.write_binary(physical(upper_root / "upper.bin"), {'h'}, toy3d::FileWriteMode::CreateNew).succeeded(),
            "upper-only fixture must be written");
        check(
            files.write_binary(physical(lower_root / "common.bin"), {'l'}, toy3d::FileWriteMode::CreateNew).succeeded(),
            "lower fixture must be written");
        check(
            files.write_binary(physical(lower_root / "lower.bin"), {'o'}, toy3d::FileWriteMode::CreateNew).succeeded(),
            "lower-only fixture must be written");
        check(files.write_binary(physical(lower_root / "revealed.bin"), {'r'}, toy3d::FileWriteMode::CreateNew)
                  .succeeded(),
              "lower reveal fixture must be written");
        check(files.write_binary(physical(writable_root / "revealed.bin"), {'w'}, toy3d::FileWriteMode::CreateNew)
                  .succeeded(),
              "writable reveal fixture must be written");
        check(files.write_binary(physical(nested_root / "common.bin"), {'n'}, toy3d::FileWriteMode::CreateNew)
                  .succeeded(),
              "nested fixture must be written");

        const auto upper = directory_store(files, upper_root, false);
        const auto writable = directory_store(files, writable_root, true);
        const auto lower = directory_store(files, lower_root, false);
        const auto nested = directory_store(files, nested_root, false);
        if (upper == nullptr || writable == nullptr || lower == nullptr || nested == nullptr)
            return;

        toy3d::FileSystem file_system;
        check(file_system.add_mount(file_mount("/Content", upper, 20)).succeeded(),
              "upper overlay layer must register");
        check(file_system.add_mount(file_mount("/Content", writable, 10, toy3d::MountAccess::ReadWrite)).succeeded(),
              "writable overlay layer must register");
        check(file_system.add_mount(file_mount("/Content", lower, 0)).succeeded(), "lower overlay layer must register");
        check(file_system.add_mount(file_mount("/Content/Nested", nested, 0)).succeeded(),
              "longest-segment nested mount must register");
        check(file_system.add_mount(file_mount("/content", lower, 0)).code == toy3d::FileErrorCode::AlreadyExists,
              "mount roots differing only by ASCII case must collide");
        check(file_system.add_mount(file_mount("/Content", lower, 20)).code == toy3d::FileErrorCode::AlreadyExists,
              "equal overlay priorities must be rejected");
        check(file_system.add_mount(file_mount("/Content", writable, 5, toy3d::MountAccess::ReadWrite)).code ==
                  toy3d::FileErrorCode::AlreadyExists,
              "an overlay group must reject a second writable layer");
        check(file_system.stat(virtual_path("/Content/common.bin")).status().code == toy3d::FileErrorCode::InvalidState,
              "lookups before freeze must fail");
        check(file_system.freeze().succeeded() && file_system.frozen(),
              "new file system snapshots must freeze explicitly");
        check(file_system.add_mount(file_mount("/Other", lower, 0)).code == toy3d::FileErrorCode::AccessDenied,
              "frozen snapshots must reject mutation");

        check(file_system.read_binary(virtual_path("/Content/common.bin")).succeeded() &&
                  file_system.read_binary(virtual_path("/Content/common.bin")).value() ==
                      std::vector<std::uint8_t>{'u'},
              "higher overlay layers must cover lower entries");
        check(file_system.read_binary(virtual_path("/Content/lower.bin")).succeeded() &&
                  file_system.read_binary(virtual_path("/Content/lower.bin")).value() == std::vector<std::uint8_t>{'o'},
              "NotFound must fall through to lower overlay layers");
        check(file_system.read_binary(virtual_path("/Content/Nested/common.bin")).succeeded() &&
                  file_system.read_binary(virtual_path("/Content/Nested/common.bin")).value() ==
                      std::vector<std::uint8_t>{'n'},
              "longest complete-segment mount roots must win");
        check(file_system.write_binary(virtual_path("/Content/new.bin"), {'x'}, toy3d::FileWriteMode::CreateNew)
                      .succeeded() &&
                  files.read_binary(physical(writable_root / "new.bin")).succeeded(),
              "writes must route only to the writable overlay layer");
        check(file_system
                  .write_binary_atomic(virtual_path("/Content/published.bin"), {'a'}, toy3d::FilePublishMode::CreateNew)
                  .succeeded(),
              "atomic create-new publication must succeed in the writable layer");
        check(file_system.write_binary_atomic(virtual_path("/Content/published.bin"), {'b'},
                                              toy3d::FilePublishMode::CreateNew)
                          .code == toy3d::FileErrorCode::AlreadyExists &&
                  file_system.read_binary(virtual_path("/Content/published.bin")).value() ==
                      std::vector<std::uint8_t>{'a'},
              "failed atomic create-new publication must preserve the destination");
        check(
            file_system
                    .write_binary_atomic(virtual_path("/Content/published.bin"), {'b'}, toy3d::FilePublishMode::Replace)
                    .succeeded() &&
                file_system.read_binary(virtual_path("/Content/published.bin")).value() ==
                    std::vector<std::uint8_t>{'b'},
            "atomic replace publication must replace the destination");

        const toy3d::FileResult<std::vector<toy3d::VirtualDirectoryEntry>> entries =
            file_system.enumerate(virtual_path("/Content"));
        const std::vector<std::string> expected_names{"common.bin",    "lower.bin",    "new.bin",
                                                      "published.bin", "revealed.bin", "upper.bin"};
        std::vector<std::string> names;
        if (entries.succeeded())
        {
            for (const toy3d::VirtualDirectoryEntry& entry : entries.value())
                names.push_back(entry.name);
        }
        check(entries.succeeded() && names == expected_names,
              "overlay enumeration must merge names with stable UTF-8 ordering");

        check(file_system.read_binary(virtual_path("/Content/revealed.bin")).succeeded() &&
                  file_system.read_binary(virtual_path("/Content/revealed.bin")).value() ==
                      std::vector<std::uint8_t>{'w'},
              "writable-layer content must cover a lower read-only entry");
        check(file_system.remove_file(virtual_path("/Content/revealed.bin")).succeeded(),
              "delete must affect the writable layer");
        check(file_system.read_binary(virtual_path("/Content/revealed.bin")).succeeded() &&
                  file_system.read_binary(virtual_path("/Content/revealed.bin")).value() ==
                      std::vector<std::uint8_t>{'r'},
              "v1 deletion must reveal lower content because tombstones are unsupported");
    }

    void test_overlay_fallback_only_on_not_found()
    {
        TestDirectory directory = make_test_directory();
        const fs::path lower_root = directory.path / "lower";
        fs::create_directories(lower_root);
        toy3d::NativePlatformFile files;
        check(files.write_binary(physical(lower_root / "entry.bin"), {9}, toy3d::FileWriteMode::CreateNew).succeeded(),
              "fallback fixture must be written");
        const auto lower = directory_store(files, lower_root, false);
        if (lower == nullptr)
            return;
        const auto failing = std::make_shared<FailingFileStore>(toy3d::FileErrorCode::IoError);

        toy3d::FileSystem file_system;
        check(file_system.add_mount(file_mount("/Fault", failing, 10)).succeeded(),
              "fault-injection layer must register");
        check(file_system.add_mount(file_mount("/Fault", lower, 0)).succeeded(), "fallback layer must register");
        check(file_system.freeze().succeeded(), "fault-injection snapshot must freeze");
        check(file_system.stat(virtual_path("/Fault/entry.bin")).status().code == toy3d::FileErrorCode::IoError,
              "stat must not hide a higher-layer I/O failure with lower content");
        check(file_system.open(virtual_path("/Fault/entry.bin"), toy3d::FileOpenMode::Read).status().code ==
                  toy3d::FileErrorCode::IoError,
              "open must fall back only after NotFound");
        check(file_system.enumerate(virtual_path("/Fault")).status().code == toy3d::FileErrorCode::IoError,
              "enumeration must not hide a higher-layer failure");
    }
} // namespace

int main()
{
    test_read_write_and_errors();
    test_file_handles();
    test_concurrent_range_reads();
    test_directories_and_paths();
    test_no_replace_rename_and_removal();
    test_concurrent_reads();
    test_virtual_path_validation();
    test_store_path_and_directory_store_contract();
    test_directory_store_symlink_boundaries();
    test_file_system_overlay_and_snapshot();
    test_overlay_fallback_only_on_not_found();

    if (failure_count != 0)
    {
        std::cerr << failure_count << " file system test(s) failed\n";
        return 1;
    }
    std::cout << "Toy3d file system tests passed\n";
    return 0;
}

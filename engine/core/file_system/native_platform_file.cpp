#include "file_system/native_platform_file.h"

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <limits>
#include <mutex>
#include <system_error>

#include "platform/platform_defines.h"

#if WITH_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if (WITH_MAC || WITH_IOS)
#include <stdio.h>
#elif (WITH_LINUX || WITH_ANDROID)
#include <sys/syscall.h>
#endif
#endif

#include "misc/utf8.h"

namespace toy3d
{
    // filesystem is confined to the native backend where canonicalization and
    // directory operations require host path semantics.
    namespace
    {
        namespace fs = std::filesystem;

        FileErrorCode map_error(const std::error_code& error)
        {
            const std::error_condition condition = error.default_error_condition();
            if (condition == std::errc::no_such_file_or_directory)
            {
                return FileErrorCode::NotFound;
            }
            if (condition == std::errc::file_exists)
            {
                return FileErrorCode::AlreadyExists;
            }
            if (condition == std::errc::permission_denied)
            {
                return FileErrorCode::AccessDenied;
            }
            if (condition == std::errc::invalid_argument || condition == std::errc::filename_too_long)
            {
                return FileErrorCode::InvalidPath;
            }
            if (condition == std::errc::not_a_directory)
            {
                return FileErrorCode::NotDirectory;
            }
            if (condition == std::errc::is_a_directory)
            {
                return FileErrorCode::IsDirectory;
            }
            if (condition == std::errc::directory_not_empty)
            {
                return FileErrorCode::NotEmpty;
            }
            if (condition == std::errc::cross_device_link)
            {
                return FileErrorCode::CrossDevice;
            }
            if (condition == std::errc::file_too_large)
            {
                return FileErrorCode::TooLarge;
            }
            if (condition == std::errc::device_or_resource_busy)
            {
                return FileErrorCode::Busy;
            }
            if (condition == std::errc::operation_not_supported || condition == std::errc::function_not_supported)
            {
                return FileErrorCode::Unsupported;
            }
            return FileErrorCode::IoError;
        }

        FileStatus make_error(const char* operation, const PhysicalPath& path, const std::error_code& error,
                              std::string message = {})
        {
            FileStatus status;
            status.code = map_error(error);
            status.operation = operation;
            status.path = path;
            status.message = message.empty() ? error.message() : std::move(message);
            status.platform_error = error.value();
            return status;
        }

        FileStatus invalid_path(const char* operation, const PhysicalPath& path, const char* message)
        {
            FileStatus status;
            status.code = FileErrorCode::InvalidPath;
            status.operation = operation;
            status.path = path;
            status.message = message;
            return status;
        }

        fs::path to_native(const PhysicalPath& path)
        {
            fs::path native = fs::u8path(path.utf8());
#if WITH_WIN
            if (native.is_absolute())
            {
                std::wstring text = native.lexically_normal().make_preferred().native();
                if (text.compare(0u, 4u, L"\\\\?\\") != 0u && text.compare(0u, 4u, L"\\\\.\\") != 0u)
                    text = text.compare(0u, 2u, L"\\\\") == 0u ? L"\\\\?\\UNC\\" + text.substr(2u) : L"\\\\?\\" + text;
                native = fs::path(std::move(text));
            }
#endif
            return native;
        }

        PhysicalPath from_native(const fs::path& path)
        {
#if WITH_WIN
            const std::wstring& text = path.native();
            if (text.compare(0u, 8u, L"\\\\?\\UNC\\") == 0u)
                return PhysicalPath(fs::path(L"\\\\" + text.substr(8u)).u8string());
            if (text.compare(0u, 4u, L"\\\\?\\") == 0u)
                return PhysicalPath(fs::path(text.substr(4u)).u8string());
#endif
            return PhysicalPath(path.u8string());
        }

#if WITH_WIN
        FileResult<std::wstring> windows_api_path(const PhysicalPath& path)
        {
            // filesystem normalizes a trusted host path before adding Win32's
            // extended-length prefix; PhysicalPath itself keeps ordinary UTF-8.
            std::error_code error;
            auto absolute = fs::absolute(to_native(path), error);
            if (error) return FileResult<std::wstring>(make_error("native_path", path, error));
            std::wstring native = absolute.lexically_normal().make_preferred().native();
            if (native.compare(0u, 4u, L"\\\\?\\") == 0u || native.compare(0u, 4u, L"\\\\.\\") == 0u)
                return FileResult<std::wstring>(std::move(native));
            if (native.compare(0u, 2u, L"\\\\") == 0u)
                return FileResult<std::wstring>(L"\\\\?\\UNC\\" + native.substr(2u));
            return FileResult<std::wstring>(L"\\\\?\\" + native);
        }
#endif

        FileType to_file_type(fs::file_type type)
        {
            switch (type)
            {
            case fs::file_type::regular:
                return FileType::File;
            case fs::file_type::directory:
                return FileType::Directory;
            case fs::file_type::symlink:
                return FileType::Symlink;
            default:
                return FileType::Other;
            }
        }


        FileStatus handle_error(FileErrorCode code, const char* operation, const PhysicalPath& path,
                                const char* message)
        {
            FileStatus status;
            status.code = code;
            status.operation = operation;
            status.path = path;
            status.message = message;
            return status;
        }

        class NativeFileHandle final : public FileHandle
        {
          public:
#if WITH_WIN
            NativeFileHandle(HANDLE handle, PhysicalPath path, bool readable, bool writable)
                : handle_(handle), path_(std::move(path)), readable_(readable), writable_(writable)
            {
            }
#else
            NativeFileHandle(int handle, PhysicalPath path, bool readable, bool writable)
                : handle_(handle), path_(std::move(path)), readable_(readable), writable_(writable)
            {
            }
#endif

            ~NativeFileHandle() override { close(); }

            FileResult<std::uint64_t> size() const override
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!is_open())
                {
                    return FileResult<std::uint64_t>(invalid_state("size"));
                }
#if WITH_WIN
                LARGE_INTEGER value{};
                if (!GetFileSizeEx(handle_, &value))
                {
                    return FileResult<std::uint64_t>(last_error("size"));
                }
                return FileResult<std::uint64_t>(static_cast<std::uint64_t>(value.QuadPart));
#else
                struct stat native_stat{};
                if (::fstat(handle_, &native_stat) != 0)
                {
                    return FileResult<std::uint64_t>(last_error("size"));
                }
                return FileResult<std::uint64_t>(static_cast<std::uint64_t>(native_stat.st_size));
#endif
            }

            FileResult<std::size_t> read(std::uint8_t* destination, std::size_t byte_count) override
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!is_open())
                    return FileResult<std::size_t>(invalid_state("read"));
                if (!readable_)
                {
                    return FileResult<std::size_t>(
                        handle_error(FileErrorCode::AccessDenied, "read", path_, "handle is not readable"));
                }
                if (byte_count != 0 && destination == nullptr)
                {
                    return FileResult<std::size_t>(invalid_path("read", path_, "destination is null"));
                }
                return read_locked(destination, byte_count);
            }

            FileResult<std::size_t> write(const std::uint8_t* source, std::size_t byte_count) override
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!is_open())
                    return FileResult<std::size_t>(invalid_state("write"));
                if (!writable_)
                {
                    return FileResult<std::size_t>(
                        handle_error(FileErrorCode::AccessDenied, "write", path_, "handle is not writable"));
                }
                if (byte_count != 0 && source == nullptr)
                {
                    return FileResult<std::size_t>(invalid_path("write", path_, "source is null"));
                }
                if (byte_count == 0)
                    return FileResult<std::size_t>(std::size_t{0});
#if WITH_WIN
                const DWORD requested =
                    static_cast<DWORD>(std::min<std::size_t>(byte_count, std::numeric_limits<DWORD>::max()));
                DWORD written = 0;
                if (!WriteFile(handle_, source, requested, &written, nullptr))
                {
                    return FileResult<std::size_t>(last_error("write"));
                }
                return FileResult<std::size_t>(static_cast<std::size_t>(written));
#else
                while (true)
                {
                    const ssize_t written = ::write(handle_, source, byte_count);
                    if (written >= 0)
                    {
                        return FileResult<std::size_t>(static_cast<std::size_t>(written));
                    }
                    if (errno != EINTR)
                        return FileResult<std::size_t>(last_error("write"));
                }
#endif
            }

            FileResult<std::uint64_t> tell() const override
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!is_open())
                    return FileResult<std::uint64_t>(invalid_state("tell"));
#if WITH_WIN
                LARGE_INTEGER distance{};
                LARGE_INTEGER position{};
                if (!SetFilePointerEx(handle_, distance, &position, FILE_CURRENT))
                {
                    return FileResult<std::uint64_t>(last_error("tell"));
                }
                return FileResult<std::uint64_t>(static_cast<std::uint64_t>(position.QuadPart));
#else
                const off_t position = ::lseek(handle_, 0, SEEK_CUR);
                if (position < 0)
                {
                    return FileResult<std::uint64_t>(last_error("tell"));
                }
                return FileResult<std::uint64_t>(static_cast<std::uint64_t>(position));
#endif
            }

            FileStatus seek(std::uint64_t offset) override
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!is_open())
                    return invalid_state("seek");
#if WITH_WIN
                if (offset > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()))
                {
                    return handle_error(FileErrorCode::TooLarge, "seek", path_, "offset is too large");
                }
                LARGE_INTEGER distance{};
                distance.QuadPart = static_cast<LONGLONG>(offset);
                if (!SetFilePointerEx(handle_, distance, nullptr, FILE_BEGIN))
                    return last_error("seek");
#else
                if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()))
                {
                    return handle_error(FileErrorCode::TooLarge, "seek", path_, "offset is too large");
                }
                if (::lseek(handle_, static_cast<off_t>(offset), SEEK_SET) < 0)
                    return last_error("seek");
#endif
                return FileStatus::success();
            }

            FileResult<std::size_t> read_at(std::uint64_t offset, std::uint8_t* destination,
                                            std::size_t byte_count) const override
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!is_open())
                    return FileResult<std::size_t>(invalid_state("read_at"));
                if (!readable_)
                {
                    return FileResult<std::size_t>(
                        handle_error(FileErrorCode::AccessDenied, "read_at", path_, "handle is not readable"));
                }
                if (byte_count != 0 && destination == nullptr)
                {
                    return FileResult<std::size_t>(invalid_path("read_at", path_, "destination is null"));
                }
#if WITH_WIN
                LARGE_INTEGER current{};
                LARGE_INTEGER zero{};
                if (!SetFilePointerEx(handle_, zero, &current, FILE_CURRENT))
                {
                    return FileResult<std::size_t>(last_error("read_at"));
                }
                if (offset > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()))
                {
                    return FileResult<std::size_t>(
                        handle_error(FileErrorCode::TooLarge, "read_at", path_, "offset is too large"));
                }
                LARGE_INTEGER target{};
                target.QuadPart = static_cast<LONGLONG>(offset);
                if (!SetFilePointerEx(handle_, target, nullptr, FILE_BEGIN))
                {
                    return FileResult<std::size_t>(last_error("read_at"));
                }
                FileResult<std::size_t> result = read_locked(destination, byte_count);
                if (!SetFilePointerEx(handle_, current, nullptr, FILE_BEGIN))
                {
                    return FileResult<std::size_t>(last_error("read_at_restore"));
                }
                return result;
#else
                if (offset > static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()))
                {
                    return FileResult<std::size_t>(
                        handle_error(FileErrorCode::TooLarge, "read_at", path_, "offset is too large"));
                }
                while (true)
                {
                    const ssize_t read_count = ::pread(handle_, destination, byte_count, static_cast<off_t>(offset));
                    if (read_count >= 0)
                    {
                        return FileResult<std::size_t>(static_cast<std::size_t>(read_count));
                    }
                    if (errno != EINTR)
                        return FileResult<std::size_t>(last_error("read_at"));
                }
#endif
            }

            FileStatus flush() override
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!is_open())
                    return invalid_state("flush");
                if (!writable_)
                    return FileStatus::success();
#if WITH_WIN
                if (!FlushFileBuffers(handle_))
                    return last_error("flush");
#else
                if (::fsync(handle_) != 0)
                    return last_error("flush");
#endif
                return FileStatus::success();
            }

            FileStatus close() override
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!is_open())
                    return FileStatus::success();
#if WITH_WIN
                const HANDLE closing = handle_;
                handle_ = INVALID_HANDLE_VALUE;
                if (!CloseHandle(closing))
                    return last_error("close");
#else
                const int closing = handle_;
                handle_ = -1;
                if (::close(closing) != 0)
                    return last_error("close");
#endif
                return FileStatus::success();
            }

          private:
            bool is_open() const
            {
#if WITH_WIN
                return handle_ != INVALID_HANDLE_VALUE;
#else
                return handle_ >= 0;
#endif
            }

            FileStatus invalid_state(const char* operation) const
            {
                return handle_error(FileErrorCode::InvalidState, operation, path_, "file handle is closed");
            }

            FileStatus last_error(const char* operation) const
            {
#if WITH_WIN
                return make_error(operation, path_,
                                  std::error_code(static_cast<int>(GetLastError()), std::system_category()));
#else
                return make_error(operation, path_, std::error_code(errno, std::generic_category()));
#endif
            }

            FileResult<std::size_t> read_locked(std::uint8_t* destination, std::size_t byte_count) const
            {
                if (byte_count == 0)
                    return FileResult<std::size_t>(std::size_t{0});
#if WITH_WIN
                const DWORD requested =
                    static_cast<DWORD>(std::min<std::size_t>(byte_count, std::numeric_limits<DWORD>::max()));
                DWORD read_count = 0;
                if (!ReadFile(handle_, destination, requested, &read_count, nullptr))
                {
                    return FileResult<std::size_t>(last_error("read"));
                }
                return FileResult<std::size_t>(static_cast<std::size_t>(read_count));
#else
                while (true)
                {
                    const ssize_t read_count = ::read(handle_, destination, byte_count);
                    if (read_count >= 0)
                    {
                        return FileResult<std::size_t>(static_cast<std::size_t>(read_count));
                    }
                    if (errno != EINTR)
                        return FileResult<std::size_t>(last_error("read"));
                }
#endif
            }

#if WITH_WIN
            mutable HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
            mutable int handle_ = -1;
#endif
            PhysicalPath path_;
            bool readable_ = false;
            bool writable_ = false;
            mutable std::mutex mutex_;
        };
    } // namespace

    PlatformFileCapabilities NativePlatformFile::capabilities() const
    {
        PlatformFileCapabilities result;
        result.supports_atomic_replace = true;
#if !WITH_WIN && !(WITH_MAC || WITH_IOS) && !((WITH_LINUX || WITH_ANDROID) && defined(SYS_renameat2))
        result.supports_atomic_rename = false;
#endif
        return result;
    }

    FileResult<std::unique_ptr<FileHandle>> NativePlatformFile::open(const PhysicalPath& path, FileOpenMode mode) const
    {
        if (path.empty() || !path.valid())
        {
            return FileResult<std::unique_ptr<FileHandle>>(invalid_path("open", path, "path is empty or invalid"));
        }
        const fs::path native_path = to_native(path);
        const bool readable = mode == FileOpenMode::Read || mode == FileOpenMode::ReadWrite;
        const bool writable = mode != FileOpenMode::Read;
#if WITH_WIN
        const auto windows_path = windows_api_path(path);
        if (!windows_path.succeeded()) return FileResult<std::unique_ptr<FileHandle>>(windows_path.status());
        const DWORD access = (readable ? GENERIC_READ : 0u) | (writable ? GENERIC_WRITE : 0u);
        const DWORD creation = mode == FileOpenMode::Read            ? OPEN_EXISTING
                               : mode == FileOpenMode::WriteNew      ? CREATE_NEW
                               : mode == FileOpenMode::WriteTruncate ? CREATE_ALWAYS
                                                                     : OPEN_ALWAYS;
        const HANDLE handle = CreateFileW(windows_path.value().c_str(), access, FILE_SHARE_READ, nullptr, creation,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            return FileResult<std::unique_ptr<FileHandle>>(
                make_error("open", path, std::error_code(static_cast<int>(GetLastError()), std::system_category())));
        }
        return FileResult<std::unique_ptr<FileHandle>>(
            std::make_unique<NativeFileHandle>(handle, path, readable, writable));
#else
        const int access = readable && writable ? O_RDWR : readable ? O_RDONLY : O_WRONLY;
        const int flags = access | (mode == FileOpenMode::WriteNew        ? O_CREAT | O_EXCL
                                    : mode == FileOpenMode::WriteTruncate ? O_CREAT | O_TRUNC
                                    : mode == FileOpenMode::ReadWrite     ? O_CREAT
                                                                          : 0);
        const int handle = ::open(native_path.c_str(), flags, 0666);
        if (handle < 0)
        {
            return FileResult<std::unique_ptr<FileHandle>>(
                make_error("open", path, std::error_code(errno, std::generic_category())));
        }
        return FileResult<std::unique_ptr<FileHandle>>(
            std::make_unique<NativeFileHandle>(handle, path, readable, writable));
#endif
    }

    FileResult<FileStat> NativePlatformFile::stat(const PhysicalPath& path) const
    {
        if (path.empty() || !path.valid())
        {
            return FileResult<FileStat>(invalid_path("stat", path, "path is empty"));
        }
        try
        {
            std::error_code error;
            const fs::file_status native_status = fs::symlink_status(to_native(path), error);
            if (error)
            {
                return FileResult<FileStat>(make_error("stat", path, error));
            }
            if (native_status.type() == fs::file_type::not_found)
            {
                return FileResult<FileStat>(
                    make_error("stat", path, std::make_error_code(std::errc::no_such_file_or_directory)));
            }
            FileStat result;
            result.type = to_file_type(native_status.type());
            if (result.type == FileType::File)
            {
                result.size = fs::file_size(to_native(path), error);
                if (error)
                {
                    return FileResult<FileStat>(make_error("stat", path, error));
                }
            }
            return FileResult<FileStat>(result);
        }
        catch (const fs::filesystem_error& error)
        {
            return FileResult<FileStat>(make_error("stat", path, error.code(), error.what()));
        }
    }

    FileResult<bool> NativePlatformFile::exists(const PhysicalPath& path) const
    {
        const FileResult<FileStat> result = stat(path);
        if (result.succeeded())
        {
            return FileResult<bool>(true);
        }
        if (result.status().code == FileErrorCode::NotFound)
        {
            return FileResult<bool>(false);
        }
        return FileResult<bool>(result.status());
    }

    FileResult<std::vector<std::uint8_t>> NativePlatformFile::read_binary(const PhysicalPath& path) const
    {
        const FileResult<FileStat> file_stat = stat(path);
        if (!file_stat.succeeded())
        {
            return FileResult<std::vector<std::uint8_t>>(file_stat.status());
        }
        if (file_stat.value().type == FileType::Directory)
        {
            return FileResult<std::vector<std::uint8_t>>(
                handle_error(FileErrorCode::IsDirectory, "read_binary", path, "path is a directory"));
        }
        if (file_stat.value().type != FileType::File)
        {
            return FileResult<std::vector<std::uint8_t>>(
                handle_error(FileErrorCode::Unsupported, "read_binary", path, "path is not a regular file"));
        }
        FileResult<std::unique_ptr<FileHandle>> opened = open(path, FileOpenMode::Read);
        if (!opened.succeeded())
        {
            return FileResult<std::vector<std::uint8_t>>(opened.status());
        }
        std::unique_ptr<FileHandle> handle = std::move(opened.value());
        const FileResult<std::uint64_t> file_size = handle->size();
        if (!file_size.succeeded())
        {
            return FileResult<std::vector<std::uint8_t>>(file_size.status());
        }
        if (file_size.value() > default_maximum_file_read_size ||
            file_size.value() > std::numeric_limits<std::size_t>::max())
        {
            return FileResult<std::vector<std::uint8_t>>(
                handle_error(FileErrorCode::TooLarge, "read_binary", path, "file exceeds the whole-file read limit"));
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size.value()));
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            const FileResult<std::size_t> read_count = handle->read(bytes.data() + offset, bytes.size() - offset);
            if (!read_count.succeeded())
            {
                return FileResult<std::vector<std::uint8_t>>(read_count.status());
            }
            if (read_count.value() == 0)
            {
                bytes.resize(offset);
                break;
            }
            offset += read_count.value();
        }
        const FileStatus close_status = handle->close();
        if (!close_status.succeeded())
        {
            return FileResult<std::vector<std::uint8_t>>(close_status);
        }
        return FileResult<std::vector<std::uint8_t>>(std::move(bytes));
    }

    FileResult<std::string> NativePlatformFile::read_text_utf8(const PhysicalPath& path) const
    {
        FileResult<std::vector<std::uint8_t>> bytes = read_binary(path);
        if (!bytes.succeeded())
        {
            return FileResult<std::string>(bytes.status());
        }
        std::string text(bytes.value().begin(), bytes.value().end());
        if (!is_valid_utf8(text))
        {
            FileStatus status;
            status.code = FileErrorCode::InvalidData;
            status.operation = "read_text_utf8";
            status.path = path;
            status.message = "file content is not valid UTF-8";
            return FileResult<std::string>(std::move(status));
        }
        return FileResult<std::string>(std::move(text));
    }

    FileStatus NativePlatformFile::write_text_utf8(const PhysicalPath& path, const std::string& text,
                                                   FileWriteMode mode)
    {
        if (!is_valid_utf8(text))
        {
            FileStatus status;
            status.code = FileErrorCode::InvalidData;
            status.operation = "write_text_utf8";
            status.path = path;
            status.message = "text is not valid UTF-8";
            return status;
        }
        return write_binary(path, std::vector<std::uint8_t>(text.begin(), text.end()), mode);
    }

    FileStatus NativePlatformFile::write_binary(const PhysicalPath& path, const std::vector<std::uint8_t>& bytes,
                                                FileWriteMode mode)
    {
        if (path.empty() || !path.valid())
        {
            return invalid_path("write_binary", path, "path is empty");
        }
        const FileOpenMode open_mode =
            mode == FileWriteMode::CreateNew ? FileOpenMode::WriteNew : FileOpenMode::WriteTruncate;
        FileResult<std::unique_ptr<FileHandle>> opened = open(path, open_mode);
        if (!opened.succeeded())
        {
            return opened.status();
        }
        std::unique_ptr<FileHandle> handle = std::move(opened.value());
        std::size_t offset = 0;
        while (offset < bytes.size())
        {
            const FileResult<std::size_t> written = handle->write(bytes.data() + offset, bytes.size() - offset);
            if (!written.succeeded())
                return written.status();
            if (written.value() == 0)
            {
                return handle_error(FileErrorCode::IoError, "write_binary", path, "write returned zero bytes");
            }
            offset += written.value();
        }
        const FileStatus flush_status = handle->flush();
        if (!flush_status.succeeded())
            return flush_status;
        return handle->close();
    }

    FileStatus NativePlatformFile::create_directory(const PhysicalPath& path)
    {
        if (path.empty() || !path.valid())
        {
            return invalid_path("create_directory", path, "path is empty");
        }
        try
        {
            std::error_code error;
            const bool created = fs::create_directory(to_native(path), error);
            if (error)
            {
                return make_error("create_directory", path, error);
            }
            if (!created)
            {
                return make_error("create_directory", path, std::make_error_code(std::errc::file_exists));
            }
            return FileStatus::success();
        }
        catch (const fs::filesystem_error& error)
        {
            return make_error("create_directory", path, error.code(), error.what());
        }
    }

    FileStatus NativePlatformFile::create_directories(const PhysicalPath& path)
    {
        if (path.empty() || !path.valid())
        {
            return invalid_path("create_directories", path, "path is empty");
        }
        try
        {
            std::error_code error;
            fs::create_directories(to_native(path), error);
            if (error)
            {
                return make_error("create_directories", path, error);
            }
            const FileResult<FileStat> result = stat(path);
            if (!result.succeeded())
            {
                return result.status();
            }
            if (result.value().type != FileType::Directory)
            {
                return make_error("create_directories", path, std::make_error_code(std::errc::not_a_directory));
            }
            return FileStatus::success();
        }
        catch (const fs::filesystem_error& error)
        {
            return make_error("create_directories", path, error.code(), error.what());
        }
    }

    FileStatus NativePlatformFile::rename_no_replace(const PhysicalPath& source, const PhysicalPath& destination)
    {
        if (source.empty() || destination.empty() || !source.valid() || !destination.valid())
        {
            return invalid_path("rename_no_replace", destination, "source or destination path is empty");
        }
        try
        {
            const fs::path native_source = to_native(source);
            const fs::path native_destination = to_native(destination);
            if (native_source.parent_path().lexically_normal() != native_destination.parent_path().lexically_normal())
            {
                return invalid_path("rename_no_replace", destination,
                                    "rename must stay within the same parent directory");
            }
#if WITH_WIN
            const auto windows_source = windows_api_path(source);
            const auto windows_destination = windows_api_path(destination);
            if (!windows_source.succeeded()) return windows_source.status();
            if (!windows_destination.succeeded()) return windows_destination.status();
            if (!MoveFileExW(windows_source.value().c_str(), windows_destination.value().c_str(), MOVEFILE_WRITE_THROUGH))
            {
                return make_error("rename_no_replace", destination,
                                  std::error_code(static_cast<int>(GetLastError()), std::system_category()));
            }
#elif (WITH_MAC || WITH_IOS)
            if (::renamex_np(native_source.c_str(), native_destination.c_str(), RENAME_EXCL) != 0)
            {
                return make_error("rename_no_replace", destination, std::error_code(errno, std::generic_category()));
            }
#elif (WITH_LINUX || WITH_ANDROID) && defined(SYS_renameat2)
            constexpr unsigned int rename_no_replace = 1u;
            if (::syscall(SYS_renameat2, AT_FDCWD, native_source.c_str(), AT_FDCWD, native_destination.c_str(),
                          rename_no_replace) != 0)
            {
                return make_error("rename_no_replace", destination, std::error_code(errno, std::generic_category()));
            }
#else
            FileStatus unsupported;
            unsupported.code = FileErrorCode::Unsupported;
            unsupported.operation = "rename_no_replace";
            unsupported.path = destination;
            unsupported.message = "atomic no-replace rename is unavailable on this platform";
            return unsupported;
#endif
            return FileStatus::success();
        }
        catch (const fs::filesystem_error& error)
        {
            return make_error("rename_no_replace", destination, error.code(), error.what());
        }
    }

    FileStatus NativePlatformFile::replace(const PhysicalPath& source, const PhysicalPath& destination)
    {
        if (source.empty() || destination.empty() || !source.valid() || !destination.valid())
        {
            return invalid_path("replace", destination, "source or destination path is empty");
        }
        try
        {
            const fs::path native_source = to_native(source);
            const fs::path native_destination = to_native(destination);
            if (native_source.parent_path().lexically_normal() != native_destination.parent_path().lexically_normal())
            {
                return invalid_path("replace", destination, "replace must stay within the same parent directory");
            }
#if WITH_WIN
            const auto windows_source = windows_api_path(source);
            const auto windows_destination = windows_api_path(destination);
            if (!windows_source.succeeded()) return windows_source.status();
            if (!windows_destination.succeeded()) return windows_destination.status();
            if (!MoveFileExW(windows_source.value().c_str(), windows_destination.value().c_str(),
                             MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                return make_error("replace", destination,
                                  std::error_code(static_cast<int>(GetLastError()), std::system_category()));
            }
#else
            if (::rename(native_source.c_str(), native_destination.c_str()) != 0)
            {
                return make_error("replace", destination, std::error_code(errno, std::generic_category()));
            }
#endif
            return FileStatus::success();
        }
        catch (const fs::filesystem_error& error)
        {
            return make_error("replace", destination, error.code(), error.what());
        }
    }

    FileStatus NativePlatformFile::remove_file(const PhysicalPath& path)
    {
        const FileResult<FileStat> file_stat = stat(path);
        if (!file_stat.succeeded())
        {
            return file_stat.status();
        }
        if (file_stat.value().type == FileType::Directory)
        {
            return make_error("remove_file", path, std::make_error_code(std::errc::is_a_directory));
        }
        try
        {
            std::error_code error;
            const bool removed = fs::remove(to_native(path), error);
            if (error)
            {
                return make_error("remove_file", path, error);
            }
            if (!removed)
            {
                return make_error("remove_file", path, std::make_error_code(std::errc::no_such_file_or_directory));
            }
            return FileStatus::success();
        }
        catch (const fs::filesystem_error& error)
        {
            return make_error("remove_file", path, error.code(), error.what());
        }
    }

    FileStatus NativePlatformFile::remove_empty_directory(const PhysicalPath& path)
    {
        const FileResult<FileStat> file_stat = stat(path);
        if (!file_stat.succeeded())
        {
            return file_stat.status();
        }
        if (file_stat.value().type != FileType::Directory)
        {
            return make_error("remove_empty_directory", path, std::make_error_code(std::errc::not_a_directory));
        }
        try
        {
            std::error_code error;
            const bool removed = fs::remove(to_native(path), error);
            if (error)
            {
                return make_error("remove_empty_directory", path, error);
            }
            if (!removed)
            {
                return make_error("remove_empty_directory", path, std::make_error_code(std::errc::directory_not_empty));
            }
            return FileStatus::success();
        }
        catch (const fs::filesystem_error& error)
        {
            return make_error("remove_empty_directory", path, error.code(), error.what());
        }
    }

    FileResult<std::uintmax_t> NativePlatformFile::remove_directory_tree(const PhysicalPath& path)
    {
        if (path.empty() || !path.valid())
        {
            return FileResult<std::uintmax_t>(invalid_path("remove_directory_tree", path, "path is empty"));
        }
        try
        {
            std::error_code error;
            const fs::path absolute_path = fs::absolute(to_native(path), error).lexically_normal();
            if (error)
            {
                return FileResult<std::uintmax_t>(make_error("remove_directory_tree", path, error));
            }
            if (absolute_path == absolute_path.root_path())
            {
                return FileResult<std::uintmax_t>(
                    invalid_path("remove_directory_tree", path, "filesystem root cannot be removed"));
            }
            const std::uintmax_t count = fs::remove_all(absolute_path, error);
            if (error)
            {
                return FileResult<std::uintmax_t>(make_error("remove_directory_tree", path, error));
            }
            return FileResult<std::uintmax_t>(count);
        }
        catch (const fs::filesystem_error& error)
        {
            return FileResult<std::uintmax_t>(make_error("remove_directory_tree", path, error.code(), error.what()));
        }
    }

    FileResult<std::vector<DirectoryEntry>> NativePlatformFile::enumerate_directory(const PhysicalPath& path) const
    {
        const FileResult<FileStat> directory_stat = stat(path);
        if (!directory_stat.succeeded())
        {
            return FileResult<std::vector<DirectoryEntry>>(directory_stat.status());
        }
        if (directory_stat.value().type != FileType::Directory)
        {
            return FileResult<std::vector<DirectoryEntry>>(
                make_error("enumerate_directory", path, std::make_error_code(std::errc::not_a_directory)));
        }
        try
        {
            std::vector<DirectoryEntry> entries;
            std::error_code error;
            fs::directory_iterator iterator(to_native(path), error);
            const fs::directory_iterator end;
            while (!error && iterator != end)
            {
                const fs::file_status entry_status = iterator->symlink_status(error);
                if (error)
                {
                    break;
                }
                entries.push_back({from_native(iterator->path()), to_file_type(entry_status.type())});
                iterator.increment(error);
            }
            if (error)
            {
                return FileResult<std::vector<DirectoryEntry>>(make_error("enumerate_directory", path, error));
            }
            std::sort(entries.begin(), entries.end(), [](const DirectoryEntry& lhs, const DirectoryEntry& rhs)
                      { return lhs.path.utf8() < rhs.path.utf8(); });
            return FileResult<std::vector<DirectoryEntry>>(std::move(entries));
        }
        catch (const fs::filesystem_error& error)
        {
            return FileResult<std::vector<DirectoryEntry>>(
                make_error("enumerate_directory", path, error.code(), error.what()));
        }
    }

    FileResult<PhysicalPath> NativePlatformFile::absolute(const PhysicalPath& path) const
    {
        if (path.empty() || !path.valid())
        {
            return FileResult<PhysicalPath>(invalid_path("absolute", path, "path is empty"));
        }
        try
        {
            std::error_code error;
            const fs::path result = fs::absolute(to_native(path), error);
            if (error)
            {
                return FileResult<PhysicalPath>(make_error("absolute", path, error));
            }
            return FileResult<PhysicalPath>(from_native(result));
        }
        catch (const fs::filesystem_error& error)
        {
            return FileResult<PhysicalPath>(make_error("absolute", path, error.code(), error.what()));
        }
    }

    FileResult<PhysicalPath> NativePlatformFile::lexically_normal(const PhysicalPath& path) const
    {
        if (path.empty() || !path.valid())
        {
            return FileResult<PhysicalPath>(invalid_path("lexically_normal", path, "path is empty"));
        }
        try
        {
            return FileResult<PhysicalPath>(from_native(to_native(path).lexically_normal()));
        }
        catch (const fs::filesystem_error& error)
        {
            return FileResult<PhysicalPath>(make_error("lexically_normal", path, error.code(), error.what()));
        }
    }

    FileResult<PhysicalPath> NativePlatformFile::canonical(const PhysicalPath& path) const
    {
        if (path.empty() || !path.valid())
        {
            return FileResult<PhysicalPath>(invalid_path("canonical", path, "path is empty"));
        }
        try
        {
            std::error_code error;
            const fs::path result = fs::canonical(to_native(path), error);
            if (error)
            {
                return FileResult<PhysicalPath>(make_error("canonical", path, error));
            }
            return FileResult<PhysicalPath>(from_native(result));
        }
        catch (const fs::filesystem_error& error)
        {
            return FileResult<PhysicalPath>(make_error("canonical", path, error.code(), error.what()));
        }
    }

    FileResult<PhysicalPath> NativePlatformFile::parent_path(const PhysicalPath& path) const
    {
        if (path.empty() || !path.valid())
        {
            return FileResult<PhysicalPath>(invalid_path("parent_path", path, "path is empty"));
        }
        try
        {
            const fs::path parent = to_native(path).parent_path();
            if (parent.empty())
            {
                return FileResult<PhysicalPath>(invalid_path("parent_path", path, "path has no parent"));
            }
            return FileResult<PhysicalPath>(from_native(parent));
        }
        catch (const fs::filesystem_error& error)
        {
            return FileResult<PhysicalPath>(make_error("parent_path", path, error.code(), error.what()));
        }
    }

    FileResult<PhysicalPath> NativePlatformFile::join_relative(const PhysicalPath& base,
                                                               const std::string& generic_relative_path) const
    {
        if (base.empty() || !base.valid())
        {
            return FileResult<PhysicalPath>(invalid_path("join_relative", base, "base path is empty"));
        }
        const PhysicalPath relative(generic_relative_path);
        if (!relative.valid())
        {
            return FileResult<PhysicalPath>(invalid_path("join_relative", base, "relative path is not valid UTF-8"));
        }
        try
        {
            const fs::path native_relative = fs::u8path(generic_relative_path);
            if (native_relative.is_absolute() || native_relative.has_root_path())
            {
                return FileResult<PhysicalPath>(
                    invalid_path("join_relative", base, "relative path must not contain a root"));
            }
            for (const fs::path& component : native_relative)
            {
                if (component == "." || component == "..")
                {
                    return FileResult<PhysicalPath>(
                        invalid_path("join_relative", base, "relative path must not contain dot segments"));
                }
            }
            return FileResult<PhysicalPath>(from_native((to_native(base) / native_relative).lexically_normal()));
        }
        catch (const fs::filesystem_error& error)
        {
            return FileResult<PhysicalPath>(make_error("join_relative", base, error.code(), error.what()));
        }
    }
} // namespace toy3d

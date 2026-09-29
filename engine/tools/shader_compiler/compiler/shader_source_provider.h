#pragma once

#include "hash/sha256.h"
#include "file_system/file_system.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace toy3d::shader
{
    struct VirtualIncludeFile
    {
        std::string virtual_path;
        std::string source;
    };

    struct ShaderSourceRecord
    {
        std::string virtual_path;
        std::string source;
        Sha256Hash content_hash{};
    };

    struct ShaderSourceLoadResult
    {
        // optional ensures a failed lookup cannot return a partially populated
        // source record alongside its error.
        std::optional<ShaderSourceRecord> source;
        std::string error;

        bool succeeded() const;
    };

    class ShaderSourceProvider
    {
      public:
        virtual ~ShaderSourceProvider() = default;
        virtual const std::string& validation_error() const = 0;
        virtual ShaderSourceLoadResult load(const std::string& virtual_path) const = 0;
    };

    class RegisteredShaderSourceProvider final : public ShaderSourceProvider
    {
      public:
        explicit RegisteredShaderSourceProvider(std::vector<VirtualIncludeFile> files);
        const std::string& validation_error() const override;
        ShaderSourceLoadResult load(const std::string& virtual_path) const override;

      private:
        std::unordered_map<std::string, ShaderSourceRecord> files_;
        std::string validation_error_;
    };

    // Explicit read-only mounts constrain disk includes; no CWD, PATH or
    // system include fallback participates in compilation.
    class FileShaderSourceProvider final : public ShaderSourceProvider
    {
      public:
        explicit FileShaderSourceProvider(const FileSystem& files) : files_(files) {}
        const std::string& validation_error() const override { return error_; }
        ShaderSourceLoadResult load(const std::string& virtual_path) const override;
      private:
        const FileSystem& files_;
        std::string error_;
    };
} // namespace toy3d::shader

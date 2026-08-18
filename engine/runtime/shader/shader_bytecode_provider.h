#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    class FileSystem;

    struct ShaderBytecodeLoadResult
    {
        std::vector<std::uint8_t> bytes;
        std::string error;

        bool succeeded() const;
    };

    class ShaderBytecodeProvider
    {
    public:
        virtual ~ShaderBytecodeProvider() = default;
        virtual ShaderBytecodeLoadResult load(const std::string& shader_name) const = 0;
    };

    class FileSystemShaderBytecodeProvider final : public ShaderBytecodeProvider
    {
    public:
        FileSystemShaderBytecodeProvider(FileSystem& file_system, std::string virtual_root);
        ShaderBytecodeLoadResult load(const std::string& shader_name) const override;

    private:
        FileSystem& file_system_;
        std::string virtual_root_;
    };
}

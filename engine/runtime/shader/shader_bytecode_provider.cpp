#include "shader/shader_bytecode_provider.h"

#include "file_system/file_system.h"

#include <utility>

namespace toy3d
{
    bool ShaderBytecodeLoadResult::succeeded() const
    {
        return error.empty();
    }

    FileSystemShaderBytecodeProvider::FileSystemShaderBytecodeProvider(
        FileSystem& file_system,
        std::string virtual_root)
        : file_system_(file_system),
          virtual_root_(std::move(virtual_root))
    {
    }

    ShaderBytecodeLoadResult FileSystemShaderBytecodeProvider::load(
        const std::string& shader_name) const
    {
        ShaderBytecodeLoadResult result;
        const auto path = VirtualPath::parse(virtual_root_ + "/" + shader_name);
        if (!path.succeeded())
        {
            result.error = "Invalid shader bytecode name: " + shader_name;
            return result;
        }
        auto bytes = file_system_.read_binary(path.value());
        if (!bytes.succeeded())
        {
            result.error = "Failed to load shader bytecode " + path.value().utf8() +
                ": " + bytes.status().message;
            return result;
        }
        result.bytes = std::move(bytes.value());
        return result;
    }
}

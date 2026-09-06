#include "rendercore/shader/loaders/shader_code_library_loader.h"

#include <utility>

namespace toy3d
{
    ShaderCodeLibraryLoader::ShaderCodeLibraryLoader(PhysicalPath library_path) : library_path_(std::move(library_path))
    {
    }

    ShaderMapProgramLoadResult ShaderCodeLibraryLoader::load_program(const ShaderMapProgramKey&) const
    {
        ShaderMapProgramLoadResult result;
        result.error = "ShaderCodeLibrary loading is not implemented for '" + library_path_.utf8() + "'.";
        return result;
    }
} // namespace toy3d

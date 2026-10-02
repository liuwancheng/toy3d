#pragma once

#include "codegen/shader_parameters_codegen.h"
#include "file_system/platform_file.h"

#include <string>
#include <vector>

namespace toy3d::shader
{
    struct ShaderParametersGeneratedUnit
    {
        ShaderParametersCodegenResult header;
        std::vector<std::string> dependencies;
    };

    struct ShaderParametersWriteResult
    {
        std::vector<PhysicalPath> outputs;
        std::vector<PhysicalPath> changed_outputs;
        std::vector<PhysicalPath> removed_outputs;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    ShaderParametersWriteResult write_shader_parameter_headers(PlatformFile& platform_file,
                                                               const PhysicalPath& output_directory,
                                                               const std::vector<ShaderParametersGeneratedUnit>& units);
} // namespace toy3d::shader

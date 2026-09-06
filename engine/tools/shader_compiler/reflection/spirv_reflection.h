#pragma once

#include "compiler/compile_request.h"
#include "format/shader_format_types.h"

#include <optional>
#include <vector>

namespace toy3d::shader
{
    struct SpirvReflectionResult
    {
        // optional publishes reflection only when the complete SPIR-V interface
        // is valid; diagnostics remain available independently.
        std::optional<ShaderStageReflection> reflection;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    SpirvReflectionResult reflect_and_validate_spirv(const std::vector<std::uint8_t>& binary,
                                                     const ShaderCompileRequest& request,
                                                     const TargetBindingLayout& expected_layout,
                                                     bool require_all_expected_bindings = true);

} // namespace toy3d::shader

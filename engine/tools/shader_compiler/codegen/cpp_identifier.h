#pragma once

#include "frontend/diagnostic.h"
#include "frontend/shader_ast.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace toy3d::shader
{
    struct CppShaderParameterFieldIdentifier
    {
        BindingGroup group = BindingGroup::Pass;
        std::string source_name;
        std::string field_name;
    };

    struct CppShaderPassIdentifiers
    {
        std::string source_name;
        std::string parameters_type;
        std::string metadata_accessor;
        std::string encode_function;
        std::vector<CppShaderParameterFieldIdentifier> fields;
    };

    struct CppShaderParameterIdentifiers
    {
        std::uint32_t version = shader_parameters_cpp_identifier_version;
        std::string header_stem;
        std::string shader_type_stem;
        std::vector<CppShaderPassIdentifiers> passes;
    };

    struct CppIdentifierMappingResult
    {
        // optional prevents partially valid generated names from reaching codegen
        // after any source name fails C++ normalization or collision validation.
        std::optional<CppShaderParameterIdentifiers> identifiers;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    CppIdentifierMappingResult map_shader_parameter_cpp_identifiers(const ShaderAsset& asset);
} // namespace toy3d::shader

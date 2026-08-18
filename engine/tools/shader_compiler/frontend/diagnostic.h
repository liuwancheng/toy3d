#pragma once

#include "frontend/source_location.h"

#include <string>

namespace toy3d::shader
{
    enum class DiagnosticSeverity
    {
        Error,
        Warning
    };

    enum class DiagnosticCode
    {
        UnexpectedCharacter,
        UnexpectedToken,
        UnterminatedString,
        UnterminatedComment,
        UnterminatedHlslBlock,
        InvalidVersion,
        DuplicateSection,
        DuplicateProperty,
        DuplicateResource,
        DuplicateVariant,
        DuplicatePass,
        InvalidPassName,
        InvalidPropertyType,
        InvalidShaderName,
        ReservedIdentifier,
        IdentifierConflict,
        InvalidDefaultValue,
        InvalidResourceGroup,
        InvalidResourceType,
        InvalidVariant,
        InvalidPassState,
        DuplicatePassState,
        UnknownPragma,
        DuplicateEntryPoint,
        MissingEntryPoint,
        MixedProgramStages,
        ShaderParameterIdCollision,
        ConstantBufferSizeLimitExceeded,
        UnknownParameterUsage,
        BindingLimitExceeded,
        InvalidIncludePath,
        IncludeNotFound,
        IncludeCycle,
        IncludeDepthExceeded,
        CompilerUnavailable,
        InvalidCompileRequest,
        InvalidToolchainManifest,
        ToolchainHashMismatch,
        ShaderCompilationFailed,
        ShaderValidationFailed,
        ReflectionFailed,
        ReflectionUnexpectedResource,
        ReflectionMismatch,
        ShaderInterfacePrecisionMismatch,
        ShaderCodeWriteFailed,
        InvalidVariantSelection,
        VariantIdCollision,
        ShaderMapReadFailed,
        ShaderMapCacheConflict
    };

    struct Diagnostic
    {
        DiagnosticSeverity severity = DiagnosticSeverity::Error;
        DiagnosticCode code = DiagnosticCode::UnexpectedToken;
        SourceLocation location;
        std::string message;
    };

    const char* diagnostic_severity_name(DiagnosticSeverity severity);
    const char* diagnostic_code_name(DiagnosticCode code);
    std::string format_diagnostic(const Diagnostic& diagnostic);
}

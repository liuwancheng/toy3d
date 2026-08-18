#include "frontend/diagnostic.h"

#include <sstream>

namespace toy3d::shader
{
    const char* diagnostic_severity_name(DiagnosticSeverity severity)
    {
        switch (severity)
        {
            case DiagnosticSeverity::Error: return "error";
            case DiagnosticSeverity::Warning: return "warning";
        }
        return "unknown";
    }

    const char* diagnostic_code_name(DiagnosticCode code)
    {
        switch (code)
        {
            case DiagnosticCode::UnexpectedCharacter: return "UnexpectedCharacter";
            case DiagnosticCode::UnexpectedToken: return "UnexpectedToken";
            case DiagnosticCode::UnterminatedString: return "UnterminatedString";
            case DiagnosticCode::UnterminatedComment: return "UnterminatedComment";
            case DiagnosticCode::UnterminatedHlslBlock: return "UnterminatedHlslBlock";
            case DiagnosticCode::InvalidVersion: return "InvalidVersion";
            case DiagnosticCode::DuplicateSection: return "DuplicateSection";
            case DiagnosticCode::DuplicateProperty: return "DuplicateProperty";
            case DiagnosticCode::DuplicateResource: return "DuplicateResource";
            case DiagnosticCode::DuplicateVariant: return "DuplicateVariant";
            case DiagnosticCode::DuplicatePass: return "DuplicatePass";
            case DiagnosticCode::InvalidPassName: return "InvalidPassName";
            case DiagnosticCode::InvalidPropertyType: return "InvalidPropertyType";
            case DiagnosticCode::InvalidShaderName: return "InvalidShaderName";
            case DiagnosticCode::ReservedIdentifier: return "ReservedIdentifier";
            case DiagnosticCode::IdentifierConflict: return "IdentifierConflict";
            case DiagnosticCode::InvalidDefaultValue: return "InvalidDefaultValue";
            case DiagnosticCode::InvalidResourceGroup: return "InvalidResourceGroup";
            case DiagnosticCode::InvalidResourceType: return "InvalidResourceType";
            case DiagnosticCode::InvalidVariant: return "InvalidVariant";
            case DiagnosticCode::InvalidVariantSelection: return "InvalidVariantSelection";
            case DiagnosticCode::VariantIdCollision: return "VariantIdCollision";
            case DiagnosticCode::ShaderMapReadFailed: return "ShaderMapReadFailed";
            case DiagnosticCode::ShaderMapCacheConflict: return "ShaderMapCacheConflict";
            case DiagnosticCode::InvalidPassState: return "InvalidPassState";
            case DiagnosticCode::DuplicatePassState: return "DuplicatePassState";
            case DiagnosticCode::UnknownPragma: return "UnknownPragma";
            case DiagnosticCode::DuplicateEntryPoint: return "DuplicateEntryPoint";
            case DiagnosticCode::MissingEntryPoint: return "MissingEntryPoint";
            case DiagnosticCode::MixedProgramStages: return "MixedProgramStages";
            case DiagnosticCode::ShaderParameterIdCollision: return "ShaderParameterIdCollision";
            case DiagnosticCode::ConstantBufferSizeLimitExceeded: return "ConstantBufferSizeLimitExceeded";
            case DiagnosticCode::UnknownParameterUsage: return "UnknownParameterUsage";
            case DiagnosticCode::BindingLimitExceeded: return "BindingLimitExceeded";
            case DiagnosticCode::InvalidIncludePath: return "InvalidIncludePath";
            case DiagnosticCode::IncludeNotFound: return "IncludeNotFound";
            case DiagnosticCode::IncludeCycle: return "IncludeCycle";
            case DiagnosticCode::IncludeDepthExceeded: return "IncludeDepthExceeded";
            case DiagnosticCode::CompilerUnavailable: return "CompilerUnavailable";
            case DiagnosticCode::InvalidCompileRequest: return "InvalidCompileRequest";
            case DiagnosticCode::InvalidToolchainManifest: return "InvalidToolchainManifest";
            case DiagnosticCode::ToolchainHashMismatch: return "ToolchainHashMismatch";
            case DiagnosticCode::ShaderCompilationFailed: return "ShaderCompilationFailed";
            case DiagnosticCode::ShaderValidationFailed: return "ShaderValidationFailed";
            case DiagnosticCode::ReflectionFailed: return "ReflectionFailed";
            case DiagnosticCode::ReflectionUnexpectedResource: return "ReflectionUnexpectedResource";
            case DiagnosticCode::ReflectionMismatch: return "ReflectionMismatch";
            case DiagnosticCode::ShaderInterfacePrecisionMismatch: return "ShaderInterfacePrecisionMismatch";
            case DiagnosticCode::ShaderCodeWriteFailed: return "ShaderCodeWriteFailed";
        }
        return "UnknownDiagnostic";
    }

    std::string format_diagnostic(const Diagnostic& diagnostic)
    {
        std::ostringstream output;
        if (!diagnostic.location.path.empty())
        {
            output << diagnostic.location.path << ':' << diagnostic.location.line << ':'
                   << diagnostic.location.column << ": ";
        }
        output << diagnostic_severity_name(diagnostic.severity) << " ["
               << diagnostic_code_name(diagnostic.code) << "]: " << diagnostic.message;
        return output.str();
    }
}

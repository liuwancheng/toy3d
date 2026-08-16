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
        InvalidPropertyType,
        InvalidDefaultValue,
        InvalidResourceGroup,
        InvalidVariant,
        InvalidPassState,
        UnknownPragma,
        DuplicateEntryPoint,
        MissingEntryPoint,
        MixedProgramStages
    };

    struct Diagnostic
    {
        DiagnosticSeverity severity = DiagnosticSeverity::Error;
        DiagnosticCode code = DiagnosticCode::UnexpectedToken;
        SourceLocation location;
        std::string message;
    };
}

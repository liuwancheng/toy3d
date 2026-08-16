#pragma once

#include "frontend/source_location.h"

#include <string>

namespace toy3d::shader
{
    enum class TokenKind
    {
        EndOfFile,
        Invalid,
        Identifier,
        StringLiteral,
        Number,
        LeftBrace,
        RightBrace,
        LeftParenthesis,
        RightParenthesis,
        LeftAngle,
        RightAngle,
        Colon,
        Comma,
        Equal,
        Minus,
        Plus
    };

    struct Token
    {
        TokenKind kind = TokenKind::Invalid;
        std::string text;
        SourceLocation location;
    };
}

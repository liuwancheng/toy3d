#pragma once

#include "frontend/diagnostic.h"
#include "frontend/token.h"

#include <string>
#include <string_view>
#include <vector>

namespace toy3d::shader
{
    struct RawBlock
    {
        std::string text;
        SourceLocation location;
        bool terminated = false;
    };

    class Tokenizer
    {
    public:
        // string_view scans caller-owned source without copying it; the caller
        // keeps that source alive for the tokenizer lifetime.
        Tokenizer(std::string_view source, std::string path);

        Token next();
        RawBlock consume_raw_block(std::string_view terminator);
        const std::vector<Diagnostic>& diagnostics() const;

    private:
        bool at_end() const;
        char current() const;
        char peek_character(std::size_t lookahead = 1) const;
        char advance();
        SourceLocation location() const;
        void skip_trivia();
        Token make_token(TokenKind kind, const SourceLocation& start) const;
        Token read_identifier(const SourceLocation& start);
        Token read_number(const SourceLocation& start);
        Token read_string(const SourceLocation& start);
        void add_error(DiagnosticCode code, const SourceLocation& at, std::string message);

        std::string_view source_text;
        std::string source_path;
        std::size_t position = 0;
        std::size_t line = 1;
        std::size_t column = 1;
        std::vector<Diagnostic> tokenizer_diagnostics;
    };
}

#include "frontend/tokenizer.h"

#include <cctype>
#include <utility>

namespace toy3d::shader
{
    namespace
    {
        bool is_identifier_start(char value)
        {
            const unsigned char character = static_cast<unsigned char>(value);
            return std::isalpha(character) != 0 || value == '_';
        }

        bool is_identifier_continue(char value)
        {
            const unsigned char character = static_cast<unsigned char>(value);
            return std::isalnum(character) != 0 || value == '_';
        }
    }

    Tokenizer::Tokenizer(std::string_view source, std::string path)
        : source_text(source)
        , source_path(std::move(path))
    {
    }

    Token Tokenizer::next()
    {
        skip_trivia();
        const SourceLocation start = location();
        if (at_end())
        {
            return Token{TokenKind::EndOfFile, {}, start};
        }

        const char character = current();
        if (is_identifier_start(character))
        {
            return read_identifier(start);
        }
        if (std::isdigit(static_cast<unsigned char>(character)) != 0 ||
            (character == '.' && std::isdigit(static_cast<unsigned char>(peek_character())) != 0))
        {
            return read_number(start);
        }
        if (character == '"')
        {
            return read_string(start);
        }

        advance();
        switch (character)
        {
        case '{': return Token{TokenKind::LeftBrace, "{", start};
        case '}': return Token{TokenKind::RightBrace, "}", start};
        case '(': return Token{TokenKind::LeftParenthesis, "(", start};
        case ')': return Token{TokenKind::RightParenthesis, ")", start};
        case '<': return Token{TokenKind::LeftAngle, "<", start};
        case '>': return Token{TokenKind::RightAngle, ">", start};
        case ':': return Token{TokenKind::Colon, ":", start};
        case ',': return Token{TokenKind::Comma, ",", start};
        case '=': return Token{TokenKind::Equal, "=", start};
        case '-': return Token{TokenKind::Minus, "-", start};
        case '+': return Token{TokenKind::Plus, "+", start};
        default:
            add_error(
                DiagnosticCode::UnexpectedCharacter,
                start,
                std::string("Unexpected character '") + character + "'.");
            return Token{TokenKind::Invalid, std::string(1, character), start};
        }
    }

    RawBlock Tokenizer::consume_raw_block(std::string_view terminator)
    {
        while (!at_end() && (current() == ' ' || current() == '\t' || current() == '\r'))
        {
            advance();
        }
        if (!at_end() && current() == '\n')
        {
            advance();
        }

        RawBlock result;
        result.location = location();
        const std::size_t content_start = position;

        while (!at_end())
        {
            const std::size_t line_start = position;
            std::size_t first_non_space = line_start;
            while (first_non_space < source_text.size() &&
                (source_text[first_non_space] == ' ' || source_text[first_non_space] == '\t'))
            {
                ++first_non_space;
            }

            const std::size_t remaining = source_text.size() - first_non_space;
            if (remaining >= terminator.size() &&
                source_text.substr(first_non_space, terminator.size()) == terminator)
            {
                std::size_t after = first_non_space + terminator.size();
                while (after < source_text.size() &&
                    (source_text[after] == ' ' || source_text[after] == '\t' || source_text[after] == '\r'))
                {
                    ++after;
                }
                if (after == source_text.size() || source_text[after] == '\n')
                {
                    result.text = std::string(source_text.substr(content_start, line_start - content_start));
                    while (position < after)
                    {
                        advance();
                    }
                    if (!at_end() && current() == '\n')
                    {
                        advance();
                    }
                    result.terminated = true;
                    return result;
                }
            }

            while (!at_end() && current() != '\n')
            {
                advance();
            }
            if (!at_end())
            {
                advance();
            }
        }

        result.text = std::string(source_text.substr(content_start));
        add_error(
            DiagnosticCode::UnterminatedHlslBlock,
            result.location,
            "HLSL block is missing an ENDHLSL terminator on its own line.");
        return result;
    }

    const std::vector<Diagnostic>& Tokenizer::diagnostics() const
    {
        return tokenizer_diagnostics;
    }

    bool Tokenizer::at_end() const
    {
        return position >= source_text.size();
    }

    char Tokenizer::current() const
    {
        return at_end() ? '\0' : source_text[position];
    }

    char Tokenizer::peek_character(std::size_t lookahead) const
    {
        const std::size_t peek_position = position + lookahead;
        return peek_position >= source_text.size() ? '\0' : source_text[peek_position];
    }

    char Tokenizer::advance()
    {
        const char value = current();
        if (at_end())
        {
            return value;
        }
        ++position;
        if (value == '\n')
        {
            ++line;
            column = 1;
        }
        else
        {
            ++column;
        }
        return value;
    }

    SourceLocation Tokenizer::location() const
    {
        return SourceLocation{source_path, position, line, column};
    }

    void Tokenizer::skip_trivia()
    {
        for (;;)
        {
            while (!at_end() && std::isspace(static_cast<unsigned char>(current())) != 0)
            {
                advance();
            }

            if (current() == '/' && peek_character() == '/')
            {
                while (!at_end() && current() != '\n')
                {
                    advance();
                }
                continue;
            }
            if (current() == '/' && peek_character() == '*')
            {
                const SourceLocation start = location();
                advance();
                advance();
                while (!at_end() && !(current() == '*' && peek_character() == '/'))
                {
                    advance();
                }
                if (at_end())
                {
                    add_error(DiagnosticCode::UnterminatedComment, start, "Block comment is not terminated.");
                    return;
                }
                advance();
                advance();
                continue;
            }
            return;
        }
    }

    Token Tokenizer::make_token(TokenKind kind, const SourceLocation& start) const
    {
        return Token{kind, std::string(source_text.substr(start.offset, position - start.offset)), start};
    }

    Token Tokenizer::read_identifier(const SourceLocation& start)
    {
        while (!at_end() && is_identifier_continue(current()))
        {
            advance();
        }
        return make_token(TokenKind::Identifier, start);
    }

    Token Tokenizer::read_number(const SourceLocation& start)
    {
        while (std::isdigit(static_cast<unsigned char>(current())) != 0)
        {
            advance();
        }
        if (current() == '.')
        {
            advance();
            while (std::isdigit(static_cast<unsigned char>(current())) != 0)
            {
                advance();
            }
        }
        if (current() == 'e' || current() == 'E')
        {
            advance();
            if (current() == '+' || current() == '-')
            {
                advance();
            }
            while (std::isdigit(static_cast<unsigned char>(current())) != 0)
            {
                advance();
            }
        }
        return make_token(TokenKind::Number, start);
    }

    Token Tokenizer::read_string(const SourceLocation& start)
    {
        advance();
        std::string value;
        while (!at_end() && current() != '"')
        {
            if (current() == '\n')
            {
                add_error(DiagnosticCode::UnterminatedString, start, "String literal cannot span source lines.");
                return Token{TokenKind::Invalid, std::move(value), start};
            }
            if (current() == '\\')
            {
                advance();
                if (at_end())
                {
                    break;
                }
                const char escaped = advance();
                value.push_back(escaped == 'n' ? '\n' : escaped == 't' ? '\t' : escaped);
                continue;
            }
            value.push_back(advance());
        }
        if (at_end())
        {
            add_error(DiagnosticCode::UnterminatedString, start, "String literal is not terminated.");
            return Token{TokenKind::Invalid, std::move(value), start};
        }
        advance();
        return Token{TokenKind::StringLiteral, std::move(value), start};
    }

    void Tokenizer::add_error(DiagnosticCode code, const SourceLocation& at, std::string message)
    {
        tokenizer_diagnostics.push_back(
            Diagnostic{DiagnosticSeverity::Error, code, at, std::move(message)});
    }
}

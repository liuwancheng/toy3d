#pragma once

#include "frontend/diagnostic.h"
#include "frontend/shader_ast.h"
#include "frontend/tokenizer.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toy3d::shader
{
    struct ParseResult
    {
        // optional publishes the AST only after parsing succeeds; string_view
        // parser inputs below observe source text without copying it.
        std::optional<ShaderAsset> asset;
        std::vector<Diagnostic> diagnostics;

        bool succeeded() const;
    };

    ParseResult parse_shader(std::string_view source, std::string path);

    class ShaderParser
    {
      public:
        ShaderParser(std::string_view source, std::string path);

        ParseResult parse();

      private:
        // Parser optionals distinguish a missing/invalid token or number from a
        // valid zero value and keep one-token lookahead explicitly nullable.
        const Token& peek();
        Token consume();
        bool check(TokenKind kind);
        bool check_identifier(std::string_view text);
        bool match(TokenKind kind);
        bool match_identifier(std::string_view text);
        std::optional<Token> expect(TokenKind kind, std::string message);
        std::optional<Token> expect_identifier(std::string message);
        bool parse_properties(ShaderAsset& asset);
        bool parse_property(ShaderAsset& asset);
        bool parse_parameters(ShaderAsset& asset);
        bool parse_parameter_group(ShaderAsset& asset);
        bool parse_resources(ShaderAsset& asset);
        bool parse_resource_group(ShaderAsset& asset);
        bool parse_resource_type(Resource& resource);
        bool parse_variants(ShaderAsset& asset);
        bool parse_vertex_factories(ShaderAsset& asset);
        bool parse_variant(ShaderAsset& asset);
        bool parse_pass(ShaderAsset& asset);
        bool parse_stencil_state(ShaderPass& pass);
        bool parse_stencil_face(ShaderGraphicsPassState::StencilFaceState& face);
        bool parse_blend_state(ShaderPass& pass);
        bool parse_hlsl_block(HlslBlock& block);
        bool parse_default_value(DefaultValue& value);
        std::optional<double> parse_number();
        void extract_pragmas(HlslBlock& block);
        void validate_program(ShaderPass& pass);
        void validate_identifier(ShaderAsset& asset, const Token& token, std::string_view category);
        void add_error(DiagnosticCode code, const SourceLocation& at, std::string message);
        void copy_tokenizer_diagnostics();

        Tokenizer tokenizer;
        std::optional<Token> lookahead;
        std::vector<Diagnostic> parser_diagnostics;
        std::size_t copied_tokenizer_diagnostics = 0;
    };
} // namespace toy3d::shader

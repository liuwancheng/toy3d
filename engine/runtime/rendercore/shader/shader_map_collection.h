#pragma once

#include "rendercore/shader/shader_map.h"

namespace toy3d
{
    struct ShaderMapCollectionResult
    {
        ShaderMapCollectionRef collection;
        std::string error;

        bool succeeded() const;
    };

    // An immutable source/configuration revision, shared by Material and
    // engine Pass owners. Programs are peers: no primary/skin attachment.
    class ShaderMapCollection final
    {
      public:
        ShaderMapCollection(const ShaderMapCollection&) = delete;
        ShaderMapCollection& operator=(const ShaderMapCollection&) = delete;
        ShaderMapCollection(ShaderMapCollection&&) noexcept = default;
        ShaderMapCollection& operator=(ShaderMapCollection&&) = delete;

        static ShaderMapCollectionResult create_candidate(ShaderMapCollectionLoadResult loaded);
        const shader::ShaderMapIndex& index() const;
        const std::vector<ShaderMapProgramRef>& programs() const;
        const shader::ShaderEngineFeatures& features() const;
        bool requires_tangent_frame() const;
        ShaderMapProgramResult find(shader::ShaderPassRole role, shader::VertexFactoryType factory,
                                    const std::string& global_pass_name = {},
                                    const std::vector<shader::ShaderPermutationSelection>& pass_selections = {}) const;

      private:
        ShaderMapCollection(shader::ShaderMapIndex index, std::vector<ShaderMapProgramRef> programs);
        shader::ShaderMapIndex index_;
        std::vector<ShaderMapProgramRef> programs_;
        shader::ShaderEngineFeatures features_;
        bool requires_tangent_frame_ = false;
    };
} // namespace toy3d

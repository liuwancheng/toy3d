#pragma once

#include "rendercore/shader/global_shader_type.h"

#include <string>
#include <vector>

namespace toy3d
{
    struct GlobalShaderTypeRegistryResult
    {
        std::vector<const GlobalShaderType*> types;
        std::string error;

        bool succeeded() const
        {
            return error.empty();
        }
    };

    // Global Shader types are process-wide immutable metadata. The registry
    // freezes before Shader loading and never owns ShaderMap or RHI objects.
    class GlobalShaderTypeRegistry final
    {
      public:
        GlobalShaderTypeRegistry() = default;

        static GlobalShaderTypeRegistry& get();

        GlobalShaderTypeRegistryResult freeze();

      private:
        friend class GlobalShaderTypeRegistration;

        void register_type(const GlobalShaderType& type);

        std::vector<const GlobalShaderType*> types_;
        std::string registration_error_;
        bool frozen_ = false;
    };

    class GlobalShaderTypeRegistration final
    {
      public:
        explicit GlobalShaderTypeRegistration(const GlobalShaderType& type);
        GlobalShaderTypeRegistration(GlobalShaderTypeRegistry& registry, const GlobalShaderType& type);
    };

    class GlobalShaderRequirements final
    {
      public:
        explicit GlobalShaderRequirements(const std::vector<const GlobalShaderType*>& registered_types)
            : registered_types_(registered_types)
        {
        }

        bool add(const GlobalShaderType& type, std::string& error);
        const std::vector<const GlobalShaderType*>& types() const
        {
            return required_types_;
        }

      private:
        std::vector<const GlobalShaderType*> registered_types_;
        std::vector<const GlobalShaderType*> required_types_;
    };
} // namespace toy3d

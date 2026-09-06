#include "rendercore/shader/global_shader_type_registry.h"

#include <algorithm>
#include <utility>

namespace toy3d
{
    GlobalShaderTypeRegistry& GlobalShaderTypeRegistry::get()
    {
        static GlobalShaderTypeRegistry registry;
        return registry;
    }

    void GlobalShaderTypeRegistry::register_type(const GlobalShaderType& type)
    {
        if (frozen_)
        {
            registration_error_ = "Global Shader type '" + type.type_name() + "' registered after registry freeze.";
            return;
        }
        types_.push_back(&type);
    }

    GlobalShaderTypeRegistryResult GlobalShaderTypeRegistry::freeze()
    {
        frozen_ = true;
        if (!registration_error_.empty())
        {
            return {{}, registration_error_};
        }

        std::vector<const GlobalShaderType*> result = types_;
        std::sort(result.begin(), result.end(), [](const GlobalShaderType* left, const GlobalShaderType* right) {
            return left->type_name() < right->type_name();
        });
        for (std::size_t index = 1; index < result.size(); ++index)
        {
            if (result[index - 1]->type_name() == result[index]->type_name())
            {
                return {{}, "Duplicate registered Global Shader type name '" + result[index]->type_name() + "'."};
            }
        }
        return {std::move(result), {}};
    }

    GlobalShaderTypeRegistration::GlobalShaderTypeRegistration(const GlobalShaderType& type)
        : GlobalShaderTypeRegistration(GlobalShaderTypeRegistry::get(), type)
    {
    }

    GlobalShaderTypeRegistration::GlobalShaderTypeRegistration(GlobalShaderTypeRegistry& registry,
                                                               const GlobalShaderType& type)
    {
        registry.register_type(type);
    }

    bool GlobalShaderRequirements::add(const GlobalShaderType& type, std::string& error)
    {
        error.clear();
        const GlobalShaderType* registered_type = nullptr;
        for (const GlobalShaderType* candidate : registered_types_)
        {
            if (candidate->type_name() == type.type_name())
            {
                registered_type = candidate;
                break;
            }
        }
        if (registered_type == nullptr)
        {
            error = "Required Global Shader type '" + type.type_name() + "' is not registered.";
            return false;
        }
        if (!(*registered_type == type))
        {
            error = "Required Global Shader type '" + type.type_name() + "' does not match its registration.";
            return false;
        }

        for (const GlobalShaderType* required_type : required_types_)
        {
            if (required_type->type_name() == type.type_name())
            {
                return true;
            }
        }
        required_types_.push_back(registered_type);
        return true;
    }
} // namespace toy3d

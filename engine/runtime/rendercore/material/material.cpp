#include "rendercore/material/material.h"

#include "logging/logger.h"
#include "rendercore/render_command.h"
#include "rendercore/shader/shader_map.h"
#include "renderscene/material/material_render_proxy.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace toy3d
{
    namespace
    {
        bool is_program_compatible_with_material(
            const MaterialDesc& desc,
            const ShaderMapProgram& program,
            std::string& error)
        {
            error.clear();
            if (program.data().shader_name != desc.shader_name)
            {
                error = "ShaderMap Program identity does not match the Material shader";
                return false;
            }

            for (const ShaderMapBinding& binding : program.data().bindings)
            {
                if (binding.group != RHIBindingGroup::Material)
                {
                    continue;
                }
                if (binding.type == RHIResourceBindingType::UniformBuffer)
                {
                    for (const ShaderMapBinding::ConstantMember& member :
                         binding.constant_members)
                    {
                        bool has_default = false;
                        switch (member.type)
                        {
                        case ShaderValueType::Float32:
                            has_default = desc.scalar_defaults.count(member.parameter_id) == 1;
                            break;
                        case ShaderValueType::Float32x2:
                            has_default = desc.vector2_defaults.count(member.parameter_id) == 1;
                            break;
                        case ShaderValueType::Float32x3:
                            has_default = desc.vector3_defaults.count(member.parameter_id) == 1;
                            break;
                        case ShaderValueType::Float32x4:
                            has_default = desc.vector4_defaults.count(member.parameter_id) == 1;
                            break;
                        default:
                            error = "Material defaults do not support a required Shader value type";
                            return false;
                        }
                        if (!has_default)
                        {
                            error = "Material is missing a required Shader constant default";
                            return false;
                        }
                    }
                    continue;
                }
                if (binding.type == RHIResourceBindingType::SampledTexture &&
                    binding.array_count == 1)
                {
                    const auto found = desc.texture_defaults.find(binding.parameter_id);
                    if (found == desc.texture_defaults.end() || !found->second)
                    {
                        error = "Material is missing a required Texture default";
                        return false;
                    }
                    continue;
                }

                error = "Material Program requires an unsupported Material resource binding";
                return false;
            }
            return true;
        }

        const ShaderParameterBinding* find_material_parameter(
            const std::shared_ptr<const ShaderMapProgram>& program,
            ShaderParameterId parameter_id)
        {
            if (!program || parameter_id == 0)
            {
                return nullptr;
            }
            const ShaderParameterBinding* binding =
                program->find_parameter_binding(parameter_id);
            if (binding == nullptr)
            {
                return nullptr;
            }

            // C++17 get_if makes the closed constant/resource reflection choice
            // explicit without exception-based variant access.
            if (const auto* constant = std::get_if<ShaderConstantBinding>(binding))
            {
                return constant->group == RHIBindingGroup::Material
                    ? binding
                    : nullptr;
            }
            const auto* resource = std::get_if<ShaderResourceBinding>(binding);
            return resource != nullptr && resource->group == RHIBindingGroup::Material
                ? binding
                : nullptr;
        }
    }

    std::shared_ptr<const Material> Material::create(MaterialDesc desc)
    {
        if (desc.shader_name.empty())
        {
            TOY_LOG_ERROR("A Material must identify a ShaderMap shader.");
            return nullptr;
        }
        if (desc.shader_program)
        {
            std::string error;
            if (!is_program_compatible_with_material(
                    desc, *desc.shader_program, error))
            {
                TOY_LOG_ERROR("Invalid Material ShaderMap Program: {}.", error);
                return nullptr;
            }
        }
        Material material(std::move(desc));
        return std::make_shared<Material>(std::move(material));
    }

    Material::Material(MaterialDesc desc) : desc_(std::move(desc))
    {
    }

    std::shared_ptr<MaterialInstance> MaterialInstance::create(MaterialRef material)
    {
        if (material == nullptr)
        {
            TOY_LOG_ERROR("A MaterialInstance must reference a Material.");
            return nullptr;
        }
        MaterialInstance material_instance(std::move(material));
        return std::make_shared<MaterialInstance>(std::move(material_instance));
    }

    MaterialInstance::MaterialInstance(MaterialRef material)
        : material_(std::move(material)),
          shader_program_(material_->desc().shader_program),
          material_render_proxy_(std::make_unique<MaterialRenderProxy>(*material_))
    {
    }

    MaterialInstance::MaterialInstance(MaterialInstance&& other) noexcept
        : material_(std::move(other.material_)),
          shader_program_(std::move(other.shader_program_)),
          pending_shader_program_(std::move(other.pending_shader_program_)),
          scalar_overrides_(std::move(other.scalar_overrides_)),
          vector2_overrides_(std::move(other.vector2_overrides_)),
          vector3_overrides_(std::move(other.vector3_overrides_)),
          vector4_overrides_(std::move(other.vector4_overrides_)),
          texture_overrides_(std::move(other.texture_overrides_)),
          material_render_proxy_(std::move(other.material_render_proxy_)),
          render_proxy_used_(other.render_proxy_used_),
          release_enqueued_(other.release_enqueued_)
    {
        other.render_proxy_used_ = false;
        other.release_enqueued_ = false;
    }

    MaterialInstance::~MaterialInstance()
    {
        if (render_proxy_used_ && !release_enqueued_ && material_render_proxy_)
        {
            TOY_LOG_ERROR(
                "A used MaterialRenderProxy must be released through MaterialInstance::release().");
            std::terminate();
        }
    }

    void MaterialInstance::release(
        std::shared_ptr<MaterialInstance>& material_instance)
    {
        if (!material_instance)
        {
            return;
        }
        if (material_instance.use_count() != 1)
        {
            throw std::invalid_argument(
                "MaterialInstance final release requires the caller to hold the last reference");
        }
        if (!material_instance->render_proxy_used_)
        {
            material_instance.reset();
            return;
        }

        material_instance->release_enqueued_ = true;
        std::shared_ptr<MaterialInstance> release_owner = material_instance;
        try
        {
            enqueue_render_command(
                "ReleaseMaterialRenderProxy",
                [release_owner = std::move(release_owner)]() noexcept
                {
                    // The capture is intentionally destroyed after this body on
                    // the logical Rendering Thread.
                });
        }
        catch (...)
        {
            material_instance->release_enqueued_ = false;
            throw;
        }
        material_instance.reset();
    }

    bool MaterialInstance::validate_constant_parameter(
        ShaderParameterId parameter_id,
        ShaderValueType expected_value_type) const
    {
        const ShaderParameterBinding* binding =
            find_material_parameter(shader_program_, parameter_id);
        // C++17 get_if directly checks the constant branch and keeps a bad
        // resource/type request on the non-mutating diagnostic path.
        const auto* constant = binding != nullptr
            ? std::get_if<ShaderConstantBinding>(binding)
            : nullptr;
        if (constant == nullptr || constant->value_type != expected_value_type)
        {
            TOY_LOG_ERROR(
                "Material parameter {} is unknown or has an incompatible constant type.",
                parameter_id);
            return false;
        }
        return true;
    }

    bool MaterialInstance::validate_texture_parameter(
        ShaderParameterId parameter_id) const
    {
        const ShaderParameterBinding* binding =
            find_material_parameter(shader_program_, parameter_id);
        // C++17 get_if distinguishes resource reflection without introducing a
        // parallel runtime type tag in MaterialInstance.
        const auto* resource = binding != nullptr
            ? std::get_if<ShaderResourceBinding>(binding)
            : nullptr;
        if (resource == nullptr ||
            resource->resource_type != RHIResourceBindingType::SampledTexture ||
            resource->array_count != 1)
        {
            TOY_LOG_ERROR(
                "Material parameter {} is unknown or is not a scalar Texture binding.",
                parameter_id);
            return false;
        }
        return true;
    }

    bool MaterialInstance::set_scalar(
        ShaderParameterId parameter_id,
        float value)
    {
        if (!validate_constant_parameter(
                parameter_id, ShaderValueType::Float32))
        {
            return false;
        }
        scalar_overrides_[parameter_id] = value;
        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command(
            "SetMaterialScalar",
            [proxy, parameter_id, value]() noexcept
            {
                proxy->set_scalar(parameter_id, value);
            });
        return true;
    }

    bool MaterialInstance::set_vector(
        ShaderParameterId parameter_id,
        const vec2& value)
    {
        if (!validate_constant_parameter(
                parameter_id, ShaderValueType::Float32x2))
        {
            return false;
        }
        vector2_overrides_[parameter_id] = value;
        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command(
            "SetMaterialVector2",
            [proxy, parameter_id, value]() noexcept
            {
                proxy->set_vector(parameter_id, value);
            });
        return true;
    }

    bool MaterialInstance::set_vector(
        ShaderParameterId parameter_id,
        const vec3& value)
    {
        if (!validate_constant_parameter(
                parameter_id, ShaderValueType::Float32x3))
        {
            return false;
        }
        vector3_overrides_[parameter_id] = value;
        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command(
            "SetMaterialVector3",
            [proxy, parameter_id, value]() noexcept
            {
                proxy->set_vector(parameter_id, value);
            });
        return true;
    }

    bool MaterialInstance::set_vector(
        ShaderParameterId parameter_id,
        const vec4& value)
    {
        if (!validate_constant_parameter(
                parameter_id, ShaderValueType::Float32x4))
        {
            return false;
        }
        vector4_overrides_[parameter_id] = value;
        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command(
            "SetMaterialVector4",
            [proxy, parameter_id, value]() noexcept
            {
                proxy->set_vector(parameter_id, value);
            });
        return true;
    }

    bool MaterialInstance::set_texture(
        ShaderParameterId parameter_id,
        TextureRef texture)
    {
        if (!texture || !validate_texture_parameter(parameter_id))
        {
            if (!texture)
            {
                TOY_LOG_ERROR("Material Texture parameter {} requires a valid Texture.",
                    parameter_id);
            }
            return false;
        }

        TextureRef old_texture;
        const auto found = texture_overrides_.find(parameter_id);
        if (found != texture_overrides_.end())
        {
            old_texture = std::move(found->second);
            found->second = texture;
        }
        else
        {
            texture_overrides_.emplace(parameter_id, texture);
        }

        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        TextureResource* const resource = texture->texture_resource();
        enqueue_render_command(
            "SetMaterialTexture",
            [proxy, parameter_id, resource, texture = std::move(texture),
             old_texture = std::move(old_texture)]() mutable noexcept
            {
                proxy->set_texture(parameter_id, resource);
                if (old_texture && old_texture.use_count() == 1)
                {
                    Texture::release(old_texture);
                }
                old_texture.reset();
                texture.reset();
            });
        return true;
    }

    bool MaterialInstance::stage_shader_program_replacement(
        std::shared_ptr<const ShaderMapProgram> shader_program)
    {
        if (!shader_program || pending_shader_program_)
        {
            TOY_LOG_ERROR("Material ShaderMap replacement requires one complete candidate.");
            return false;
        }
        std::string error;
        if (!is_program_compatible_with_material(
                material_->desc(), *shader_program, error))
        {
            TOY_LOG_ERROR("Invalid Material ShaderMap candidate: {}.", error);
            return false;
        }

        pending_shader_program_ = shader_program;
        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command(
            "StageMaterialShaderProgram",
            [proxy, shader_program = std::move(shader_program)]() noexcept
            {
                const RHIStatus status = proxy->stage_shader_program(
                    std::move(shader_program));
                if (!status)
                {
                    TOY_LOG_ERROR("Material ShaderMap candidate staging failed: {}",
                        status.message());
                }
            });
        return true;
    }

    bool MaterialInstance::publish_shader_program_replacement()
    {
        if (!pending_shader_program_)
        {
            TOY_LOG_ERROR("Material has no ShaderMap candidate to publish.");
            return false;
        }
        shader_program_ = pending_shader_program_;
        pending_shader_program_.reset();
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command(
            "PublishMaterialShaderProgram",
            [proxy]() noexcept
            {
                const RHIStatus status = proxy->commit_shader_program();
                if (!status)
                {
                    TOY_LOG_ERROR("Material ShaderMap candidate commit failed: {}",
                        status.message());
                }
            });
        return true;
    }

    bool MaterialInstance::discard_shader_program_replacement()
    {
        if (!pending_shader_program_)
        {
            return false;
        }
        pending_shader_program_.reset();
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command(
            "DiscardMaterialShaderProgram",
            [proxy]() noexcept
            {
                proxy->discard_shader_program();
            });
        return true;
    }

    MaterialRenderProxy* MaterialInstance::material_render_proxy() noexcept
    {
        render_proxy_used_ = true;
        return material_render_proxy_.get();
    }
}

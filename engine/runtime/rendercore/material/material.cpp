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
          two_sided_(material_->desc().two_sided),
          material_render_proxy_(std::make_unique<MaterialRenderProxy>(*material_))
    {
    }

    MaterialInstance::MaterialInstance(MaterialInstance&& other) noexcept
        : material_(std::move(other.material_)),
          shader_program_(std::move(other.shader_program_)),
          pending_shader_program_(std::move(other.pending_shader_program_)),
          two_sided_(other.two_sided_),
          pending_two_sided_(other.pending_two_sided_),
          replacement_commit_complete_(
              std::move(other.replacement_commit_complete_)),
          replacement_commit_succeeded_(
              std::move(other.replacement_commit_succeeded_)),
          replacement_publication_pending_(
              other.replacement_publication_pending_),
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
        other.replacement_publication_pending_ = false;
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
        if (!resolve_material_replacement_publication() ||
            !validate_constant_parameter(
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
        if (!resolve_material_replacement_publication() ||
            !validate_constant_parameter(
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
        if (!resolve_material_replacement_publication() ||
            !validate_constant_parameter(
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
        if (!resolve_material_replacement_publication() ||
            !validate_constant_parameter(
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
        if (!resolve_material_replacement_publication())
        {
            return false;
        }
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

    bool MaterialInstance::stage_material_replacement(
        std::shared_ptr<const ShaderMapProgram> shader_program,
        bool two_sided)
    {
        if (!resolve_material_replacement_publication() || !shader_program ||
            pending_shader_program_)
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
        pending_two_sided_ = two_sided;
        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command(
            "StageMaterialCandidate",
            [proxy, shader_program = std::move(shader_program), two_sided]() noexcept
            {
                const RHIStatus status = proxy->stage_material_candidate(
                    std::move(shader_program), two_sided);
                if (!status)
                {
                    TOY_LOG_ERROR("Material candidate staging failed: {}",
                        status.message());
                }
            });
        return true;
    }

    bool MaterialInstance::publish_material_replacement()
    {
        if (!resolve_material_replacement_publication() ||
            !pending_shader_program_)
        {
            TOY_LOG_ERROR("Material has no ShaderMap candidate to publish.");
            return false;
        }
        replacement_commit_succeeded_->store(false, std::memory_order_relaxed);
        replacement_commit_complete_->store(false, std::memory_order_relaxed);
        replacement_publication_pending_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        const std::shared_ptr<std::atomic<bool>> commit_complete =
            replacement_commit_complete_;
        const std::shared_ptr<std::atomic<bool>> commit_succeeded =
            replacement_commit_succeeded_;
        try
        {
            enqueue_render_command(
                "PublishMaterialCandidate",
                [proxy, commit_complete, commit_succeeded]() noexcept
                {
                    const RHIStatus status =
                        proxy->commit_material_candidate();
                    if (!status)
                    {
                        TOY_LOG_ERROR("Material candidate commit failed: {}",
                            status.message());
                    }
                    commit_succeeded->store(
                        status.succeeded(), std::memory_order_relaxed);
                    // Release publishes both the RT commit result and the complete
                    // active-state mutation before GT resolves its Shader schema.
                    commit_complete->store(true, std::memory_order_release);
                });
        }
        catch (...)
        {
            replacement_publication_pending_ = false;
            throw;
        }
        return true;
    }

    bool MaterialInstance::discard_material_replacement()
    {
        if (!resolve_material_replacement_publication() ||
            !pending_shader_program_)
        {
            return false;
        }
        pending_shader_program_.reset();
        pending_two_sided_ = two_sided_;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command(
            "DiscardMaterialCandidate",
            [proxy]() noexcept
            {
                proxy->discard_material_candidate();
            });
        return true;
    }

    bool MaterialInstance::resolve_material_replacement_publication()
    {
        if (!replacement_publication_pending_)
        {
            return true;
        }
        // Acquire makes the RT commit and its result visible before GT changes
        // the parameter-validation schema associated with the active Program.
        if (!replacement_commit_complete_->load(std::memory_order_acquire))
        {
            TOY_LOG_ERROR(
                "Material replacement publication is still pending on the Rendering Thread.");
            return false;
        }

        const bool commit_succeeded =
            replacement_commit_succeeded_->load(std::memory_order_relaxed);
        if (commit_succeeded)
        {
            shader_program_ = pending_shader_program_;
            two_sided_ = pending_two_sided_;
        }
        pending_shader_program_.reset();
        pending_two_sided_ = two_sided_;
        replacement_publication_pending_ = false;
        return true;
    }

    MaterialRenderProxy* MaterialInstance::material_render_proxy() noexcept
    {
        render_proxy_used_ = true;
        return material_render_proxy_.get();
    }
}

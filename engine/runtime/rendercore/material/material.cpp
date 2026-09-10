#include "rendercore/material/material.h"

#include "logging/logger.h"
#include "rendercore/render_command.h"
#include "rendercore/shader/shader_map.h"
#include "renderscene/material/material_render_proxy.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <utility>

namespace toy3d
{
    shader::ShaderParameterSchema material_parameter_schema_from_shader_schema(
        const shader::ShaderParameterSchema& source)
    {
        shader::ShaderParameterSchema result;
        for (const shader::ShaderParameterConstantBufferSchema& buffer : source.constant_buffers)
        {
            if (buffer.group == shader::BindingGroup::Material)
            {
                result.constant_buffers.push_back(buffer);
            }
        }
        for (const shader::ShaderParameterResourceSchema& resource : source.resources)
        {
            if (resource.group == shader::BindingGroup::Material)
            {
                result.resources.push_back(resource);
            }
        }
        result.logical_layout_hash = shader::calculate_shader_parameter_logical_layout_hash(result);
        result.schema_identity = shader::calculate_shader_parameter_schema_identity(result);
        return result;
    }

    namespace
    {
        bool validate_material_schema_defaults(const MaterialDesc& desc, std::string& error)
        {
            std::size_t scalar_count = 0u;
            std::size_t vector2_count = 0u;
            std::size_t vector3_count = 0u;
            std::size_t vector4_count = 0u;
            for (const shader::ShaderParameterConstantBufferSchema& buffer : desc.parameter_schema.constant_buffers)
            {
                if (buffer.group != shader::BindingGroup::Material)
                {
                    error = "Material parameter schema contains a non-Material constant buffer";
                    return false;
                }
                for (const shader::ShaderParameterConstantMemberSchema& member : buffer.members)
                {
                    if (member.default_value.size() != member.size)
                    {
                        error = "Material constant schema is missing its canonical default value";
                        return false;
                    }
                    bool has_runtime_default = false;
                    switch (member.type)
                    {
                    case shader::ShaderValueType::Float32:
                        ++scalar_count;
                        has_runtime_default = desc.scalar_defaults.count(member.parameter_id) == 1u;
                        break;
                    case shader::ShaderValueType::Float32x2:
                        ++vector2_count;
                        has_runtime_default = desc.vector2_defaults.count(member.parameter_id) == 1u;
                        break;
                    case shader::ShaderValueType::Float32x3:
                        ++vector3_count;
                        has_runtime_default = desc.vector3_defaults.count(member.parameter_id) == 1u;
                        break;
                    case shader::ShaderValueType::Float32x4:
                        ++vector4_count;
                        has_runtime_default = desc.vector4_defaults.count(member.parameter_id) == 1u;
                        break;
                    default:
                        error = "Material schema contains an unsupported first-stage constant type";
                        return false;
                    }
                    if (!has_runtime_default)
                    {
                        error = "Material is missing a runtime value for a schema constant default";
                        return false;
                    }
                }
            }
            if (scalar_count != desc.scalar_defaults.size() || vector2_count != desc.vector2_defaults.size() ||
                vector3_count != desc.vector3_defaults.size() || vector4_count != desc.vector4_defaults.size())
            {
                error = "Material contains a constant default that is not declared by its complete schema";
                return false;
            }
            std::size_t texture_count = 0u;
            for (const shader::ShaderParameterResourceSchema& resource : desc.parameter_schema.resources)
            {
                if (resource.group != shader::BindingGroup::Material)
                {
                    error = "Material parameter schema contains a non-Material resource";
                    return false;
                }
                if (resource.category != shader::ShaderParameterCategory::SampledTexture ||
                    resource.resource_kind != shader::ResourceKind::Texture2D || resource.array_count != 1u)
                {
                    error = "Material schema contains an unsupported first-stage resource type";
                    return false;
                }
                if (resource.default_value_kind == shader::ShaderParameterDefaultValueKind::None ||
                    desc.texture_defaults.count(resource.parameter_id) != 1u ||
                    !desc.texture_defaults.at(resource.parameter_id))
                {
                    error = "Material Texture schema is missing its canonical or runtime default";
                    return false;
                }
                ++texture_count;
            }
            if (texture_count != desc.texture_defaults.size())
            {
                error = "Material contains a Texture default that is not declared by its complete schema";
                return false;
            }
            return true;
        }

        bool is_program_compatible_with_material(const MaterialDesc& desc, const ShaderMapProgram& program,
                                                 std::string& error)
        {
            error.clear();
            if (program.data().shader_name != desc.shader_name)
            {
                error = "ShaderMap Program identity does not match the Material shader";
                return false;
            }
            const shader::ShaderParameterSchema program_material_schema =
                material_parameter_schema_from_shader_schema(program.data().parameter_schema);
            if (program_material_schema.schema_identity != desc.parameter_schema.schema_identity)
            {
                error = "ShaderMap Program complete Material schema does not match the Material schema identity";
                return false;
            }
            return true;
        }

    } // namespace

    std::shared_ptr<const Material> Material::create(MaterialDesc desc)
    {
        if (desc.shader_name.empty())
        {
            TOY_LOG_ERROR("A Material must identify a ShaderMap shader.");
            return nullptr;
        }
        std::string schema_error;
        if (desc.parameter_schema.schema_identity == shader::Sha256Hash{} &&
            desc.parameter_schema.logical_layout_hash == shader::Sha256Hash{} &&
            desc.parameter_schema.constant_buffers.empty() && desc.parameter_schema.resources.empty())
        {
            desc.parameter_schema.logical_layout_hash =
                shader::calculate_shader_parameter_logical_layout_hash(desc.parameter_schema);
            desc.parameter_schema.schema_identity =
                shader::calculate_shader_parameter_schema_identity(desc.parameter_schema);
        }
        if (!shader::validate_shader_parameter_schema(desc.parameter_schema, schema_error) ||
            !validate_material_schema_defaults(desc, schema_error))
        {
            TOY_LOG_ERROR("Invalid Material parameter schema: {}.", schema_error);
            return nullptr;
        }
        if (desc.shader_program)
        {
            std::string error;
            if (!is_program_compatible_with_material(desc, *desc.shader_program, error))
            {
                TOY_LOG_ERROR("Invalid Material ShaderMap Program: {}.", error);
                return nullptr;
            }
        }
        Material material(std::move(desc));
        return std::make_shared<Material>(std::move(material));
    }

    Material::Material(MaterialDesc desc) : desc_(std::move(desc)) {}

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
        : material_(std::move(material)), shader_program_(material_->desc().shader_program),
          two_sided_(material_->desc().two_sided),
          material_render_proxy_(std::make_unique<MaterialRenderProxy>(*material_))
    {
    }

    MaterialInstance::MaterialInstance(MaterialInstance&& other) noexcept
        : material_(std::move(other.material_)), shader_program_(std::move(other.shader_program_)),
          pending_shader_program_(std::move(other.pending_shader_program_)), two_sided_(other.two_sided_),
          pending_two_sided_(other.pending_two_sided_),
          replacement_commit_complete_(std::move(other.replacement_commit_complete_)),
          replacement_commit_succeeded_(std::move(other.replacement_commit_succeeded_)),
          replacement_publication_pending_(other.replacement_publication_pending_),
          scalar_overrides_(std::move(other.scalar_overrides_)),
          vector2_overrides_(std::move(other.vector2_overrides_)),
          vector3_overrides_(std::move(other.vector3_overrides_)),
          vector4_overrides_(std::move(other.vector4_overrides_)),
          texture_overrides_(std::move(other.texture_overrides_)),
          material_render_proxy_(std::move(other.material_render_proxy_)), render_proxy_used_(other.render_proxy_used_),
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
            TOY_LOG_ERROR("A used MaterialRenderProxy must be released through MaterialInstance::release().");
            std::terminate();
        }
    }

    void MaterialInstance::release(std::shared_ptr<MaterialInstance>& material_instance)
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
            enqueue_render_command("ReleaseMaterialRenderProxy",
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

    bool MaterialInstance::resolve_constant_parameter(std::string_view parameter_name,
                                                      shader::ShaderValueType expected_value_type,
                                                      ShaderParameterId& parameter_id) const
    {
        parameter_id = 0;
        for (const shader::ShaderParameterConstantBufferSchema& buffer : material_->parameter_schema().constant_buffers)
        {
            for (const shader::ShaderParameterConstantMemberSchema& member : buffer.members)
            {
                if (member.name == parameter_name)
                {
                    if (member.type != expected_value_type)
                    {
                        TOY_LOG_ERROR("Material parameter '{}' has an incompatible constant type.", parameter_name);
                        return false;
                    }
                    parameter_id = member.parameter_id;
                    return true;
                }
            }
        }
        TOY_LOG_ERROR("Material constant parameter '{}' is unknown.", parameter_name);
        return false;
    }

    bool MaterialInstance::resolve_texture_parameter(std::string_view parameter_name,
                                                     ShaderParameterId& parameter_id) const
    {
        parameter_id = 0;
        for (const shader::ShaderParameterResourceSchema& resource : material_->parameter_schema().resources)
        {
            if (resource.name == parameter_name)
            {
                if (resource.category != shader::ShaderParameterCategory::SampledTexture ||
                    resource.resource_kind != shader::ResourceKind::Texture2D || resource.array_count != 1u)
                {
                    TOY_LOG_ERROR("Material parameter '{}' is not a scalar Texture2D binding.", parameter_name);
                    return false;
                }
                parameter_id = resource.parameter_id;
                return true;
            }
        }
        TOY_LOG_ERROR("Material Texture parameter '{}' is unknown.", parameter_name);
        return false;
    }

    bool MaterialInstance::set_scalar(std::string_view parameter_name, float value)
    {
        ShaderParameterId parameter_id = 0;
        if (!resolve_material_replacement_publication() ||
            !resolve_constant_parameter(parameter_name, shader::ShaderValueType::Float32, parameter_id))
        {
            return false;
        }
        scalar_overrides_[parameter_id] = value;
        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command("SetMaterialScalar",
                               [proxy, parameter_id, value]() noexcept {
                                   proxy->apply_scalar_update(parameter_id, value);
                               });
        return true;
    }

    bool MaterialInstance::set_vector(std::string_view parameter_name, const vec2& value)
    {
        ShaderParameterId parameter_id = 0;
        if (!resolve_material_replacement_publication() ||
            !resolve_constant_parameter(parameter_name, shader::ShaderValueType::Float32x2, parameter_id))
        {
            return false;
        }
        vector2_overrides_[parameter_id] = value;
        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command("SetMaterialVector2",
                               [proxy, parameter_id, value]() noexcept {
                                   proxy->apply_vector_update(parameter_id, value);
                               });
        return true;
    }

    bool MaterialInstance::set_vector(std::string_view parameter_name, const vec3& value)
    {
        ShaderParameterId parameter_id = 0;
        if (!resolve_material_replacement_publication() ||
            !resolve_constant_parameter(parameter_name, shader::ShaderValueType::Float32x3, parameter_id))
        {
            return false;
        }
        vector3_overrides_[parameter_id] = value;
        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command("SetMaterialVector3",
                               [proxy, parameter_id, value]() noexcept {
                                   proxy->apply_vector_update(parameter_id, value);
                               });
        return true;
    }

    bool MaterialInstance::set_vector(std::string_view parameter_name, const vec4& value)
    {
        ShaderParameterId parameter_id = 0;
        if (!resolve_material_replacement_publication() ||
            !resolve_constant_parameter(parameter_name, shader::ShaderValueType::Float32x4, parameter_id))
        {
            return false;
        }
        vector4_overrides_[parameter_id] = value;
        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command("SetMaterialVector4",
                               [proxy, parameter_id, value]() noexcept {
                                   proxy->apply_vector_update(parameter_id, value);
                               });
        return true;
    }

    bool MaterialInstance::set_texture(std::string_view parameter_name, TextureRef texture)
    {
        if (!resolve_material_replacement_publication())
        {
            return false;
        }
        ShaderParameterId parameter_id = 0;
        if (!texture || !resolve_texture_parameter(parameter_name, parameter_id))
        {
            if (!texture)
            {
                TOY_LOG_ERROR("Material Texture parameter '{}' requires a valid Texture.", parameter_name);
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
        enqueue_render_command("SetMaterialTexture",
                               [proxy, parameter_id, resource, texture = std::move(texture),
                                old_texture = std::move(old_texture)]() mutable noexcept
                               {
                                   proxy->apply_texture_update(parameter_id, resource);
                                   if (old_texture && old_texture.use_count() == 1)
                                   {
                                       Texture::release(old_texture);
                                   }
                                   old_texture.reset();
                                   texture.reset();
                               });
        return true;
    }

    bool MaterialInstance::stage_material_replacement(std::shared_ptr<const ShaderMapProgram> shader_program,
                                                      bool two_sided)
    {
        if (!resolve_material_replacement_publication() || !shader_program || pending_shader_program_)
        {
            TOY_LOG_ERROR("Material ShaderMap replacement requires one complete candidate.");
            return false;
        }
        std::string error;
        if (!is_program_compatible_with_material(material_->desc(), *shader_program, error))
        {
            TOY_LOG_ERROR("Invalid Material ShaderMap candidate: {}.", error);
            return false;
        }

        pending_shader_program_ = shader_program;
        pending_two_sided_ = two_sided;
        render_proxy_used_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command("StageMaterialCandidate",
                               [proxy, shader_program = std::move(shader_program), two_sided]() noexcept
                               {
                                   const RHIStatus status =
                                       proxy->stage_material_candidate(std::move(shader_program), two_sided);
                                   if (!status)
                                   {
                                       TOY_LOG_ERROR("Material candidate staging failed: {}", status.message());
                                   }
                               });
        return true;
    }

    bool MaterialInstance::publish_material_replacement()
    {
        if (!resolve_material_replacement_publication() || !pending_shader_program_)
        {
            TOY_LOG_ERROR("Material has no ShaderMap candidate to publish.");
            return false;
        }
        replacement_commit_succeeded_->store(false, std::memory_order_relaxed);
        replacement_commit_complete_->store(false, std::memory_order_relaxed);
        replacement_publication_pending_ = true;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        const std::shared_ptr<std::atomic<bool>> commit_complete = replacement_commit_complete_;
        const std::shared_ptr<std::atomic<bool>> commit_succeeded = replacement_commit_succeeded_;
        try
        {
            enqueue_render_command("PublishMaterialCandidate",
                                   [proxy, commit_complete, commit_succeeded]() noexcept
                                   {
                                       const RHIStatus status = proxy->commit_material_candidate();
                                       if (!status)
                                       {
                                           TOY_LOG_ERROR("Material candidate commit failed: {}", status.message());
                                       }
                                       commit_succeeded->store(status.succeeded(), std::memory_order_relaxed);
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
        if (!resolve_material_replacement_publication() || !pending_shader_program_)
        {
            return false;
        }
        pending_shader_program_.reset();
        pending_two_sided_ = two_sided_;
        MaterialRenderProxy* const proxy = material_render_proxy_.get();
        enqueue_render_command("DiscardMaterialCandidate", [proxy]() noexcept { proxy->discard_material_candidate(); });
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
            TOY_LOG_ERROR("Material replacement publication is still pending on the Rendering Thread.");
            return false;
        }

        const bool commit_succeeded = replacement_commit_succeeded_->load(std::memory_order_relaxed);
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
} // namespace toy3d

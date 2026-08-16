#include "codegen/binding_codegen.h"

#include <sstream>
#include <type_traits>

namespace toy3d::shader
{
    namespace
    {
        const char* value_type_name(ShaderValueType type)
        {
            switch (type)
            {
            case ShaderValueType::Float32: return "float";
            case ShaderValueType::Float32x2: return "float2";
            case ShaderValueType::Float32x3: return "float3";
            case ShaderValueType::Float32x4: return "float4";
            case ShaderValueType::Int32: return "int";
            case ShaderValueType::Int32x2: return "int2";
            case ShaderValueType::Int32x3: return "int3";
            case ShaderValueType::Int32x4: return "int4";
            case ShaderValueType::UInt32: return "uint";
            case ShaderValueType::UInt32x2: return "uint2";
            case ShaderValueType::UInt32x3: return "uint3";
            case ShaderValueType::UInt32x4: return "uint4";
            case ShaderValueType::Float32x2x2: return "float2x2";
            case ShaderValueType::Float32x2x3: return "float2x3";
            case ShaderValueType::Float32x2x4: return "float2x4";
            case ShaderValueType::Float32x3x2: return "float3x2";
            case ShaderValueType::Float32x3x3: return "float3x3";
            case ShaderValueType::Float32x3x4: return "float3x4";
            case ShaderValueType::Float32x4x2: return "float4x2";
            case ShaderValueType::Float32x4x3: return "float4x3";
            case ShaderValueType::Float32x4x4: return "float4x4";
            }
            return "float";
        }

        bool is_matrix(ShaderValueType type)
        {
            switch (type)
            {
            case ShaderValueType::Float32x2x2:
            case ShaderValueType::Float32x2x3:
            case ShaderValueType::Float32x2x4:
            case ShaderValueType::Float32x3x2:
            case ShaderValueType::Float32x3x3:
            case ShaderValueType::Float32x3x4:
            case ShaderValueType::Float32x4x2:
            case ShaderValueType::Float32x4x3:
            case ShaderValueType::Float32x4x4: return true;
            default: return false;
            }
        }

        const char* element_type_name(ResourceElementType type)
        {
            switch (type)
            {
            case ResourceElementType::Float: return "float";
            case ResourceElementType::Float2: return "float2";
            case ResourceElementType::Float3: return "float3";
            case ResourceElementType::Float4: return "float4";
            case ResourceElementType::Int: return "int";
            case ResourceElementType::Int2: return "int2";
            case ResourceElementType::Int3: return "int3";
            case ResourceElementType::Int4: return "int4";
            case ResourceElementType::UInt: return "uint";
            case ResourceElementType::UInt2: return "uint2";
            case ResourceElementType::UInt3: return "uint3";
            case ResourceElementType::UInt4: return "uint4";
            case ResourceElementType::Float2x2: return "float2x2";
            case ResourceElementType::Float2x3: return "float2x3";
            case ResourceElementType::Float2x4: return "float2x4";
            case ResourceElementType::Float3x2: return "float3x2";
            case ResourceElementType::Float3x3: return "float3x3";
            case ResourceElementType::Float3x4: return "float3x4";
            case ResourceElementType::Float4x2: return "float4x2";
            case ResourceElementType::Float4x3: return "float4x3";
            case ResourceElementType::Float4x4: return "float4x4";
            case ResourceElementType::None: return "void";
            }
            return "void";
        }

        char register_prefix(NativeRegisterClass value)
        {
            switch (value)
            {
            case NativeRegisterClass::ConstantBuffer: return 'b';
            case NativeRegisterClass::ShaderResource: return 't';
            case NativeRegisterClass::Sampler: return 's';
            case NativeRegisterClass::UnorderedAccess: return 'u';
            }
            return 't';
        }

        std::string packoffset(std::uint32_t offset)
        {
            static constexpr char components[] = {'x', 'y', 'z', 'w'};
            const std::uint32_t register_index = offset / 16u;
            const std::uint32_t component = (offset % 16u) / 4u;
            std::string result = "c" + std::to_string(register_index);
            if (component != 0u)
            {
                result += '.';
                result += components[component];
            }
            return result;
        }

        std::string resource_declaration(const ShaderResourceParameter& resource)
        {
            const std::string element = element_type_name(resource.element_type);
            switch (resource.resource_kind)
            {
            case ResourceKind::Texture2D: return "Texture2D<" + element + ">";
            case ResourceKind::Texture2DArray: return "Texture2DArray<" + element + ">";
            case ResourceKind::Texture3D: return "Texture3D<" + element + ">";
            case ResourceKind::TextureCube: return "TextureCube<" + element + ">";
            case ResourceKind::Texture2DMS: return "Texture2DMS<" + element + ">";
            case ResourceKind::Sampler: return "SamplerState";
            case ResourceKind::ComparisonSampler: return "SamplerComparisonState";
            case ResourceKind::Buffer: return "Buffer<" + element + ">";
            case ResourceKind::ByteAddressBuffer: return "ByteAddressBuffer";
            case ResourceKind::StructuredBuffer: return "StructuredBuffer<" + element + ">";
            case ResourceKind::RWBuffer: return "RWBuffer<" + element + ">";
            case ResourceKind::RWByteAddressBuffer: return "RWByteAddressBuffer";
            case ResourceKind::RWStructuredBuffer: return "RWStructuredBuffer<" + element + ">";
            case ResourceKind::RWTexture2D: return "RWTexture2D<" + element + ">";
            case ResourceKind::RWTexture2DArray: return "RWTexture2DArray<" + element + ">";
            case ResourceKind::RWTexture3D: return "RWTexture3D<" + element + ">";
            }
            return {};
        }

        template<typename T>
        void append_integer(std::vector<std::uint8_t>& bytes, T value)
        {
            using Unsigned = std::make_unsigned_t<T>;
            const Unsigned converted = static_cast<Unsigned>(value);
            for (std::size_t index = 0; index < sizeof(T); ++index)
            {
                bytes.push_back(static_cast<std::uint8_t>(converted >> (index * 8u)));
            }
        }
    }

    bool BindingCodegenResult::succeeded() const
    {
        return source.has_value() && diagnostics.empty();
    }

    BindingCodegenResult generate_binding_hlsl(
        const LogicalShaderLayout& logical_layout,
        const TargetBindingLayout& target_layout,
        ShaderStageFlags stage)
    {
        BindingCodegenResult result;
        if (stage == ShaderStageFlags::None ||
            (static_cast<std::uint8_t>(stage) & (static_cast<std::uint8_t>(stage) - 1u)) != 0u)
        {
            result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::UnexpectedToken, {}, "Binding HLSL generation requires exactly one Shader stage."});
            return result;
        }
        std::ostringstream source;
        source << "#ifndef TOY_BINDINGS_GENERATED\n#define TOY_BINDINGS_GENERATED\n";
        source << "#line 1 \"/Generated/ToyBindings.hlsli\"\n\n";
        for (const NativeBinding& binding : target_layout.bindings)
        {
            if (!has_stage(binding.stages, stage)) continue;
            if (target_layout.target != ShaderTarget::VulkanSpirV && binding.stages != stage) continue;
            if (!binding.logical_binding)
            {
                result.diagnostics.push_back({DiagnosticSeverity::Error, DiagnosticCode::UnexpectedToken, {}, "Native binding is missing its logical binding."});
                continue;
            }
            if (target_layout.target == ShaderTarget::VulkanSpirV)
            {
                source << "[[vk::binding(" << binding.descriptor_binding << ", " << binding.descriptor_set << ")]]\n";
            }
            const char register_name = register_prefix(binding.register_class);
            if (binding.logical_binding->constant_buffer)
            {
                const ConstantBufferLayout& buffer = *binding.logical_binding->constant_buffer;
                source << "cbuffer " << binding.name << " : register(" << register_name << binding.register_index << ")\n{\n";
                for (const ShaderConstantMember& member : buffer.members)
                {
                    source << "    ";
                    if (is_matrix(member.type)) source << "column_major ";
                    source << value_type_name(member.type) << ' ' << member.name;
                    if (member.array_count > 1u) source << '[' << member.array_count << ']';
                    source << " : packoffset(" << packoffset(member.offset) << ");\n";
                }
                source << "};\n\n";
            }
            else if (binding.logical_binding->resource)
            {
                const ShaderResourceParameter& resource = *binding.logical_binding->resource;
                source << resource_declaration(resource) << ' ' << resource.name << " : register(" << register_name << binding.register_index << ");\n\n";
            }
        }
        source << "#endif\n";
        if (!result.diagnostics.empty()) return result;
        result.source = source.str();
        std::vector<std::uint8_t> key_bytes;
        key_bytes.insert(key_bytes.end(), logical_layout.logical_layout_hash.begin(), logical_layout.logical_layout_hash.end());
        key_bytes.insert(key_bytes.end(), target_layout.target_binding_hash.begin(), target_layout.target_binding_hash.end());
        append_integer(key_bytes, static_cast<std::uint32_t>(target_layout.target));
        append_integer(key_bytes, target_layout.mapping_version);
        append_integer(key_bytes, static_cast<std::uint32_t>(stage));
        key_bytes.insert(key_bytes.end(), result.source->begin(), result.source->end());
        result.compile_key = sha256(key_bytes);
        return result;
    }
}

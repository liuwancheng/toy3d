#pragma once

#include "misc/sha256.h"

#include <cstdint>

namespace toy3d
{
    // ShaderParameterId is the single binding identity shared by ShaderFormat,
    // RenderCore, and RHI; target register assignments are deliberately separate.
    using ShaderParameterId = std::uint64_t;

    // A full SHA-256 value identifies the canonical constant-buffer byte layout.
    using ShaderDataLayoutHash = Sha256Hash;
} // namespace toy3d

namespace toy3d::shader
{
    // Re-export the canonical value types into ShaderFormat's working namespace
    // without defining a second alias or conversion type.
    using ::toy3d::ShaderDataLayoutHash;
    using ::toy3d::ShaderParameterId;
} // namespace toy3d::shader

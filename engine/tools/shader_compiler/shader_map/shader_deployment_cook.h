#pragma once

#include "file_system/platform_file.h"
#include "platform/platform_services.h"

namespace toy3d::shader
{
    struct ShaderDeploymentCookInput
    {
        PhysicalPath engine_root;
        PhysicalPath project_root;
        PhysicalPath output_root;
        PhysicalPath work_root;
        PhysicalPath compiler;
        PhysicalPath toolchain;
        bool editor = false;
    };
    bool cook_shader_deployment(PlatformFile& files, const ProcessService& processes,
                                const ShaderDeploymentCookInput& input, std::string& error);
} // namespace toy3d::shader

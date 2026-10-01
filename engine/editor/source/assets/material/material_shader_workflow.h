#pragma once

#include "asset/asset_identity.h"
#include "file_system/file_system.h"
#include "file_system/native_platform_file.h"
#include "shader/shader_editor_properties.h"
#include "process/process.h"
#include "rendercore/material/material_program_validation.h"
#include "rendercore/material/material.h"
#include "threading/thread.h"
#include "threading/thread_manager.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    struct MaterialShaderPaths
    {
        PhysicalPath project_shader;
        PhysicalPath project_config;
        PhysicalPath engine_shader;
        PhysicalPath engine_include;
        PhysicalPath builtin_entries;
        PhysicalPath saved;
        PhysicalPath compiler;
        PhysicalPath toolchain;
        PhysicalPath code_executable;
    };

    struct MaterialShaderSource
    {
        std::string name;
        VirtualPath path;
        ShaderMapProgramRef program;
        std::vector<shader::ShaderEditorProperty> properties;
    };

    // Application owns this Shader-specific source/compile/publication workflow
    // and injects shared process/thread services. All public operations are GT.
    class MaterialShaderWorkflow final
    {
      public:
        MaterialShaderWorkflow(ProcessService& processes, ThreadManager& threads) : processes_(processes), threads_(threads) {}
        ~MaterialShaderWorkflow();
        bool initialize(MaterialShaderPaths paths, MaterialRef defaults, std::string& error);
        const std::vector<MaterialShaderSource>& sources() const { return sources_; }
        ShaderMapProgramRef program(const std::string& name) const;
        const MaterialShaderSource* find(const std::string& name) const;
        bool open_source(const std::string& name, std::uint32_t line = 1u, std::uint32_t column = 1u);
        bool has_error_location() const;
        bool open_error();
        bool recompile(const std::string& name, AssetId origin = {}, std::uint64_t session_revision = 0u);
        void tick();
        void collect_validation(std::vector<MaterialProgramValidationRef>& requests);
        bool busy() const { return worker_ || validation_ || candidate_; }
        const ShaderMapProgramRef& candidate() const { return candidate_; }
        bool candidate_ready() const { return candidate_ && !validation_; }
        const AssetId& origin() const { return origin_; }
        std::uint64_t origin_revision() const { return origin_revision_; }
        const std::vector<shader::ShaderEditorProperty>& candidate_properties() const { return candidate_properties_; }
        bool publish();
        void reject(const std::string& error);
        const std::string& status() const { return status_; }
        const std::string& output() const { return output_; }
        const std::string& error() const { return error_; }
        void shutdown();

      private:
        struct CompileResult
        {
            ProcessResult process;
            std::atomic<bool> complete{false};
        };
        bool read_sources(std::string& error);
        bool physical_source(const MaterialShaderSource& source, PhysicalPath& path, std::string& error) const;
        bool load_candidate(const PhysicalPath& directory, const std::string& name, std::string& error);
        bool validate_interface(const ShaderMapProgram& program, std::string& error) const;
        bool mount(const PhysicalPath& root, const std::string& virtual_root, bool writable, std::string& error);
        bool error_location(std::uint32_t& line, std::uint32_t& column) const;
        ProcessService& processes_;
        ThreadManager& threads_;
        NativePlatformFile platform_;
        FileSystem files_;
        MaterialShaderPaths paths_;
        MaterialRef defaults_;
        std::vector<MaterialShaderSource> sources_;
        std::unique_ptr<Thread> worker_;
        std::shared_ptr<CompileResult> result_;
        std::atomic<bool> cancel_{false};
        std::string request_name_;
        AssetId origin_;
        std::uint64_t origin_revision_ = 0u;
        AssetId request_id_;
        Sha256Hash source_hash_{};
        PhysicalPath request_directory_;
        MaterialProgramValidationRef validation_;
        bool validation_sent_ = false;
        std::uint32_t validation_attempts_ = 0u;
        ShaderMapProgramRef candidate_;
        std::vector<shader::ShaderEditorProperty> candidate_properties_;
        std::map<std::string, Sha256Hash> candidate_dependencies_;
        std::string candidate_relative_;
        std::string status_;
        std::string output_;
        std::string error_;
        std::vector<std::string> restore_queue_;
    };
}

#pragma once

#include "asset/asset_identity.h"
#include "file_system/file_system.h"
#include "file_system/native_platform_file.h"
#include "shader/shader_editor_properties.h"
#include "platform/platform_services.h"
#include "rendercore/material/material_program_validation.h"
#include "rendercore/material/material.h"
#include "rendercore/shader/builtin_shader_update.h"
#include "shader_parameters/builtin_shader_sources.generated.h"
#include "threading/thread.h"
#include "threading/thread_manager.h"

#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    constexpr std::size_t maximum_registered_shader_sources = 256u;
    constexpr std::size_t maximum_shader_manifest_bytes = 64u * 1024u;
    constexpr std::size_t maximum_shader_source_bytes = 4u * 1024u * 1024u;

    enum class ShaderTaskPhase
    {
        Idle,
        Compiling,
        Validating,
        Publishing,
        Cancelling,
        Completed
    };

    struct ShaderTaskDiagnostic
    {
        std::string name;
        std::string message;
        std::uint32_t line = 0u;
        std::uint32_t column = 0u;
    };

    // GT-owned progress persists after completion; diagnostics survive the next
    // source in a batch. UI never infers lifecycle from human-readable logs.
    struct ShaderTaskStatus
    {
        std::uint64_t id = 0u;
        ShaderTaskPhase phase = ShaderTaskPhase::Idle;
        std::string current_source;
        std::size_t total = 0u;
        std::size_t applied = 0u;
        std::size_t failed = 0u;
        std::size_t cancelled = 0u;
        bool restoring = false;
        std::vector<ShaderTaskDiagnostic> diagnostics;
    };

    struct ShaderWorkflowPaths
    {
        PhysicalPath project_shader;
        PhysicalPath engine_shader;
        PhysicalPath engine_include;
        PhysicalPath builtin_root;
        PhysicalPath saved;
        PhysicalPath compiler;
        PhysicalPath toolchain;
        PhysicalPath code_executable;
    };

    struct EditorShaderSource
    {
        std::string name;
        VirtualPath path;
        ShaderMapProgramRef program;
        std::vector<shader::ShaderEditorProperty> properties;
        std::string pass = "Forward";
        BuiltinShaderUsage usage = BuiltinShaderUsage::Material;
        PhysicalPath artifacts;
        std::string diagnostic;
        std::string discovery_error;
        bool name_conflict = false;
    };

    // Application owns this Shader-specific source/compile/publication workflow
    // and injects shared process/thread services. All public operations are GT.
    class ShaderWorkflow final
    {
      public:
        ShaderWorkflow(ProcessService& processes, ThreadManager& threads) : processes_(processes), threads_(threads)
        {
        }
        ~ShaderWorkflow();
        bool initialize(ShaderWorkflowPaths paths, MaterialRef defaults, std::string& error);
        const std::vector<EditorShaderSource>& sources() const
        {
            return sources_;
        }
        ShaderMapProgramRef program(const std::string& name) const;
        const EditorShaderSource* find(const std::string& name) const;
        bool open_source(const std::string& name, std::uint32_t line = 1u, std::uint32_t column = 1u);
        bool has_error_location() const;
        bool open_error();
        bool recompile(const std::string& name, AssetId origin = {}, std::uint64_t session_revision = 0u);
        bool recompile_all();
        bool create_source(const std::string& name, const std::string& relative_path, const std::string& template_name);
        const ShaderTaskStatus& task_status() const
        {
            return task_;
        }
        void cancel();
        bool batch_active() const
        {
            return batch_active_;
        }
        std::string unavailable_reason(const std::string& name) const;
        void tick();
        void collect_validation(std::vector<MaterialProgramValidationRef>& requests);
        void collect_builtin_updates(std::vector<BuiltinShaderUpdateRef>& requests);
        bool busy() const
        {
            return active() || !restore_queue_.empty() || !global_candidates_.empty() || global_failed_ ||
                   batch_active_;
        }
        const ShaderMapProgramRef& candidate() const
        {
            return candidate_;
        }
        bool candidate_ready() const;
        const AssetId& origin() const
        {
            return origin_;
        }
        std::uint64_t origin_revision() const
        {
            return origin_revision_;
        }
        const std::vector<shader::ShaderEditorProperty>& candidate_properties() const
        {
            return candidate_properties_;
        }
        bool publish();
        void reject(const std::string& error);
        const std::string& status() const
        {
            return status_;
        }
        std::string progress() const;
        const std::string& output() const
        {
            return output_;
        }
        const std::string& error() const
        {
            return error_;
        }
        void shutdown();

      private:
        struct Revision
        {
            std::string name;
            ShaderMapProgramRef program;
            std::vector<shader::ShaderEditorProperty> properties;
            Sha256Hash source_hash{};
            std::map<std::string, Sha256Hash> dependencies;
            std::string relative;
        };
        struct CompileResult
        {
            ProcessResult process;
            std::atomic<bool> complete{false};
        };
        bool request_failed(const std::string& operation, const std::string& name);
        void begin_task(std::size_t total, bool restoring = false);
        void record_task_error(const std::string& name, const std::string& message);
        void complete_task();
        bool active() const
        {
            return worker_ || validation_ || candidate_ || builtin_update_;
        }
        bool start_compile(const std::string& name, AssetId origin, std::uint64_t revision);
        void restore(const std::string& name);
        Revision take_revision();
        void stage_builtin();
        bool validate_revision(const Revision& revision, std::string& error) const;
        bool write_publication(const std::string& name, const std::string& relative, const Sha256Hash& hash);
        bool publish_builtin();
        void finish_global_group();
        void finish_batch_item(bool success, std::size_t count = 1u);
        void finish_batch();
        bool read_sources(std::string& error);
        bool physical_source(const EditorShaderSource& source, PhysicalPath& path, std::string& error) const;
        bool load_candidate(const PhysicalPath& directory, const std::string& name, std::string& error);
        bool validate_interface(const ShaderMapProgram& program, std::string& error) const;
        bool mount(const PhysicalPath& root, const std::string& virtual_root, bool writable, std::string& error);
        bool error_location(std::uint32_t& line, std::uint32_t& column) const;
        ProcessService& processes_;
        ThreadManager& threads_;
        NativePlatformFile platform_;
        FileSystem files_;
        ShaderWorkflowPaths paths_;
        ShaderTaskStatus task_;
        MaterialRef defaults_;
        std::vector<EditorShaderSource> sources_;
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
        std::vector<std::string> compile_queue_;
        std::size_t compile_index_ = 0u;
        bool restoring_ = false;
        bool saved_candidate_ = false;
        bool batch_active_ = false;
        bool batch_cancelled_ = false;
        std::size_t batch_total_ = 0u;
        std::size_t batch_success_ = 0u;
        std::size_t batch_failed_ = 0u;
        std::size_t batch_cancel_count_ = 0u;
        std::vector<Revision> global_candidates_;
        bool global_failed_ = false;
        std::vector<Revision> builtin_revisions_;
        BuiltinShaderUpdateRef builtin_update_;
        bool builtin_sent_ = false;
        std::map<std::string, std::pair<std::string, Sha256Hash>> global_restore_;
        std::set<std::string> ignore_saved_;
    };
} // namespace toy3d

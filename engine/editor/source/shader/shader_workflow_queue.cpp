#include "shader/shader_workflow.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "logging/logger.h"

namespace toy3d
{
    namespace
    {
        bool request_relative(const std::string& relative)
        {
            AssetId id;
            return relative.size() == 41u && relative.compare(0u, 9u, "requests/") == 0 &&
                   AssetId::parse(relative.substr(9u), id) && id.valid() && id.hex() == relative.substr(9u);
        }
    } // namespace

    void ShaderWorkflow::begin_task(std::size_t total, bool restoring)
    {
        const auto next_id = task_.id + 1u;
        task_ = {};
        task_.id = next_id;
        task_.total = total;
        task_.restoring = restoring;
        task_.phase = total == 0u ? ShaderTaskPhase::Completed : ShaderTaskPhase::Validating;
    }

    void ShaderWorkflow::record_task_error(const std::string& name, const std::string& message)
    {
        if (task_.phase == ShaderTaskPhase::Completed || task_.phase == ShaderTaskPhase::Idle)
        {
            return;
        }
        ShaderTaskDiagnostic diagnostic;
        diagnostic.name = name;
        // Cards retain bounded summaries; the Logger keeps full compiler output.
        constexpr std::size_t maximum_diagnostic_bytes = 2048u;
        std::string detail;
        std::istringstream lines(message);
        std::string line;
        const auto* registered = find(name);
        while (std::getline(lines, line))
        {
            if (line.find_first_not_of(" \t\r") == std::string::npos)
            {
                continue;
            }
            if (detail.empty())
            {
                detail = line;
            }
            if (line.find(": error:") != std::string::npos || line.find("[error]") != std::string::npos)
            {
                detail = line;
                if (registered && line.find(registered->path.utf8()) != std::string::npos)
                {
                    break;
                }
            }
        }
        std::size_t bytes = std::min(detail.size(), maximum_diagnostic_bytes);
        while (bytes < detail.size() && bytes > 0u && (static_cast<unsigned char>(detail[bytes]) & 0xc0u) == 0x80u)
        {
            --bytes;
        }
        diagnostic.message = detail.substr(0u, bytes);
        if (name == request_name_ && !output_.empty() && message.find(output_) != std::string::npos)
        {
            error_location(diagnostic.line, diagnostic.column);
        }
        task_.diagnostics.push_back(std::move(diagnostic));
    }

    void ShaderWorkflow::complete_task()
    {
        const auto finished = task_.applied + task_.failed;
        task_.cancelled = task_.total > finished && batch_cancelled_ ? task_.total - finished : 0u;
        task_.phase = ShaderTaskPhase::Completed;
    }

    bool ShaderWorkflow::candidate_ready() const
    {
        const auto* source = find(request_name_);
        return candidate_ && !validation_ && source && source->usage == BuiltinShaderUsage::Material;
    }

    std::string ShaderWorkflow::unavailable_reason(const std::string& name) const
    {
        const auto* source = find(name);
        if (!source)
        {
            return "Shader '" + name + "' is not registered.";
        }
        if (source->usage != BuiltinShaderUsage::Material)
        {
            return "Shader '" + name + "' is not a Material Shader.";
        }
        if (source->program)
        {
            return {};
        }
        if (active() && request_name_ == name)
        {
            return "Shader '" + name + "' is compiling or validating. Previous assignment remains active.";
        }
        if (!source->diagnostic.empty())
        {
            return "Shader '" + name + "' is unavailable: " + source->diagnostic;
        }
        return "Shader '" + name + "' has no validated Program. Use Compile Shader or Recompile Shaders.";
    }

    std::string ShaderWorkflow::progress() const
    {
        if (!batch_active_)
        {
            return status_;
        }
        return std::to_string(batch_success_ + batch_failed_) + "/" + std::to_string(batch_total_) + " | " + status_;
    }

    bool ShaderWorkflow::recompile(const std::string& name, AssetId origin, std::uint64_t revision)
    {
        if (busy())
        {
            error_ = "Shader compilation or startup validation is in progress.";
            return request_failed("Request compile", name);
        }
        if (!read_sources(error_))
        {
            return request_failed("Recompile", name);
        }
        const auto* source = find(name);
        if (!source)
        {
            error_ = "Shader is not registered.";
            return request_failed("Recompile", name);
        }
        batch_cancelled_ = false;
        begin_task(1u);
        if (source->usage == BuiltinShaderUsage::Global)
        {
            compile_queue_.clear();
            for (const auto& item : sources_)
            {
                if (item.usage == BuiltinShaderUsage::Global)
                {
                    compile_queue_.push_back(item.name);
                }
            }
            batch_active_ = true;
            batch_total_ = compile_queue_.size();
            compile_index_ = 0u;
            batch_success_ = batch_failed_ = batch_cancel_count_ = 0u;
            task_.total = batch_total_;
            return true;
        }
        if (start_compile(name, origin, revision))
        {
            return true;
        }
        finish_batch_item(false);
        complete_task();
        return false;
    }

    bool ShaderWorkflow::recompile_all()
    {
        if (busy())
        {
            error_ = "Shader compilation or startup validation is in progress.";
            return false;
        }
        if (!read_sources(error_))
        {
            return request_failed("Recompile", "all sources");
        }
        compile_queue_.clear();
        for (const auto& source : sources_)
        {
            compile_queue_.push_back(source.name);
        }
        compile_index_ = batch_success_ = batch_failed_ = batch_cancel_count_ = 0u;
        batch_total_ = compile_queue_.size();
        batch_active_ = true;
        batch_cancelled_ = false;
        begin_task(batch_total_);
        global_failed_ = false;
        error_.clear();
        status_ = "Starting Shader compilation...";
        TOY_LOG_INFO("Recompile Shaders: {} registered sources, Vulkan ES3.1/default permutation, saved files.",
                     batch_total_);
        return true;
    }

    void ShaderWorkflow::cancel()
    {
        if (busy())
        {
            task_.phase = ShaderTaskPhase::Cancelling;
        }
        cancel_.store(true, std::memory_order_release);
        batch_cancelled_ = true;
        restore_queue_.clear();
        global_candidates_.clear();
        global_failed_ = false;
        if (builtin_update_ && builtin_update_->decision.load() == BuiltinShaderDecision::Pending)
        {
            builtin_update_->decision.store(BuiltinShaderDecision::Discard, std::memory_order_release);
        }
        if (!worker_)
        {
            candidate_.reset();
            validation_.reset();
            candidate_dependencies_.clear();
        }
        status_ = "Cancelling Shader compilation...";
    }

    void ShaderWorkflow::finish_batch_item(bool success, std::size_t count)
    {
        if (success)
        {
            task_.applied += count;
        }
        else if (!batch_cancelled_)
        {
            task_.failed += count;
        }
        if (!batch_active_)
        {
            return;
        }
        if (batch_cancelled_)
        {
            if (success)
            {
                batch_success_ += count;
            }
            return;
        }
        if (success)
        {
            batch_success_ += count;
        }
        else
        {
            batch_failed_ += count;
        }
    }

    void ShaderWorkflow::finish_batch()
    {
        batch_cancel_count_ = batch_total_ - batch_success_ - batch_failed_;
        status_ = "Shader batch: " + std::to_string(batch_success_) + " applied, " + std::to_string(batch_failed_) +
                  " failed, " + std::to_string(batch_cancel_count_) + " cancelled.";
        TOY_LOG_INFO("{}", status_);
        compile_queue_.clear();
        batch_active_ = false;
        complete_task();
        batch_cancelled_ = false;
    }

    void ShaderWorkflow::restore(const std::string& name)
    {
        task_.phase = ShaderTaskPhase::Validating;
        task_.current_source = name;
        restoring_ = true;
        batch_cancelled_ = false;
        request_name_ = name;
        origin_ = {};
        origin_revision_ = 0u;
        error_.clear();
        output_.clear();
        candidate_relative_.clear();
        saved_candidate_ = false;
        const auto* source = find(name);
        if (source && !source->discovery_error.empty())
        {
            error_ = source->discovery_error;
            reject(error_);
            return;
        }
        const auto text = files_.read_text_utf8(source->path, maximum_shader_source_bytes);
        if (!text.succeeded())
        {
            reject("Cannot read source " + source->path.utf8() + ": " + text.status().message);
            return;
        }
        source_hash_ = sha256(text.value());
        std::string saved_relative, saved_error;
        Sha256Hash saved_hash{};
        if (source->usage == BuiltinShaderUsage::Global)
        {
            const auto found = global_restore_.find(name);
            if (found != global_restore_.end())
            {
                saved_relative = found->second.first;
                saved_hash = found->second.second;
            }
        }
        else if (!ignore_saved_.count(name))
        {
            const auto pointer = VirtualPath::parse("/Saved/" + sha256_to_hex(sha256(name)) + "/current.txt");
            const auto record = files_.read_text_utf8(pointer.value(), 4096u);
            if (record.succeeded())
            {
                std::istringstream lines(record.value());
                std::string hash, extra;
                std::getline(lines, saved_relative);
                std::getline(lines, hash);
                // optional distinguishes malformed persisted hashes from valid values.
                const auto parsed = sha256_from_hex(hash);
                if (!parsed || !request_relative(saved_relative) || std::getline(lines, extra))
                {
                    saved_error = "Invalid Saved publication record.";
                    saved_relative.clear();
                }
                else
                {
                    saved_hash = *parsed;
                }
            }
            else if (record.status().code != FileErrorCode::NotFound)
            {
                saved_error = record.status().message;
            }
        }
        if (!saved_relative.empty())
        {
            PhysicalPath directory;
            const std::string deployment =
                "deployment/" + source->artifacts.utf8().substr(source->artifacts.utf8().find_last_of("/\\") + 1u);
            if (saved_hash != source_hash_)
            {
                saved_error = "Saved source hash differs from the current source.";
            }
            else if (request_relative(saved_relative))
            {
                const auto joined = platform_.join_relative(paths_.saved, saved_relative + "/entries");
                if (joined.succeeded())
                {
                    directory = joined.value();
                }
                else
                {
                    saved_error = joined.status().message;
                }
            }
            else if (saved_relative == deployment)
            {
                directory = source->artifacts;
            }
            else
            {
                saved_error = "Invalid Saved artifact locator.";
            }
            if (!directory.empty() && load_candidate(directory, name, saved_error))
            {
                candidate_relative_ = saved_relative;
                saved_candidate_ = request_relative(saved_relative);
                status_ = "Validating saved Shader: " + name;
                return;
            }
        }
        if (!source->artifacts.empty())
        {
            std::string error;
            if (load_candidate(source->artifacts, name, error))
            {
                candidate_relative_ =
                    "deployment/" + source->artifacts.utf8().substr(source->artifacts.utf8().find_last_of("/\\") + 1u);
                if (!saved_error.empty())
                {
                    TOY_LOG_WARN("Shader [{}]: {} Validating deployed version.", name, saved_error);
                }
                status_ = "Validating deployed Shader: " + name;
                return;
            }
            if (!saved_error.empty())
            {
                error = saved_error + " Deployed version: " + error;
            }
            reject(error);
            return;
        }
        reject(saved_error.empty() ? "Saved Shader artifacts are unavailable. Recompile " + name : saved_error);
    }

    ShaderWorkflow::Revision ShaderWorkflow::take_revision()
    {
        Revision result;
        result.name = request_name_;
        result.program = std::move(candidate_);
        result.properties = std::move(candidate_properties_);
        result.source_hash = source_hash_;
        result.dependencies = std::move(candidate_dependencies_);
        result.relative = candidate_relative_;
        return result;
    }

    void ShaderWorkflow::stage_builtin()
    {
        task_.phase = ShaderTaskPhase::Validating;
        const auto* source = find(request_name_);
        if (source->usage == BuiltinShaderUsage::Global)
        {
            global_candidates_.push_back(take_revision());
        }
        else
        {
            builtin_revisions_.push_back(take_revision());
            builtin_update_ = std::make_shared<BuiltinShaderUpdate>();
            builtin_update_->programs.push_back(builtin_revisions_.front().program);
            builtin_sent_ = false;
            status_ = "Validating ShadowDepth pipeline...";
        }
    }

    void ShaderWorkflow::finish_global_group()
    {
        std::size_t required = 0u;
        for (const auto& source : sources_)
        {
            if (source.usage == BuiltinShaderUsage::Global)
            {
                ++required;
            }
        }
        if (global_failed_ || global_candidates_.size() != required)
        {
            finish_batch_item(false, required);
            global_candidates_.clear();
            global_failed_ = false;
            TOY_LOG_WARN("Global Shader candidate group was discarded; the previous rendering programs remain active.");
            return;
        }
        builtin_revisions_ = std::move(global_candidates_);
        global_candidates_.clear();
        builtin_update_ = std::make_shared<BuiltinShaderUpdate>();
        for (const auto& revision : builtin_revisions_)
        {
            builtin_update_->programs.push_back(revision.program);
        }
        task_.phase = ShaderTaskPhase::Validating;
        task_.current_source = "Tonemap / ImGui / HitProxy";
        builtin_sent_ = false;
        status_ = "Validating Tonemap, ImGui and HitProxy pipelines...";
    }

    bool ShaderWorkflow::validate_revision(const Revision& revision, std::string& error) const
    {
        const auto* source = find(revision.name);
        if (!source)
        {
            error = "Shader registration changed: " + revision.name;
            return false;
        }
        const auto current = files_.read_text_utf8(source->path, maximum_shader_source_bytes);
        if (!current.succeeded())
        {
            error = current.status().message;
            return false;
        }
        if (sha256(current.value()) != revision.source_hash)
        {
            error = "Source changed during compilation: " + revision.name;
            return false;
        }
        for (const auto& dependency : revision.dependencies)
        {
            const auto path = VirtualPath::parse(dependency.first);
            if (!path.succeeded())
            {
                error = path.status().message;
                return false;
            }
            const auto text = files_.read_text_utf8(path.value(), maximum_shader_source_bytes);
            if (!text.succeeded())
            {
                error = text.status().message;
                return false;
            }
            if (sha256(text.value()) != dependency.second)
            {
                error = "Dependency changed during validation: " + dependency.first;
                return false;
            }
        }
        return true;
    }

    bool ShaderWorkflow::write_publication(const std::string& name, const std::string& relative, const Sha256Hash& hash)
    {
        const auto directory = platform_.join_relative(paths_.saved, sha256_to_hex(sha256(name)));
        if (!directory.succeeded())
        {
            error_ = directory.status().message;
            return false;
        }
        const auto made = platform_.create_directories(directory.value());
        if (!made.succeeded())
        {
            error_ = made.message;
            return false;
        }
        const auto pointer = VirtualPath::parse("/Saved/" + sha256_to_hex(sha256(name)) + "/current.txt");
        const std::string text = relative + "\n" + sha256_to_hex(hash) + "\n";
        const auto saved = files_.write_binary_atomic(
            pointer.value(), std::vector<std::uint8_t>(text.begin(), text.end()), FilePublishMode::Replace);
        if (!saved.succeeded())
        {
            error_ = "Cannot save Shader publication record: " + saved.message;
            return false;
        }
        return true;
    }

    bool ShaderWorkflow::publish_builtin()
    {
        for (const auto& revision : builtin_revisions_)
        {
            if (!validate_revision(revision, error_))
            {
                return false;
            }
        }
        if (restoring_)
        {
            return true;
        }
        if (builtin_revisions_.size() == 1u)
        {
            const auto& revision = builtin_revisions_.front();
            return write_publication(revision.name, revision.relative, revision.source_hash);
        }
        std::string text = "Toy3dGlobalShaders 1\n";
        for (const auto& revision : builtin_revisions_)
        {
            text += revision.name + "\t" + revision.relative + "\t" + sha256_to_hex(revision.source_hash) + "\n";
        }
        const auto path = VirtualPath::parse("/Saved/globals.txt");
        const auto saved = files_.write_binary_atomic(path.value(), std::vector<std::uint8_t>(text.begin(), text.end()),
                                                      FilePublishMode::Replace);
        if (!saved.succeeded())
        {
            error_ = "Cannot save Global Shader publication record: " + saved.message;
            return false;
        }
        return true;
    }

    void ShaderWorkflow::collect_builtin_updates(std::vector<BuiltinShaderUpdateRef>& requests)
    {
        if (builtin_update_ && !builtin_sent_)
        {
            requests.push_back(builtin_update_);
            builtin_sent_ = true;
        }
    }
} // namespace toy3d

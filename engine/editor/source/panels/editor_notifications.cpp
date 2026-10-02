#include "panels/editor_notifications.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "imgui.h"
#include "panels/console_panel.h"

namespace toy3d
{
    namespace
    {
        constexpr std::size_t maximum_cards = 3u;
        bool running(const ShaderTaskStatus& task)
        {
            return task.phase != ShaderTaskPhase::Idle && task.phase != ShaderTaskPhase::Completed;
        }
        const char* phase_text(ShaderTaskPhase phase)
        {
            switch (phase)
            {
            case ShaderTaskPhase::Compiling:
                return "Compiling saved source";
            case ShaderTaskPhase::Validating:
                return "Validating rendering pipelines";
            case ShaderTaskPhase::Publishing:
                return "Publishing validated programs";
            case ShaderTaskPhase::Cancelling:
                return "Cancelling; waiting for work to finish";
            default:
                return "Preparing compilation";
            }
        }
        std::string summary(const std::string& message)
        {
            const auto end = message.find_first_of("\r\n");
            constexpr std::size_t maximum_summary_bytes = 240u;
            std::size_t bytes = std::min(std::min(end, message.size()), maximum_summary_bytes);
            while (bytes < message.size() && bytes > 0u &&
                   (static_cast<unsigned char>(message[bytes]) & 0xc0u) == 0x80u)
            {
                --bytes;
            }
            return message.substr(0u, bytes);
        }
    } // namespace

    EditorNotifications::EditorNotifications(std::shared_ptr<LogBuffer> buffer) : buffer_(std::move(buffer))
    {
        if (buffer_)
        {
            const auto snapshot = buffer_->snapshot();
            log_after_ = snapshot.last_sequence;
            log_revision_ = 0u;
        }
    }

    EditorNotifications::Card& EditorNotifications::add(Kind kind, const std::string& title)
    {
        if (cards_.size() >= maximum_cards)
        {
            // Keep running work and persistent file failures visible. Retired
            // cards remain inspectable in Console, not an unbounded UI history.
            auto retired =
                std::find_if(cards_.begin(), cards_.end(),
                             [](const Card& card)
                             {
                                 return card.closed || card.kind == Kind::Success ||
                                        (card.kind == Kind::Shader && !running(card.task) && card.task.failed == 0u);
                             });
            if (retired == cards_.end())
            {
                retired = std::find_if(cards_.begin(), cards_.end(),
                                       [](const Card& card)
                                       {
                                           return card.kind != Kind::File && !running(card.task);
                                       });
            }
            if (retired == cards_.end())
            {
                retired = cards_.begin();
            }
            cards_.erase(retired);
        }
        Card card;
        card.id = ++next_id_;
        card.kind = kind;
        card.title = title;
        cards_.push_back(std::move(card));
        return cards_.back();
    }

    void EditorNotifications::success(const std::string& title, const std::string& message)
    {
        add(Kind::Success, title).message = message;
    }

    void EditorNotifications::update(const ShaderTaskStatus& task)
    {
        if (task.id != 0u)
        {
            auto card = std::find_if(cards_.begin(), cards_.end(),
                                     [&task](const Card& value)
                                     {
                                         return value.kind == Kind::Shader && value.task.id == task.id;
                                     });
            if (observed_task_ != task.id)
            {
                observed_task_ = task.id;
                if (running(task) || task.failed != 0u)
                {
                    add(Kind::Shader, task.restoring ? "Validating Shader cache" : "Compiling Shaders").task = task;
                }
            }
            else if (card != cards_.end() && !card->closed)
            {
                const bool was_running = running(card->task);
                card->task = task;
                if (was_running && !running(task))
                {
                    card->remaining = 4.0f;
                }
            }
        }
        if (!buffer_ || buffer_->revision() == log_revision_)
        {
            return;
        }
        const auto snapshot = buffer_->snapshot();
        log_revision_ = snapshot.revision;
        std::shared_ptr<const LogRecord> file_record;
        for (const auto& record : snapshot.records)
        {
            if (record->sequence <= log_after_ ||
                (record->level != LogLevel::TOY_ERROR && record->level != LogLevel::TOY_CRITICAL))
            {
                continue;
            }
            if (snapshot.file_requested && !snapshot.file_ready && record->logger_name == "Logging")
            {
                file_record = record;
                continue;
            }
            // Match retained structured diagnostics to their full log message;
            // this only links a record, never derives task state from its text.
            const bool shader_log = record->source_file.find("shader_workflow") != std::string::npos;
            auto task_card = std::find_if(cards_.begin(), cards_.end(),
                                          [&task, &record, shader_log](const Card& card)
                                          {
                                              if (!shader_log || card.kind != Kind::Shader || card.task.id != task.id)
                                              {
                                                  return false;
                                              }
                                              return std::any_of(task.diagnostics.begin(), task.diagnostics.end(),
                                                                 [&record](const ShaderTaskDiagnostic& diagnostic)
                                                                 {
                                                                     return !diagnostic.message.empty() &&
                                                                            record->message.find(diagnostic.message) !=
                                                                                std::string::npos;
                                                                 });
                                          });
            if (task_card != cards_.end())
            {
                task_card->record = record;
                continue;
            }
            auto card = std::find_if(cards_.begin(), cards_.end(),
                                     [](const Card& value)
                                     {
                                         return value.kind == Kind::Errors && !value.closed;
                                     });
            if (card == cards_.end())
            {
                add(Kind::Errors, "Editor errors");
                card = cards_.end() - 1;
            }
            card->record = record;
            ++card->error_count;
            card->message = summary(record->message);
        }
        log_after_ = snapshot.last_sequence;
        const bool failed = snapshot.file_requested && !snapshot.file_ready;
        auto file = std::find_if(cards_.begin(), cards_.end(),
                                 [](const Card& value)
                                 {
                                     return value.kind == Kind::File;
                                 });
        if (failed && !file_failed_)
        {
            add(Kind::File, "File logging failed").message = summary(snapshot.output_error);
        }
        else if (failed && file != cards_.end())
        {
            file->message = summary(snapshot.output_error);
        }
        else if (!failed && file_failed_ && file != cards_.end())
        {
            file->kind = Kind::Success;
            file->title = "File logging restored";
            file->message.clear();
            file->remaining = 4.0f;
        }
        if (file_record)
        {
            file = std::find_if(cards_.begin(), cards_.end(),
                                [](const Card& value)
                                {
                                    return value.kind == Kind::File;
                                });
            if (file != cards_.end())
            {
                file->record = std::move(file_record);
            }
        }
        file_failed_ = failed;
    }

    void EditorNotifications::draw(ConsolePanel& console, ShaderWorkflow& shaders)
    {
        const auto* viewport = ImGui::GetMainViewport();
        const float margin = 14.0f;
        const float width = std::max(160.0f, std::min(380.0f, viewport->WorkSize.x - 2.0f * margin));
        float bottom = viewport->WorkPos.y + viewport->WorkSize.y - 32.0f;
        for (auto entry = cards_.rbegin(); entry != cards_.rend(); ++entry)
        {
            auto& card = *entry;
            if (card.closed)
            {
                continue;
            }
            const bool active = card.kind == Kind::Shader && running(card.task);
            const bool error = card.kind == Kind::Errors || card.kind == Kind::File || card.task.failed != 0u;
            const ImVec4 accent = error    ? ImVec4(1.0f, 0.40f, 0.34f, 1.0f)
                                  : active ? ImVec4(0.35f, 0.65f, 1.0f, 1.0f)
                                           : ImVec4(0.40f, 0.82f, 0.55f, 1.0f);
            const std::string window = "###EditorNotification" + std::to_string(card.id);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - margin, bottom),
                                    ImGuiCond_Always, ImVec2(1, 1));
            ImGui::SetNextWindowSize(ImVec2(width, 0));
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(width, 0),
                ImVec2(width, std::max(110.0f, (viewport->WorkSize.y - 70.0f) / static_cast<float>(maximum_cards))));
            ImGui::SetNextWindowBgAlpha(0.97f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, accent);
            ImGui::Begin(window.c_str(), nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNavFocus);
            const char* title = card.title.c_str();
            if (card.kind == Kind::Shader && !active)
            {
                title = error ? (card.task.restoring ? "Shader validation failed" : "Shader compilation failed")
                        : card.task.cancelled != 0u ? "Shader compilation cancelled"
                                                    : "Shaders ready";
            }
            ImGui::TextColored(accent, "%s", title);
            if (!active && card.kind != Kind::File)
            {
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - 18.0f);
                if (ImGui::SmallButton("x"))
                {
                    card.closed = true;
                }
            }
            if (card.kind == Kind::Shader)
            {
                if (active)
                {
                    ImGui::TextWrapped("%s", card.task.current_source.c_str());
                    ImGui::TextDisabled("%s", phase_text(card.task.phase));
                    const float ratio = card.task.total == 0u
                                            ? 0.0f
                                            : static_cast<float>(card.task.applied + card.task.failed) /
                                                  static_cast<float>(card.task.total);
                    const std::string label =
                        std::to_string(card.task.applied + card.task.failed) + "/" + std::to_string(card.task.total);
                    ImGui::ProgressBar(ratio, ImVec2(-1, 4), "");
                    ImGui::TextDisabled("%s complete%s", label.c_str(),
                                        std::fmod(ImGui::GetTime(), 1.0) < 0.5 ? " ." : " ..");
                    if (!card.task.restoring)
                    {
                        ImGui::BeginDisabled(card.task.phase == ShaderTaskPhase::Cancelling ||
                                             card.task.id != shaders.task_status().id);
                        if (ImGui::SmallButton("Cancel"))
                        {
                            shaders.cancel();
                        }
                        ImGui::EndDisabled();
                    }
                }
                else
                {
                    ImGui::TextWrapped("%zu applied / %zu failed / %zu cancelled", card.task.applied, card.task.failed,
                                       card.task.cancelled);
                }
                if (!card.task.diagnostics.empty())
                {
                    const auto& diagnostic = card.task.diagnostics.back();
                    ImGui::TextWrapped("%s: %s", diagnostic.name.c_str(), summary(diagnostic.message).c_str());
                    if (diagnostic.line != 0u && ImGui::SmallButton("Open Source"))
                    {
                        shaders.open_source(diagnostic.name, diagnostic.line, diagnostic.column);
                    }
                }
            }
            else
            {
                if (card.kind == Kind::Errors)
                {
                    ImGui::TextDisabled("%zu errors; latest:", card.error_count);
                }
                ImGui::TextWrapped("%s", card.message.c_str());
            }
            if (error || card.record)
            {
                if (ImGui::SmallButton("View Console"))
                {
                    if (card.record)
                    {
                        console.reveal(card.record);
                    }
                    else
                    {
                        console.open();
                    }
                }
            }
            const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            if (!active && !error && !hovered)
            {
                card.remaining -= ImGui::GetIO().DeltaTime;
            }
            if (!active && !error && card.remaining <= 0.0f)
            {
                card.closed = true;
            }
            bottom -= ImGui::GetWindowSize().y + 8.0f;
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }
        cards_.erase(std::remove_if(cards_.begin(), cards_.end(),
                                    [](const Card& card)
                                    {
                                        return card.closed;
                                    }),
                     cards_.end());
    }
} // namespace toy3d

#include "panels/console_panel.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <iterator>
#include <string>
#include <utility>
#include "imgui.h"
#include "logging/logger.h"
#include "platform/platform_services.h"
#include "platform/platform_defines.h"

namespace toy3d
{
    namespace
    {
        constexpr std::array<const char*, log_level_count> level_names =
            {"Trace", "Debug", "Info", "Warning", "Error", "Critical"};

        bool contains_text(const std::string& text, const char* search, const char* search_end)
        {
            return std::search(text.begin(), text.end(), search, search_end,
                [](unsigned char left, unsigned char right) { return std::tolower(left) == std::tolower(right); }) != text.end();
        }

        std::string time_text(std::chrono::system_clock::time_point time)
        {
            const std::time_t value = std::chrono::system_clock::to_time_t(time);
            std::tm parts{};
#if WITH_WIN
            if (localtime_s(&parts, &value) != 0) return "Unknown";
#else
            if (!localtime_r(&value, &parts)) return "Unknown";
#endif
            char result[32]{};
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;
            std::snprintf(result, sizeof(result), "%02d:%02d:%02d.%03d", parts.tm_hour, parts.tm_min,
                          parts.tm_sec, static_cast<int>(milliseconds));
            return result;
        }

        std::string source_text(const LogRecord& record)
        {
            const auto offset = record.source_file.find_last_of("/\\");
            return record.source_file.empty() ? record.logger_name :
                record.source_file.substr(offset == std::string::npos ? 0 : offset + 1u) + ":" + std::to_string(record.source_line);
        }

        ImVec4 level_color(LogLevel level)
        {
            if (level == LogLevel::TOY_ERROR || level == LogLevel::TOY_CRITICAL) return ImVec4(1.0f, 0.4f, 0.35f, 1.0f);
            if (level == LogLevel::TOY_WARN) return ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
            return ImGui::GetStyleColorVec4(ImGuiCol_Text);
        }
    }

    // --------------------------------------------------------------------------
    // ConsoleLogFilter: independent display-level and text predicates
    // --------------------------------------------------------------------------
    ConsoleLogFilter::ConsoleLogFilter() { defaults(); }
    void ConsoleLogFilter::defaults()
    { levels.fill(true); levels[static_cast<std::size_t>(LogLevel::TOY_TRACE)] = false; levels[static_cast<std::size_t>(LogLevel::TOY_DEBUG)] = false; }
    void ConsoleLogFilter::errors_only()
    { levels.fill(false); levels[static_cast<std::size_t>(LogLevel::TOY_ERROR)] = true; levels[static_cast<std::size_t>(LogLevel::TOY_CRITICAL)] = true; }
    bool ConsoleLogFilter::matches(const LogRecord& record) const
    {
        const auto index = static_cast<std::size_t>(record.level);
        const char* search_end = search.data() + std::distance(search.begin(), std::find(search.begin(), search.end(), '\0'));
        return index < levels.size() && levels[index] && (search[0] == '\0' ||
            contains_text(record.message, search.data(), search_end) || contains_text(record.source_file, search.data(), search_end) ||
            contains_text(record.logger_name, search.data(), search_end));
    }

    // --------------------------------------------------------------------------
    // ConsolePanel: Game Thread view of a session buffer, never a log producer callback
    // --------------------------------------------------------------------------
    ConsolePanel::ConsolePanel(std::shared_ptr<LogBuffer> buffer) : buffer_(std::move(buffer)) {}
    void ConsolePanel::open() { open_ = true; focus_requested_ = true; }
    void ConsolePanel::reveal(std::shared_ptr<const LogRecord> record)
    { revealed_ = std::move(record); open(); }
    void ConsolePanel::open_log_directory()
    {
        refresh();
        const auto separator = snapshot_.file_path.find_last_of("/\\");
        std::string error = "The log file path has no directory.";
        if (separator == std::string::npos ||
            !open_directory_on_desktop(PhysicalPath(snapshot_.file_path.substr(0u, separator)), error))
            TOY_LOG_ERROR("Open log directory: {}", error);
    }
    void ConsolePanel::refresh()
    {
        if (!buffer_ || buffer_->revision() == snapshot_.revision) return;
        snapshot_ = buffer_->snapshot();
        counts_.fill(0);
        for (const auto& record : snapshot_.records)
        {
            const auto level = static_cast<std::size_t>(record->level);
            if (record->sequence > display_after_ && level < counts_.size()) ++counts_[level];
        }
        if (selected_ != 0 && std::none_of(snapshot_.records.begin(), snapshot_.records.end(),
            [this](const std::shared_ptr<const LogRecord>& record) { return record->sequence == selected_; })) selected_ = 0;
    }
    void ConsolePanel::clear_display()
    { refresh(); display_after_ = snapshot_.last_sequence; selected_ = 0; counts_.fill(0); }
    std::size_t ConsolePanel::error_count()
    { refresh(); return counts_[static_cast<std::size_t>(LogLevel::TOY_ERROR)] + counts_[static_cast<std::size_t>(LogLevel::TOY_CRITICAL)]; }
    std::size_t ConsolePanel::warning_count()
    { refresh(); return counts_[static_cast<std::size_t>(LogLevel::TOY_WARN)]; }

    void ConsolePanel::draw()
    {
        refresh();
        if (!open_) return;
        ImGui::SetNextWindowSize(ImVec2(900, 380), ImGuiCond_FirstUseEver);
        if (focus_requested_) { ImGui::SetNextWindowFocus(); focus_requested_ = false; }
        if (!ImGui::Begin("Console", &open_)) { ImGui::End(); return; }
        for (std::size_t index = 0; index < level_names.size(); ++index)
        {
            if (index != 0) ImGui::SameLine();
            const std::string label = std::string(level_names[index]) + " (" + std::to_string(counts_[index]) +
                ")###" + level_names[index];
            ImGui::Checkbox(label.c_str(), &filter_.levels[index]);
        }
        if (ImGui::Button("All")) filter_.levels.fill(true);
        ImGui::SameLine(); if (ImGui::Button("Errors Only")) filter_.errors_only();
        ImGui::SameLine(); if (ImGui::Button("Defaults")) filter_.defaults();
        ImGui::SameLine(); if (ImGui::Button("Clear Display")) clear_display();
        ImGui::SameLine(); ImGui::Checkbox("Auto-scroll", &auto_scroll_);
        ImGui::SetNextItemWidth(260.0f);
        ImGui::InputTextWithHint("##log_search", "Search message or source", filter_.search.data(), filter_.search.size());
        ImGui::SameLine();
        ImGui::BeginDisabled(!snapshot_.file_requested || snapshot_.file_path.empty());
        if (ImGui::Button("Open Log Directory")) open_log_directory();
        ImGui::EndDisabled();
        ImGui::TextDisabled("History evicted: %llu | File: %s", static_cast<unsigned long long>(snapshot_.evicted_records),
            snapshot_.file_requested ? (snapshot_.file_ready ? "Writing" : "FAILED") : "Disabled");
        if (snapshot_.file_requested && !snapshot_.file_ready)
            ImGui::TextColored(level_color(LogLevel::TOY_ERROR), "File logging failed. %s", snapshot_.output_error.c_str());

        if (revealed_)
        {
            // Notification navigation is independent of checkbox/search/clear.
            // A shared record keeps its full details available after buffer eviction.
            ImGui::Separator();
            ImGui::TextColored(level_color(revealed_->level), "Notification: %s", source_text(*revealed_).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Dismiss Details")) revealed_.reset();
            if (revealed_)
            {
                ImGui::BeginChild("NotificationDetails", ImVec2(0, 95), true, ImGuiWindowFlags_HorizontalScrollbar);
                ImGui::TextUnformatted(revealed_->message.c_str());
                ImGui::EndChild();
            }
        }

        std::vector<const LogRecord*> visible;
        for (const auto& record : snapshot_.records)
            if (record->sequence > display_after_ && filter_.matches(*record)) visible.push_back(record.get());
        const float detail_height = selected_ != 0 ? 110.0f : 0.0f;
        if (ImGui::BeginTable("LogRecords", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV,
                              ImVec2(0, std::max(80.0f, ImGui::GetContentRegionAvail().y - detail_height))))
        {
            ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Message"); ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
            const bool was_at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()));
            while (clipper.Step())
            {
                for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
                {
                    const LogRecord& record = *visible[static_cast<std::size_t>(index)];
                    ImGui::PushID(std::to_string(record.sequence).c_str());
                    ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable(time_text(record.time).c_str(), selected_ == record.sequence,
                                           ImGuiSelectableFlags_SpanAllColumns)) selected_ = record.sequence;
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextColored(level_color(record.level), "%s", level_names[static_cast<std::size_t>(record.level)]);
                    ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(source_text(record).c_str());
                    ImGui::TableSetColumnIndex(3);
                    const auto end = record.message.find_first_of("\r\n");
                    ImGui::TextUnformatted(record.message.c_str(), record.message.c_str() + std::min(end, record.message.size()));
                    ImGui::PopID();
                }
            }
            if (auto_scroll_ && was_at_bottom && scrolled_sequence_ != snapshot_.last_sequence) ImGui::SetScrollHereY(1.0f);
            scrolled_sequence_ = snapshot_.last_sequence;
            ImGui::EndTable();
        }
        for (const auto& record : snapshot_.records)
        {
            if (record->sequence != selected_) continue;
            ImGui::Separator();
            if (ImGui::Button("Copy Full Message")) ImGui::SetClipboardText(record->message.c_str());
            ImGui::SameLine(); ImGui::TextDisabled("%s | thread %llu%s", record->logger_name.c_str(),
                static_cast<unsigned long long>(record->thread_id), record->truncated ? " | Memory preview truncated" : "");
            ImGui::BeginChild("LogDetails", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            if (!record->source_file.empty()) ImGui::Text("%s:%d", record->source_file.c_str(), record->source_line);
            ImGui::TextUnformatted(record->message.c_str());
            ImGui::EndChild();
            break;
        }
        ImGui::End();
    }
}

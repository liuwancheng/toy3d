#pragma once

#include "logging/log_buffer.h"
#include "shader/shader_workflow.h"

#include <memory>
#include <string>
#include <vector>

namespace toy3d
{
    class ConsolePanel;

    // Editor-only GT presentation. Workers publish workflow results/log records,
    // never callbacks into ImGui or scene owners.
    class EditorNotifications final
    {
      public:
        explicit EditorNotifications(std::shared_ptr<LogBuffer> buffer);
        void update(const ShaderTaskStatus& task);
        void success(const std::string& title, const std::string& message);
        void draw(ConsolePanel& console, ShaderWorkflow& shaders);
        std::size_t count() const { return cards_.size(); }
      private:
        enum class Kind { Shader, Errors, File, Success };
        struct Card
        {
            std::uint64_t id = 0u;
            Kind kind = Kind::Success;
            std::string title;
            std::string message;
            ShaderTaskStatus task;
            std::shared_ptr<const LogRecord> record;
            std::size_t error_count = 0u;
            float remaining = 4.0f;
            bool closed = false;
        };
        Card& add(Kind kind, const std::string& title);
        std::shared_ptr<LogBuffer> buffer_;
        std::vector<Card> cards_;
        std::uint64_t log_after_ = 0u;
        std::uint64_t log_revision_ = 0u;
        std::uint64_t next_id_ = 0u;
        std::uint64_t observed_task_ = 0u;
        bool file_failed_ = false;
    };
}

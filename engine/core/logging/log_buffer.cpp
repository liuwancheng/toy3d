#include "logging/log_buffer.h"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace toy3d
{
    namespace
    {
        std::size_t record_bytes(const LogRecord& record)
        {
            return sizeof(LogRecord) + record.message.size() + record.source_file.size() + record.logger_name.size();
        }

        void trim_field(std::string& field, std::size_t& excess)
        {
            const std::size_t removed = std::min(excess, field.size());
            std::size_t kept = field.size() - removed;
            // Preserve UTF-8 boundaries when bounding an oversized message.
            while (kept > 0 && kept < field.size() && (static_cast<unsigned char>(field[kept]) & 0xc0u) == 0x80u)
            {
                --kept;
            }
            excess -= std::min(excess, field.size() - kept);
            std::string(field.data(), kept).swap(field);
        }
    } // namespace

    LogBuffer::LogBuffer(LogBufferLimits limits) : limits_(limits)
    {
        if (limits_.max_records == 0 || limits_.max_bytes < sizeof(LogRecord))
        {
            throw std::invalid_argument("LogBuffer requires a nonzero record limit and room for record metadata.");
        }
    }

    void LogBuffer::append_locked(LogRecord record)
    {
        if (record_bytes(record) > limits_.max_bytes)
        {
            std::size_t excess = record_bytes(record) - limits_.max_bytes;
            trim_field(record.message, excess);
            trim_field(record.source_file, excess);
            trim_field(record.logger_name, excess);
            record.truncated = true;
        }
        const std::size_t bytes = record_bytes(record);
        record.sequence = sequence_ + 1u;
        auto stored = std::make_shared<const LogRecord>(std::move(record));
        while (!records_.empty() &&
               (records_.size() >= limits_.max_records || retained_bytes_ > limits_.max_bytes - bytes))
        {
            retained_bytes_ -= record_bytes(*records_.front());
            records_.pop_front();
            ++evicted_records_;
        }
        records_.push_back(std::move(stored));
        retained_bytes_ += bytes;
        ++sequence_;
        ++revision_;
    }

    void LogBuffer::append(LogRecord record)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        append_locked(std::move(record));
    }

    LogSnapshot LogBuffer::snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        LogSnapshot result;
        result.records.assign(records_.begin(), records_.end());
        result.revision = revision_;
        result.last_sequence = sequence_;
        result.evicted_records = evicted_records_;
        result.retained_bytes = retained_bytes_;
        result.file_requested = file_requested_;
        result.file_ready = file_ready_;
        result.file_path = file_path_;
        result.output_error = output_error_;
        return result;
    }

    std::uint64_t LogBuffer::revision() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return revision_;
    }

    void LogBuffer::configure_file(bool requested, const std::string& path, bool ready)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        file_requested_ = requested;
        file_path_ = path;
        file_ready_ = ready;
        output_error_.clear();
        ++revision_;
    }

    void LogBuffer::file_flushed()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!file_ready_)
        {
            file_ready_ = true;
            ++revision_;
        }
    }

    void LogBuffer::report_output_error(const std::string& message, bool file_failure)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_failure && file_ready_)
        {
            file_ready_ = false;
            ++revision_;
        }
        // Retain a bounded health message independently of record eviction.
        constexpr std::size_t max_diagnostic_bytes = 4096u;
        std::string diagnostic = message;
        std::size_t excess = diagnostic.size() > max_diagnostic_bytes ? diagnostic.size() - max_diagnostic_bytes : 0;
        if (excess != 0)
        {
            trim_field(diagnostic, excess);
        }
        if (output_error_ == diagnostic)
        {
            return;
        }
        output_error_ = std::move(diagnostic);
        LogRecord record;
        record.time = std::chrono::system_clock::now();
        record.level = LogLevel::TOY_ERROR;
        record.thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
        record.logger_name = "Logging";
        record.message = message;
        append_locked(std::move(record));
    }
} // namespace toy3d

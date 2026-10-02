#include "logging/logger.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
#include "misc/utf8.h"

namespace
{
    int failures = 0;
    void check(bool result, const char* message)
    { if (!result) { ++failures; std::cerr << "FAILED: " << message << '\n'; } }

    std::string read_file(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }

    void buffer_contract()
    {
        using namespace toy3d;
        LogBuffer buffer({2u, 4096u});
        LogRecord record; record.message = "first";
        buffer.append(record);
        const auto held = buffer.snapshot();
        record.message = "second"; buffer.append(record);
        record.message = "third"; buffer.append(record);
        const auto snapshot = buffer.snapshot();
        check(snapshot.records.size() == 2u && snapshot.evicted_records == 1u && snapshot.last_sequence == 3u,
              "Count bound evicts oldest entries while sequence remains monotonic");
        check(held.records.front()->message == "first", "Immutable reader survives producer eviction");
        LogBuffer tiny({10u, sizeof(LogRecord) + 5u});
        record.message = u8"汉字日志"; tiny.append(record);
        const auto trimmed = tiny.snapshot();
        check(trimmed.retained_bytes <= sizeof(LogRecord) + 5u && trimmed.records.front()->truncated &&
              is_valid_utf8(trimmed.records.front()->message), "Oversized records are bounded and preserve UTF-8");
        tiny.append(record);
        check(tiny.snapshot().records.size() == 1u && tiny.snapshot().evicted_records == 1u,
              "Byte budget evicts even before the record-count limit is reached");
        buffer.configure_file(true, "fixture.log", true);
        buffer.report_output_error("disk failure", true);
        buffer.file_flushed();
        const auto revision = buffer.revision();
        buffer.report_output_error("disk failure", true);
        check(!buffer.snapshot().file_ready && buffer.revision() > revision,
              "Repeated failure after recovery updates health even when diagnostic is deduplicated");
    }

    toy3d::LogConfig config_for(const std::filesystem::path& root, const char* name,
                                const std::shared_ptr<toy3d::LogBuffer>& buffer)
    {
        toy3d::LogConfig config;
        config.logger_name = name; config.console_output = false;
        config.log_directory = root; config.file_name = std::string(name) + ".log";
        config.memory_output = buffer;
        return config;
    }

    void fanout_and_concurrency(const std::filesystem::path& root)
    {
        using namespace toy3d;
        auto buffer = std::make_shared<LogBuffer>();
        auto config = config_for(root, "fanout", buffer);
        auto& logger = Logger::get_instance();
        check(logger.init(config), "File and memory sinks initialize together");
        TOY_LOG_TRACE("trace fixture"); TOY_LOG_DEBUG("debug fixture"); TOY_LOG_INFO("startup fixture");
        TOY_LOG_WARN("warning fixture"); TOY_LOG_ERROR("material assignment fixture"); TOY_LOG_CRITICAL("critical fixture");
        constexpr std::size_t producer_count = 4u;
        constexpr std::size_t records_per_producer = 200u;
        std::atomic<bool> stop{false}, ordered{true};
        std::thread reader([&]()
        {
            while (!stop.load())
            {
                const auto snapshot = buffer->snapshot();
                std::uint64_t previous = 0;
                for (const auto& record : snapshot.records)
                { if (record->sequence <= previous) ordered.store(false); previous = record->sequence; }
            }
        });
        std::vector<std::thread> producers;
        for (std::size_t producer = 0; producer < producer_count; ++producer)
            producers.emplace_back([producer, records_per_producer]()
            { for (std::size_t index = 0; index < records_per_producer; ++index) TOY_LOG_INFO("worker {} record {}", producer, index); });
        for (auto& producer : producers) producer.join();
        stop.store(true); reader.join();
        logger.exit();
        const auto snapshot = buffer->snapshot();
        check(ordered.load() && snapshot.records.size() == log_level_count + producer_count * records_per_producer,
              "Concurrent producers and snapshots lose no records and preserve ordering");
        check(snapshot.records.front()->source_line > 0 && !snapshot.records.front()->source_file.empty() &&
              snapshot.records.front()->logger_name == "fanout", "Source and logger metadata are copied");
        const std::string file = read_file(root / "fanout.log");
        check(file.find("startup fixture") != std::string::npos && file.find("material assignment fixture") != std::string::npos &&
              file.find("worker 3 record 199") != std::string::npos && file.find("trace fixture") != std::string::npos,
              "File retains all levels and final flush retains worker output");
        TOY_LOG_ERROR("after exit");
        check(buffer->snapshot().last_sequence == snapshot.last_sequence, "Logging after shutdown cannot touch released outputs");

        config.file_output = false;
        check(logger.init(config), "Memory-only logger reinitializes after shutdown");
        TOY_LOG_ERROR("broken format {", 1);
        check(!buffer->snapshot().output_error.empty(), "Formatter failures report without recursive logging");
        std::atomic<bool> writing{false};
        std::thread racing([&]() { writing.store(true); for (int i = 0; i < 1000; ++i) TOY_LOG_INFO("shutdown race {}", i); });
        while (!writing.load()) std::this_thread::yield();
        logger.exit(); racing.join();
        check(buffer->snapshot().last_sequence >= snapshot.last_sequence, "Shutdown is synchronized with active producers");
    }

    void unicode_file_output(const std::filesystem::path& root)
    {
        using namespace toy3d;
        // C++17 filesystem preserves the project's Unicode directory at the I/O boundary.
        const auto directory = root / std::filesystem::u8path(u8"日志-🧪");
        auto buffer = std::make_shared<LogBuffer>();
        auto config = config_for(directory, "unicode", buffer);
        config.file_name = u8"编辑器-🧪.log";
        config.max_file_size = 256u;
        config.max_file_count = 1u;
        auto& logger = Logger::get_instance();
        check(logger.init(config), "Unicode project directory and filename initialize losslessly");
        TOY_LOG_WARN("{}", std::string(180u, 'a'));
        TOY_LOG_ERROR("Unicode project final record");
        logger.exit();
        const auto snapshot = buffer->snapshot();
        check(snapshot.file_ready && snapshot.output_error.empty(), "Unicode file writes and rotation stay healthy");
        check(read_file(directory / std::filesystem::u8path(config.file_name)).find("Unicode project final record") != std::string::npos &&
              std::filesystem::is_regular_file(directory / std::filesystem::u8path(u8"编辑器-🧪.1.log")),
              "Unicode primary and rotated logs are accessible by their original native paths");
    }

    void file_failure_and_rotation(const std::filesystem::path& root)
    {
        using namespace toy3d;
        auto buffer = std::make_shared<LogBuffer>();
        auto& logger = Logger::get_instance();
        const auto blocked = root / "not-a-directory";
        { std::ofstream output(blocked); output << "fixture"; }
        auto config = config_for(blocked, "startup-failure", buffer);
        std::string error;
        check(!logger.init(config, &error) && !error.empty(), "Unwritable file setup returns a diagnostic");
        TOY_LOG_ERROR("file failed but Console still receives this");
        check(!buffer->snapshot().file_ready && buffer->snapshot().records.back()->message.find("Console still") != std::string::npos,
              "File creation failure preserves the independent memory output");
        logger.exit();

        buffer = std::make_shared<LogBuffer>();
        config = config_for(root, "rotation", buffer); config.max_file_size = 256u; config.max_file_count = 1u;
        check(logger.init(config), "Rotation fixture initializes");
        const auto rotation_target = root / "rotation.1.log";
        std::filesystem::create_directory(rotation_target);
        { std::ofstream output(rotation_target / "hold"); output << "prevent directory removal"; }
        TOY_LOG_WARN("{}", std::string(100u, 'a'));
        TOY_LOG_ERROR("{}", std::string(150u, 'b'));
        auto snapshot = buffer->snapshot();
        check(!snapshot.file_ready && !snapshot.output_error.empty() && snapshot.records.size() >= 3u,
              "Rotation I/O failure remains visible while the failed message is still captured");
        // This fixed fixture is exclusively below the CMake test-output root.
        std::filesystem::remove_all(rotation_target);
        TOY_LOG_WARN("rotation recovered");
        check(buffer->snapshot().file_ready, "A successful write and flush restores file health");
        TOY_LOG_WARN("{}", std::string(180u, 'c'));
        logger.exit();
        check(std::filesystem::is_regular_file(rotation_target) && std::filesystem::is_regular_file(root / "rotation.log"),
              "Rolling output resumes and produces the configured backup file");
    }
}

int main()
{
    // C++17 filesystem isolates native I/O fixtures beneath CMake's build directory.
    const std::filesystem::path root = std::filesystem::u8path(TOY3D_LOGGING_TEST_ROOT);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    buffer_contract();
    fanout_and_concurrency(root);
    unicode_file_output(root);
    file_failure_and_rotation(root);
    std::cout << "Logging checks: " << (failures == 0 ? "passed" : "failed") << '\n';
    return failures == 0 ? 0 : 1;
}

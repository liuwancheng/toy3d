#include "panels/console_panel.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include "imgui.h"
#include "imgui_internal.h"
#include "logging/logger.h"
#include "panels/editor_panel_registry.h"

namespace
{
    int failures = 0;
    void check(bool result, const char* message)
    { if (!result) { ++failures; std::cerr << "FAILED: " << message << '\n'; } }

    void filter_contract()
    {
        using namespace toy3d;
        ConsoleLogFilter filter;
        LogRecord record; record.message = "Material assignment rejected"; record.source_file = "mesh_component_editor.cpp";
        for (std::size_t level = 0; level < log_level_count; ++level)
        {
            record.level = static_cast<LogLevel>(level);
            check(filter.matches(record) == (level >= static_cast<std::size_t>(LogLevel::TOY_INFO)), "Default levels match Info and above");
        }
        filter.errors_only();
        record.level = LogLevel::TOY_ERROR;
        std::strcpy(filter.search.data(), "MATERIAL");
        check(filter.matches(record), "Text search is case-insensitive");
        record.level = LogLevel::TOY_WARN;
        check(!filter.matches(record), "Text match cannot bypass an unchecked level");
        record.level = LogLevel::TOY_CRITICAL;
        std::strcpy(filter.search.data(), "mesh_component");
        check(filter.matches(record), "Source search and Critical filtering combine");
        std::strcpy(filter.search.data(), "unknown");
        check(!filter.matches(record), "Checked level cannot bypass text search");
        record.level = LogLevel::TOY_OFF;
        check(!filter.matches(record), "Off is not a display level");
    }

    void panel_interaction()
    {
        using namespace toy3d;
        const auto buffer = std::make_shared<LogBuffer>();
        // C++17 filesystem keeps UI/file integration fixtures below the build root.
        const auto root = std::filesystem::u8path(TOY3D_CONSOLE_TEST_ROOT);
        std::filesystem::remove_all(root);
        LogConfig config; config.logger_name = "ConsoleUi"; config.console_output = false;
        config.memory_output = buffer; config.log_directory = root; config.file_name = "console.log";
        check(Logger::get_instance().init(config), "Console session has file and memory outputs");
        TOY_LOG_TRACE("row-Trace"); TOY_LOG_DEBUG("row-Debug"); TOY_LOG_INFO("row-Info");
        TOY_LOG_WARN("row-Warning"); TOY_LOG_ERROR("row-Error"); TOY_LOG_CRITICAL("row-Critical");
        ConsolePanel console(buffer);
        EditorPanelRegistry registry;
        int undo_count = 0;
        EditorPanel scene; scene.id = "scene"; scene.title = "Scene"; scene.window_name = "Scene";
        scene.draw = []() {}; scene.focused = []() { return true; };
        scene.undo = [&]() { ++undo_count; }; scene.redo = []() {};
        check(registry.add(std::move(scene)), "Scene history target registers");
        EditorPanel panel; panel.id = "console"; panel.title = "Console"; panel.window_name = "Console";
        panel.draw = [&]() { console.draw(); }; panel.open = [&]() { console.open(); };
        check(registry.add(std::move(panel)), "Console registers with reopen callback"); registry.freeze();

        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr; io.DisplaySize = ImVec2(1200, 1000); io.DeltaTime = 1.0f / 60.0f;
        io.ConfigInputTrickleEventQueue = false;
        unsigned char* pixels = nullptr; int width = 0, height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        std::string rendered;
        // End() finishes ImGui logging before draw() returns. Capture through an
        // in-memory clipboard callback so the test never touches the OS clipboard.
        io.ClipboardUserData = &rendered;
        io.SetClipboardTextFn = [](void* data, const char* text) { *static_cast<std::string*>(data) = text; };
        io.GetClipboardTextFn = [](void* data) { return static_cast<std::string*>(data)->c_str(); };
        const auto frame = [&](bool capture = false)
        {
            ImGui::NewFrame();
            if (capture) ImGui::LogToClipboard();
            ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ImVec2(1180, 680));
            registry.draw();
            ImGui::SetNextWindowPos(ImVec2(0, 710)); ImGui::SetNextWindowSize(ImVec2(300, 150));
            ImGui::Begin("Window Menu"); registry.draw_window_menu(); ImGui::End();
            const ImGuiID hovered = ImGui::GetCurrentContext()->HoveredId;
            if (capture) ImGui::LogFinish();
            ImGui::Render();
            return hovered;
        };
        frame(); frame(); frame(true);
        check(rendered.find("row-Info") != std::string::npos && rendered.find("row-Error") != std::string::npos &&
              rendered.find("row-Trace") == std::string::npos && rendered.find("row-Debug") == std::string::npos,
              "Actual Console draws startup entries with its default filters");
        const auto click = [&](ImGuiID id, float min_y, float max_y)
        {
            // Resolve widgets by stable ImGui identity, independently of their count labels.
            for (float y = min_y; y < max_y; y += 5.0f)
                for (float x = 10; x < 1180; x += 10.0f)
                {
                    io.AddMousePosEvent(x, y);
                    if (frame() != id) continue;
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, true); frame();
                    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false); frame();
                    return true;
                }
            return false;
        };
        const char* names[log_level_count] = {"Trace", "Debug", "Info", "Warning", "Error", "Critical"};
        for (std::size_t level = 0; level < log_level_count; ++level)
        {
            auto* window = ImGui::FindWindowByName("Console");
            const std::string label = std::string("###") + names[level];
            const ImGuiID id = window->GetID(label.c_str());
            check(click(id, 25, 55), "Actual log-level checkbox is clickable"); frame(true);
            const bool shown = rendered.find(std::string("row-") + names[level]) != std::string::npos;
            check(shown == (level < static_cast<std::size_t>(LogLevel::TOY_INFO)), "Checkbox toggles its own level independently");
            check(click(id, 25, 55), "Checkbox restores its previous state");
        }
        auto* window = ImGui::FindWindowByName("Console");
        check(click(window->GetID("Errors Only"), 50, 80), "Errors Only preset is clickable"); frame(true);
        check(rendered.find("row-Error") != std::string::npos && rendered.find("row-Critical") != std::string::npos &&
              rendered.find("row-Info") == std::string::npos && rendered.find("row-Warning") == std::string::npos,
              "Errors Only shows Error and Critical");
        check(click(window->GetID("All"), 50, 80), "All preset is clickable"); frame(true);
        check(rendered.find("row-Trace") != std::string::npos && rendered.find("row-Debug") != std::string::npos,
              "All restores verbose records captured while hidden");
        check(click(window->GetID("Clear Display"), 50, 80), "Clear Display is clickable"); frame(true);
        check(rendered.find("row-Error") == std::string::npos && console.error_count() == 0 && buffer->snapshot().records.size() == log_level_count,
              "Clear advances only the panel cursor and preserves captured history");
        check(click(window->GetID("#CLOSE"), 5, 25), "Console close button is clickable"); frame();
        TOY_LOG_ERROR("arrived while closed");
        check(console.error_count() == 1, "Closed panel continues receiving errors");
        auto* menu = ImGui::FindWindowByName("Window Menu");
        check(click(menu->GetID("Console"), 745, 790), "Window menu reopens Console through registered callback"); frame(true);
        check(rendered.find("arrived while closed") != std::string::npos, "Reopened Console displays logs collected while closed");
        registry.undo(); check(undo_count == 1, "Console focus does not steal the scene's Undo target");
        Logger::get_instance().exit();
        std::ifstream input(root / "console.log", std::ios::binary);
        const std::string file(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>{});
        check(file.find("row-Trace") != std::string::npos && file.find("row-Error") != std::string::npos &&
              file.find("arrived while closed") != std::string::npos, "Checkboxes, clear and window close do not alter file output");
        ImGui::DestroyContext();
    }
}

int main()
{
    filter_contract(); panel_interaction();
    std::cout << "Console checks: " << (failures == 0 ? "passed" : "failed") << '\n';
    return failures == 0 ? 0 : 1;
}

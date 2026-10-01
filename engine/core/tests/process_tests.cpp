#include "process/process.h"

#include <iostream>
#include <chrono>
#include <thread>
#include "platform/platform_defines.h"

#if WITH_WIN
#include <Windows.h>
#endif

namespace
{
    bool check(bool passed, const char* name)
    { if (!passed) std::cerr << "FAIL: " << name << '\n'; return passed; }
}
int main(int count, char** values)
{
    if (count != 2) return 2;
    const toy3d::NativeProcessService process;
    const toy3d::PhysicalPath helper(values[1]);
    const std::vector<std::string> arguments = {"", "hello world", "quote\"here", "ends \\", "中文", "$(not-a-shell);&"};
    std::vector<std::string> input = {"echo"}; input.insert(input.end(), arguments.begin(), arguments.end());
    std::string expected; for (const auto& argument : arguments) expected += std::to_string(argument.size()) + ":" + argument + "\n";
    const auto echoed = process.run(helper, input);
#if WITH_WIN
    std::string normalized; for (const auto character : echoed.output) if (character != '\r') normalized.push_back(character);
#else
    const std::string& normalized = echoed.output;
#endif
    bool passed = check(echoed.succeeded() && normalized == expected, "exact argv including empty and UTF-8");
    const auto failed = process.run(helper, {"fail"});
    passed &= check(failed.launched && failed.error == toy3d::ProcessError::None && failed.exit_code == 7 && failed.output == "failure", "nonzero exit");
    const auto missing = process.run(toy3d::PhysicalPath(helper.utf8() + ".missing"), {});
    passed &= check(!missing.launched && missing.error == toy3d::ProcessError::Launch, "missing executable");
    toy3d::ProcessRunOptions options; options.maximum_output_bytes = 123u;
    const auto flood = process.run(helper, {"flood"}, options);
    passed &= check(flood.succeeded() && flood.output_truncated && flood.output.size() == 123u, "drain oversized output");
    options.timeout_ms = 50u;
    const auto timed = process.run(helper, {"sleep"}, options);
    passed &= check(timed.launched && timed.error == toy3d::ProcessError::Timeout, "timeout reaps owned process");
    std::atomic<bool> cancelled{true}; options.cancel = &cancelled;
    const auto stopped = process.run(helper, {"sleep"}, options);
    passed &= check(!stopped.launched && stopped.error == toy3d::ProcessError::Cancelled, "cancel before launch");
    cancelled.store(false); options.timeout_ms = 5000u;
    std::thread cancellation([&cancelled]() { std::this_thread::sleep_for(std::chrono::milliseconds(500)); cancelled.store(true); });
    const auto running_cancelled = process.run(helper, {"sleep"}, options);
    cancellation.join();
    passed &= check(running_cancelled.launched && running_cancelled.error == toy3d::ProcessError::Cancelled, "cancel running process");
    options.cancel = nullptr;
#if WITH_WIN
    options.timeout_ms = 500u; options.maximum_output_bytes = 1024u;
    const auto tree = process.run(helper, {"tree"}, options);
    passed &= check(tree.launched && tree.error == toy3d::ProcessError::Timeout && !tree.output.empty(), "timeout child process tree");
    if (!tree.output.empty())
    {
        const DWORD child_id = static_cast<DWORD>(std::stoul(tree.output));
        HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, child_id);
        if (child)
        {
            passed &= check(WaitForSingleObject(child, 2000u) == WAIT_OBJECT_0, "descendant terminated by process job");
            passed &= check(CloseHandle(child) != FALSE, "close descendant verification handle");
        }
        else passed &= check(GetLastError() == ERROR_INVALID_PARAMETER, "descendant already reaped");
    }
#endif
    const auto invalid = process.run(helper, {std::string("nul\0value", 9u)});
    passed &= check(!invalid.launched && invalid.error == toy3d::ProcessError::InvalidArgument, "NUL argument rejected");
    const auto detached = process.launch_detached(toy3d::PhysicalPath(helper.utf8() + ".missing"), {});
    passed &= check(!detached.launched && detached.error == toy3d::ProcessError::Launch, "detached launch failure");
    return passed ? 0 : 1;
}

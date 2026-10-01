#include "process/process.h"

#include <chrono>
#include <iostream>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

// Compile the actual implementation into this Windows-only fault-path test.
// Restricted real handles exercise private cleanup without public test hooks
// or a parallel mock implementation of the process lifecycle.
#include "../process.cpp"

namespace
{
    constexpr DWORD verification_timeout_ms = 5000u;

    bool check(bool condition, const char* message)
    {
        if (!condition) std::cerr << "FAILED: " << message << '\n';
        return condition;
    }

    bool check_uncontained_cleanup(const wchar_t* executable, DWORD access, bool expect_timeout)
    {
        std::wstring command = L"\"" + std::wstring(executable) + L"\" sleep";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &info))
            return check(false, "create suspended process");

        toy3d::NativeHandle process(info.hProcess);
        toy3d::NativeHandle thread(info.hThread);
        // A separate test-owned Job also kills this suspended root if a
        // regression hangs the test and CTest terminates the test executable.
        toy3d::NativeHandle containment(CreateJobObjectW(nullptr, nullptr));
        bool passed = check(containment.get() != nullptr, "create fault-test containment job");
        if (passed)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            passed &= check(SetInformationJobObject(containment.get(), JobObjectExtendedLimitInformation,
                                                   &limits, sizeof(limits)) != FALSE,
                            "configure fault-test containment");
            passed &= check(AssignProcessToJobObject(containment.get(), process.get()) != FALSE,
                            "contain fault-test process");
        }
        HANDLE restricted_value = nullptr;
        passed &= check(DuplicateHandle(GetCurrentProcess(), process.get(), GetCurrentProcess(),
                                        &restricted_value, access, FALSE, 0) != FALSE,
                        "create restricted process handle");
        toy3d::NativeHandle restricted(restricted_value);
        toy3d::NativeHandle job(CreateJobObjectW(nullptr, nullptr));
        passed &= check(job.get() != nullptr, "create unassigned process job");
        if (passed)
        {
            toy3d::ProcessResult result;
            result.error = toy3d::ProcessError::Launch;
            result.message = "Original assignment failure.";
            const auto started = std::chrono::steady_clock::now();
            toy3d::finish_windows_process(restricted.get(), job, false, result);
            const auto elapsed = std::chrono::steady_clock::now() - started;
            passed &= check(elapsed < std::chrono::milliseconds(verification_timeout_ms),
                            "failed cleanup must return within a finite deadline");
            passed &= check(result.error == toy3d::ProcessError::Launch &&
                            result.message.find("Original assignment failure.") == 0,
                            "cleanup preserves original failure");
            passed &= check(result.message.find("Terminate uncontained process failed") != std::string::npos,
                            "termination failure is diagnosed");
            passed &= check(result.message.find(expect_timeout ? "exit was not confirmed" : "Reap process failed") !=
                            std::string::npos, "timeout or wait failure is diagnosed");
            passed &= check(result.exit_code == -1, "unconfirmed exit must not publish an exit code");
            passed &= check(job.get() == nullptr, "job is closed before returning from cleanup");
            passed &= check(WaitForSingleObject(process.get(), 0u) == WAIT_TIMEOUT,
                            "restricted cleanup must not pretend the suspended root exited");
        }

        // This test retains a full-access owner so the deliberately uncontained
        // suspended process is terminated even when the tested path fails.
        const bool terminated = TerminateProcess(process.get(), 1u) != FALSE;
        passed &= check(terminated, "terminate fault-test process with full-access owner");
        passed &= check(WaitForSingleObject(process.get(), verification_timeout_ms) == WAIT_OBJECT_0,
                        "fault-test process termination confirmed");
        return passed;
    }
}

int wmain(int count, wchar_t** values)
{
    if (count != 2) return 2;
    // Missing PROCESS_TERMINATE causes a real failure; SYNCHRONIZE lets the
    // first case time out. The second handle also lacks SYNCHRONIZE.
    bool passed = check_uncontained_cleanup(values[1], PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, true);
    passed &= check_uncontained_cleanup(values[1], PROCESS_QUERY_LIMITED_INFORMATION, false);
    return passed ? 0 : 1;
}

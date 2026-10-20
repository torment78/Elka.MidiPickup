#pragma once
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace elka::audio_reset {
// Fixed helper mode: no caller-selected service, executable, script or output path.
inline constexpr auto HelperArgument = L"--reset-windows-audio";
enum class Result : DWORD { Success, NotElevated, Busy, PreflightFailed, StopFailed, RestoreFailed, Cancelled, LaunchFailed };
const wchar_t* Describe(Result result);
int RunElevated();

enum class Stage { Idle, Preparing, Windows, Delay, Recovering };
class Sequence {
    Stage stage{Stage::Idle};
    uint64_t due{};
public:
    Stage Current() const { return stage; }
    bool Busy() const { return stage != Stage::Idle; }
    bool Begin() { if (Busy()) return false; stage = Stage::Preparing; return true; }
    void WindowsStarted() { if (stage == Stage::Preparing) stage = Stage::Windows; }
    void WindowsFinished(bool success, uint64_t now) {
        if (stage != Stage::Windows) return;
        stage = success ? Stage::Delay : Stage::Idle; due = now + 1000;
    }
    bool TakeVoiceMeeterRestart(uint64_t now) {
        if (stage != Stage::Delay || now < due) return false;
        stage = Stage::Recovering; due = now + 1000; return true;
    }
    bool ReadyToRead(uint64_t now) const { return stage == Stage::Recovering && now >= due; }
    void Finish() { stage = Stage::Idle; }
};

// The plan is children first, Windows Audio last. Every attempted stop is restored,
// including a stop that failed or timed out. Initially stopped services are absent.
template<class Stop, class Start>
Result RestartPlan(size_t count, Stop stop, Start start) {
    size_t attempted{};
    bool stopped = true;
    for (size_t i = 0; i < count; ++i) {
        attempted = i + 1;
        if (!stop(i)) { stopped = false; break; }
    }
    bool restored = true;
    while (attempted) if (!start(--attempted)) restored = false;
    return !restored ? Result::RestoreFailed : stopped ? Result::Success : Result::StopFailed;
}

class Job {
    std::jthread worker;
    std::atomic<bool> done{};
    std::atomic<Result> result{Result::LaunchFailed};
public:
    ~Job() { worker.request_stop(); }
    void Start(HWND owner);
    bool Done() const { return done.load(); }
    Result Outcome() const { return result.load(); }
};
}

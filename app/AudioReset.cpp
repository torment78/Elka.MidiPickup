#include "AudioReset.h"
#include <shellapi.h>
#include <objbase.h>
#include <string>
#include <memory>
#include <set>

namespace elka::audio_reset {
namespace {
struct ServiceCloser { void operator()(SC_HANDLE value) const { if (value) CloseServiceHandle(value); } };
using Service = std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, ServiceCloser>;
struct HandleCloser { void operator()(HANDLE value) const { if (value) CloseHandle(value); } };
using Handle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;
struct Entry { std::wstring name; Service service; };

bool Status(SC_HANDLE service, SERVICE_STATUS_PROCESS& status) {
    DWORD size{};
    return QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
        reinterpret_cast<BYTE*>(&status), sizeof(status), &size) != FALSE;
}
bool Wait(SC_HANDLE service, DWORD state) {
    const auto deadline = GetTickCount64() + 15000;
    do {
        SERVICE_STATUS_PROCESS status{};
        if (!Status(service, status)) return false;
        if (status.dwCurrentState == state) return true;
        Sleep(50);
    } while (GetTickCount64() < deadline);
    return false;
}
bool Plan(SC_HANDLE manager, const std::wstring& name, std::vector<Entry>& plan, std::set<std::wstring>& seen) {
    if (!seen.insert(name).second) return true;
    if (seen.size() > 16) return false;
    Service service(OpenServiceW(manager, name.c_str(),
        SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS | SERVICE_STOP | SERVICE_START));
    SERVICE_STATUS_PROCESS status{};
    if (!service || !Status(service.get(), status) || status.dwCurrentState != SERVICE_RUNNING ||
        !(status.dwControlsAccepted & SERVICE_ACCEPT_STOP)) return false;
    DWORD needed{}, count{};
    if (!EnumDependentServicesW(service.get(), SERVICE_ACTIVE, nullptr, 0, &needed, &count)) {
        if (GetLastError() != ERROR_MORE_DATA || !needed || needed > 1024 * 1024) return false;
        std::vector<BYTE> storage(needed);
        const auto entries = reinterpret_cast<ENUM_SERVICE_STATUSW*>(storage.data());
        if (!EnumDependentServicesW(service.get(), SERVICE_ACTIVE, entries, needed, &needed, &count)) return false;
        for (DWORD i = 0; i < count; ++i) {
            if (entries[i].ServiceStatus.dwCurrentState != SERVICE_RUNNING ||
                !Plan(manager, entries[i].lpServiceName, plan, seen)) return false;
        }
    }
    plan.push_back({name, std::move(service)});
    return true;
}
bool Stop(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS current{};
    if (!Status(service, current)) return false;
    if (current.dwCurrentState == SERVICE_STOPPED) return true;
    if (current.dwCurrentState == SERVICE_STOP_PENDING) return Wait(service, SERVICE_STOPPED);
    SERVICE_STATUS status{};
    if (!ControlService(service, SERVICE_CONTROL_STOP, &status)) return false;
    return Wait(service, SERVICE_STOPPED);
}
bool Start(SC_HANDLE service) {
    SERVICE_STATUS_PROCESS status{};
    if (!Status(service, status)) return false;
    if (status.dwCurrentState == SERVICE_STOP_PENDING) {
        if (!Wait(service, SERVICE_STOPPED) || !Status(service, status)) return false;
    }
    if (status.dwCurrentState == SERVICE_RUNNING) return true;
    if (status.dwCurrentState == SERVICE_START_PENDING) return Wait(service, SERVICE_RUNNING);
    if (status.dwCurrentState != SERVICE_STOPPED) return false;
    if (!StartServiceW(service, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) return false;
    return Wait(service, SERVICE_RUNNING);
}
}

int RunElevated() {
    HANDLE rawToken{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) return static_cast<int>(Result::NotElevated);
    Handle token(rawToken); TOKEN_ELEVATION elevation{}; DWORD size{};
    if (!GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &size) || !elevation.TokenIsElevated)
        return static_cast<int>(Result::NotElevated);
    Handle mutex(CreateMutexW(nullptr, TRUE, L"Local\\ElkaMidiPickupAudioReset"));
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) return static_cast<int>(Result::Busy);
    Service manager(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    std::vector<Entry> plan; std::set<std::wstring> seen;
    // All allocations and access checks happen before the first service stop.
    if (!manager || !Plan(manager.get(), L"Audiosrv", plan, seen)) return static_cast<int>(Result::PreflightFailed);
    const auto result = RestartPlan(plan.size(),
        [&](size_t i) { return Stop(plan[i].service.get()); },
        [&](size_t i) { return Start(plan[i].service.get()); });
    ReleaseMutex(mutex.get());
    return static_cast<int>(result);
}

const wchar_t* Describe(Result result) {
    switch (result) {
    case Result::Success: return L"Windows Audio is running again.";
    case Result::Cancelled: return L"Audio reset cancelled at administrator approval. VoiceMeeter was not restarted.";
    case Result::NotElevated: return L"Windows Audio reset requires administrator approval.";
    case Result::Busy: return L"Another audio reset is already running. VoiceMeeter was not restarted.";
    case Result::PreflightFailed: return L"Windows Audio or a dependent service is unavailable or cannot be stopped. Nothing was reset.";
    case Result::StopFailed: return L"Windows Audio could not be stopped. Affected services were restored; VoiceMeeter was not restarted.";
    case Result::RestoreFailed: return L"An audio service could not be restored. Check Windows Services before retrying. VoiceMeeter was not restarted.";
    default: return L"The audio reset helper did not complete successfully. Check Windows audio before retrying.";
    }
}

void Job::Start(HWND owner) {
    done = false; result = Result::LaunchFailed;
    worker = std::jthread([this, owner](std::stop_token stop) {
        const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        wchar_t executable[32768]{};
        const auto length = GetModuleFileNameW(nullptr, executable, 32768);
        if (length && length < 32768) {
            SHELLEXECUTEINFOW launch{sizeof(launch)};
            launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
            launch.hwnd = owner; launch.lpVerb = L"runas"; launch.lpFile = executable;
            launch.lpParameters = HelperArgument; launch.nShow = SW_HIDE;
            if (!ShellExecuteExW(&launch)) result = GetLastError() == ERROR_CANCELLED ? Result::Cancelled : Result::LaunchFailed;
            else {
                Handle process(launch.hProcess);
                if (process) {
                    while (!stop.stop_requested()) {
                        const auto waited = WaitForSingleObject(process.get(), 100);
                        if (waited == WAIT_OBJECT_0) {
                            DWORD code{};
                            if (GetExitCodeProcess(process.get(), &code) && code <= static_cast<DWORD>(Result::RestoreFailed))
                                result = static_cast<Result>(code);
                            break;
                        }
                        if (waited == WAIT_FAILED) break;
                    }
                }
            }
        }
        if (SUCCEEDED(apartment)) CoUninitialize();
        done = true;
    });
}
}

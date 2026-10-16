#include "Probe.h"
#include <Windows.h>
#include <mmsystem.h>
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <cwchar>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {
std::wstring RegistryString(HKEY root, const wchar_t* key, const wchar_t* name) {
    wchar_t value[512]{};
    DWORD bytes = sizeof(value);
    return RegGetValueW(root, key, name, RRF_RT_REG_SZ, nullptr, value, &bytes) == ERROR_SUCCESS
        ? std::wstring(value) : L"unavailable";
}

DWORD RegistryDword(const wchar_t* key, const wchar_t* name, DWORD fallback = 0) {
    DWORD value{}, bytes = sizeof(value);
    return RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_DWORD, nullptr, &value, &bytes) == ERROR_SUCCESS
        ? value : fallback;
}

void PrintMidiError(const wchar_t* action, MMRESULT result) {
    wchar_t text[MAXERRORLENGTH]{};
    midiInGetErrorTextW(result, text, MAXERRORLENGTH);
    std::wcout << action << L": " << result << L" " << text << L'\n';
}

std::optional<UINT> FindInput(const std::wstring& name) {
    std::optional<UINT> result;
    for (UINT i = 0; i < midiInGetNumDevs(); ++i) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR && name == caps.szPname) {
            if (result) throw std::runtime_error("Multiple inputs have that name; disconnect the duplicate before testing.");
            result = i;
        }
    }
    return result;
}

void ListPorts() {
    std::wcout << L"\nWinMM input ports (the legacy port list):\n";
    for (UINT i = 0; i < midiInGetNumDevs(); ++i) {
        MIDIINCAPSW caps{};
        auto result = midiInGetDevCapsW(i, &caps, sizeof(caps));
        if (result == MMSYSERR_NOERROR) std::wcout << L"  [" << i << L"] " << caps.szPname << L'\n';
        else PrintMidiError(L"  Input enumeration failed", result);
    }
    std::wcout << L"WinMM output ports:\n";
    for (UINT i = 0; i < midiOutGetNumDevs(); ++i) {
        MIDIOUTCAPSW caps{};
        auto result = midiOutGetDevCapsW(i, &caps, sizeof(caps));
        if (result == MMSYSERR_NOERROR) std::wcout << L"  [" << i << L"] " << caps.szPname << L'\n';
        else PrintMidiError(L"  Output enumeration failed", result);
    }
}

void ReadVoiceMeeter() {
    wchar_t programFiles[32768]{};
    const auto length = GetEnvironmentVariableW(L"ProgramFiles(x86)", programFiles, 32768);
    if (length == 0 || length >= 32768) { std::wcout << L"VoiceMeeter: installation directory unavailable\n"; return; }
    const auto dll = std::filesystem::path(programFiles) / L"VB" / L"Voicemeeter" / L"VoicemeeterRemote64.dll";
    HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) { std::wcout << L"VoiceMeeter Remote API unavailable (Windows error " << GetLastError() << L")\n"; return; }
    using Login = LONG(__stdcall*)();
    using GetType = LONG(__stdcall*)(LONG*);
    using GetFloat = LONG(__stdcall*)(char*, float*);
    const auto login = reinterpret_cast<Login>(GetProcAddress(module, "VBVMR_Login"));
    const auto logout = reinterpret_cast<Login>(GetProcAddress(module, "VBVMR_Logout"));
    const auto dirty = reinterpret_cast<Login>(GetProcAddress(module, "VBVMR_IsParametersDirty"));
    const auto type = reinterpret_cast<GetType>(GetProcAddress(module, "VBVMR_GetVoicemeeterType"));
    const auto gain = reinterpret_cast<GetFloat>(GetProcAddress(module, "VBVMR_GetParameterFloat"));
    if (!login || !logout || !dirty || !type || !gain) {
        std::wcout << L"VoiceMeeter: required read-only exports missing\n";
        FreeLibrary(module); return;
    }
    const LONG loginResult = login();
    if (loginResult >= 0) {
        if (loginResult == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            LONG edition{};
            const LONG typeResult = type(&edition);
            const LONG dirtyResult = dirty();
            std::wcout << L"VoiceMeeter Remote API login: OK; type=" << edition << L"; read status=" << dirtyResult << L'\n';
            if (typeResult == 0 && dirtyResult >= 0) {
                const int strips = edition == 3 ? 8 : edition == 2 ? 5 : 3;
                const int buses = edition == 3 ? 8 : edition == 2 ? 5 : 2;
                for (const auto& group : {std::pair{"Strip", strips}, std::pair{"Bus", buses}}) {
                    for (int i = 0; i < group.second; ++i) {
                        auto parameter = std::string(group.first) + "[" + std::to_string(i) + "].Gain";
                        float value{};
                        const auto result = gain(parameter.data(), &value);
                        std::wcout << L"  " << std::wstring(parameter.begin(), parameter.end()) << L": ";
                        if (result == 0) std::wcout << value << L" dB\n";
                        else std::wcout << L"read failed (" << result << L")\n";
                    }
                }
            }
        } else std::wcout << L"VoiceMeeter installed but not running; not launched\n";
        logout();
    } else std::wcout << L"VoiceMeeter login failed: " << loginResult << L'\n';
    FreeLibrary(module);
}

struct MidiInput {
    HMIDIIN handle{};
    ~MidiInput() { if (handle) { midiInStop(handle); midiInReset(handle); midiInClose(handle); } }
    MidiInput() = default;
    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;
};

struct MonitorState {
    std::atomic<unsigned long long> count{};
    std::atomic<DWORD> last{};
    std::atomic<unsigned long long> errors{};
};

void CALLBACK ReceiveMidi(HMIDIIN, UINT message, DWORD_PTR context, DWORD_PTR data, DWORD_PTR) {
    auto& state = *reinterpret_cast<MonitorState*>(context);
    if (message == MIM_DATA) {
        state.last.store(static_cast<DWORD>(data), std::memory_order_relaxed);
        state.count.fetch_add(1, std::memory_order_release);
    } else if (message == MIM_ERROR || message == MIM_MOREDATA) state.errors.fetch_add(1);
}

int InputTest(const std::wstring& name, unsigned seconds) {
    auto index = FindInput(name);
    if (!index) { std::wcout << L"Input not found: " << name << L'\n'; return 2; }
    MonitorState state;
    MidiInput first, second;
    auto result = midiInOpen(&first.handle, *index, reinterpret_cast<DWORD_PTR>(&ReceiveMidi), reinterpret_cast<DWORD_PTR>(&state), CALLBACK_FUNCTION | MIDI_IO_STATUS);
    if (result != MMSYSERR_NOERROR) { PrintMidiError(L"First open failed", result); return 2; }
    std::wcout << L"First open of " << name << L": PASS\n";
    result = midiInOpen(&second.handle, *index, 0, 0, CALLBACK_NULL);
    const bool shared = result == MMSYSERR_NOERROR;
    if (shared) std::wcout << L"Second simultaneous open: PASS\n";
    else PrintMidiError(L"Second simultaneous open failed", result);
    if (seconds > 0) {
        result = midiInStart(first.handle);
        if (result != MMSYSERR_NOERROR) { PrintMidiError(L"Input start failed", result); return 2; }
        std::wcout << L"Monitoring for " << seconds << L" seconds. Move an APC40 fader. No MIDI output is sent.\n";
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        unsigned long long seen{};
        while (std::chrono::steady_clock::now() < until) {
            const auto count = state.count.load(std::memory_order_acquire);
            if (count != seen) {
                seen = count;
                const auto data = state.last.load();
                std::wcout << L"  count=" << count << L" last=" << std::hex << std::setw(6) << std::setfill(L'0') << data << std::dec
                    << L" channel=" << ((data & 0xf) + 1) << L" data1=" << ((data >> 8) & 0x7f) << L" data2=" << ((data >> 16) & 0x7f) << L'\n';
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        midiInStop(first.handle);
        std::wcout << L"Received=" << state.count.load() << L" errors/overruns=" << state.errors.load() << L'\n';
        if (state.count.load() == 0) { std::wcout << L"Hardware message reception NOT VERIFIED (no activity).\n"; return 3; }
        if (state.errors.load() != 0) return 2;
    }
    return shared ? 0 : 2;
}
}

unsigned long WindowsBuild() {
    const auto text = RegistryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber");
    return std::wcstoul(text.c_str(), nullptr, 10);
}

std::wstring SystemFile(const wchar_t* name) {
    wchar_t path[MAX_PATH]{};
    GetSystemDirectoryW(path, MAX_PATH);
    return (std::filesystem::path(path) / name).wstring();
}

std::wstring FileVersion(const std::wstring& path) {
    DWORD ignored{};
    const DWORD bytes = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!bytes) return L"not present";
    std::vector<BYTE> data(bytes);
    if (!GetFileVersionInfoW(path.c_str(), 0, bytes, data.data())) return L"unavailable";
    VS_FIXEDFILEINFO* info{};
    UINT size{};
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &size) || size < sizeof(*info)) return L"unavailable";
    return std::to_wstring(HIWORD(info->dwFileVersionMS)) + L"." + std::to_wstring(LOWORD(info->dwFileVersionMS)) + L"."
        + std::to_wstring(HIWORD(info->dwFileVersionLS)) + L"." + std::to_wstring(LOWORD(info->dwFileVersionLS));
}

bool MidiServiceRunning() {
    const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) { std::wcout << L"MIDI service query: Windows error " << GetLastError() << L'\n'; return false; }
    const auto service = OpenServiceW(manager, L"midisrv", SERVICE_QUERY_STATUS);
    SERVICE_STATUS_PROCESS status{};
    DWORD bytes{};
    bool running = false;
    if (service) {
        if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&status), sizeof(status), &bytes)) {
            running = status.dwCurrentState == SERVICE_RUNNING;
            std::wcout << L"MIDI service state=" << status.dwCurrentState << L" (4=running), PID=" << status.dwProcessId << L'\n';
        } else std::wcout << L"MIDI service status unavailable: " << GetLastError() << L'\n';
        CloseServiceHandle(service);
    } else std::wcout << L"MIDI service unavailable: " << GetLastError() << L'\n';
    CloseServiceHandle(manager);
    return running;
}

int wmain(int argc, wchar_t** argv) {
    try {
        std::wcout << L"Elka MIDI Pickup - C++ compatibility probe 0.1.0\n";
        std::wstring command = argc > 1 ? argv[1] : L"--diagnose";
        int exitCode{};
        if (command == L"--diagnose") {
            const wchar_t* osKey = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
            std::wcout << L"Windows " << RegistryString(HKEY_LOCAL_MACHINE, osKey, L"DisplayVersion") << L" build " << WindowsBuild() << L"." << RegistryDword(osKey, L"UBR") << L'\n';
            MidiServiceRunning();
            std::wcout << L"UseLegacyMidi=" << RegistryDword(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Drivers32", L"UseLegacyMidi", 0xffffffff) << L" (4294967295=not configured)\n";
            for (auto file : {L"midisrv.exe", L"Midi2.VirtualMidiTransport.dll", L"Midi2.LoopbackMidiTransport.dll", L"Windows.Devices.Midi2.dll"})
                std::wcout << file << L": " << FileVersion(SystemFile(file)) << L'\n';
            ListPorts();
            ReadVoiceMeeter();
            std::wcout << L"\nVirtual endpoint creation: NOT TESTED. See docs/Compatibility.md for Microsoft #1047.\n";
        } else if (command == L"--input-open-test" || command == L"--monitor") {
            const std::wstring name = argc > 2 ? argv[2] : L"APC40 mkII";
            unsigned seconds = command == L"--monitor" ? 20 : 0;
            if (command == L"--monitor" && argc > 3) {
                wchar_t* end{};
                auto parsed = std::wcstoul(argv[3], &end, 10);
                if (!end || *end != L'\0' || parsed < 1 || parsed > 300) throw std::runtime_error("Monitor duration must be 1 through 300 seconds.");
                seconds = static_cast<unsigned>(parsed);
            }
            exitCode = InputTest(name, seconds);
        } else if (command == L"--native-probe") exitCode = RunNativeProbe();
        else {
            std::wcout << L"Usage: --diagnose | --input-open-test [exact input name] | --monitor [exact input name] [seconds 1..300] | --native-probe\n";
            return command == L"--help" ? 0 : 2;
        }
        PROCESS_MEMORY_COUNTERS_EX memory{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
            std::wcout << L"Probe working set=" << memory.WorkingSetSize / 1024 << L" KiB; private commit=" << memory.PrivateUsage / 1024 << L" KiB\n";
        return exitCode;
    } catch (const std::exception& error) { std::cerr << "Error: " << error.what() << '\n'; return 2; }
}

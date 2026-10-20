#pragma once
#include <Windows.h>
#include <mmsystem.h>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "MidiInbox.h"

namespace elka {
struct MidiPort { UINT id; std::wstring name; };
inline std::vector<MidiPort> MidiInputs() {
    std::vector<MidiPort> ports;
    for (UINT i = 0; i < midiInGetNumDevs(); ++i) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == 0) ports.push_back({i, caps.szPname});
    }
    return ports;
}
inline std::wstring MidiError(MMRESULT result) {
    wchar_t message[MAXERRORLENGTH]{};
    midiInGetErrorTextW(result, message, MAXERRORLENGTH);
    return message;
}
class MidiInput {
    MidiInbox inbox;
    HMIDIIN handle{};
    static void CALLBACK Receive(HMIDIIN, UINT event, DWORD_PTR pointer, DWORD_PTR data, DWORD_PTR) {
        auto& queue = *reinterpret_cast<MidiInbox*>(pointer);
        queue.Receive(event, static_cast<DWORD>(data), GetTickCount());
    }
public:
    std::wstring name;
    ~MidiInput() { Close(); }
    MidiInput() = default;
    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;
    bool Open() const { return handle != nullptr; }
    uint64_t Count() const { return inbox.count.load(); }
    uint64_t Errors() const { return inbox.errors.load(); }
    uint64_t Bursts() const { return inbox.bursts.load(); }
    uint64_t Dropped() const { return inbox.dropped.load(); }
    void Drain(MidiInbox::Batch& batch) { inbox.Drain(batch); }
    void Clear() { inbox.Clear(); }
    MMRESULT Connect(const MidiPort& port) {
        Close(); inbox.Reset();
        auto result = midiInOpen(&handle, port.id, reinterpret_cast<DWORD_PTR>(&Receive), reinterpret_cast<DWORD_PTR>(&inbox), CALLBACK_FUNCTION | MIDI_IO_STATUS);
        if (result == 0) result = midiInStart(handle);
        if (result != 0) Close(); else name = port.name;
        return result;
    }
    void Close() {
        if (handle) { midiInStop(handle); midiInReset(handle); midiInClose(handle); handle = nullptr; }
        inbox.Clear();
        name.clear();
    }
};

class VoiceMeeter {
    HMODULE module{};
    bool loggedIn{};
    using NoArgs = LONG(__stdcall*)();
    using GetLong = LONG(__stdcall*)(LONG*);
    using GetFloat = LONG(__stdcall*)(char*, float*);
    using SetFloat = LONG(__stdcall*)(char*, float);
    NoArgs login{}, logout{}, dirty{};
    GetLong getType{};
    GetFloat getFloat{};
    SetFloat setFloat{};
    using CustomButton = LONG(__stdcall*)(LONG, LONG, LONG, wchar_t*, HWND, LONG);
    CustomButton customButton{};
    HWND commandWindow{};
    bool buttonRegistered{};
    uint64_t nextButtonAttempt{};
public:
    // VoiceMeeter numbers its two custom display buttons from zero.
    // FX Host owns slot zero; MIDI Pickup only ever touches slot one.
    static constexpr LONG ButtonIndex = 1;
    static constexpr LONG ButtonCommand = 0x454d;
    LONG edition{};
    std::wstring error;
    ~VoiceMeeter() { Close(); }
    VoiceMeeter() = default;
    VoiceMeeter(const VoiceMeeter&) = delete;
    VoiceMeeter& operator=(const VoiceMeeter&) = delete;
    bool Connected() const { return loggedIn && edition >= 1 && edition <= 3; }
    int Strips() const { return edition == 3 ? 8 : edition == 2 ? 5 : 3; }
    int Buses() const { return edition == 3 ? 8 : edition == 2 ? 5 : 2; }
    bool ValidTarget(int target) const { return target >= 0 && (target < 8 ? target < Strips() : target < 8 + Buses()); }
    static std::string Parameter(int target) { return (target < 8 ? "Strip[" : "Bus[") + std::to_string(target % 8) + "].Gain"; }
    bool ButtonRegistered() const { return buttonRegistered; }
    bool ButtonSupported() const { return customButton && edition == 3; }
    LONG lastButtonRemoval{};
    bool Connect(HWND window) {
        Close(); error.clear();
        wchar_t directory[32768]{};
        if (!GetEnvironmentVariableW(L"ProgramFiles(x86)", directory, 32768)) { error = L"Program Files location unavailable."; return false; }
        const auto path = std::filesystem::path(directory) / L"VB" / L"Voicemeeter" / L"VoicemeeterRemote64.dll";
        module = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) { error = L"VoiceMeeter Remote DLL was not found. Install VoiceMeeter first."; return false; }
        login = reinterpret_cast<NoArgs>(GetProcAddress(module, "VBVMR_Login"));
        logout = reinterpret_cast<NoArgs>(GetProcAddress(module, "VBVMR_Logout"));
        dirty = reinterpret_cast<NoArgs>(GetProcAddress(module, "VBVMR_IsParametersDirty"));
        getType = reinterpret_cast<GetLong>(GetProcAddress(module, "VBVMR_GetVoicemeeterType"));
        getFloat = reinterpret_cast<GetFloat>(GetProcAddress(module, "VBVMR_GetParameterFloat"));
        setFloat = reinterpret_cast<SetFloat>(GetProcAddress(module, "VBVMR_SetParameterFloat"));
        customButton = reinterpret_cast<CustomButton>(GetProcAddress(module, "VBVMR_SetCustomButton"));
        if (!login || !logout || !dirty || !getType || !getFloat || !setFloat) { error = L"Required VoiceMeeter API functions are missing."; Close(); return false; }
        const auto result = login();
        loggedIn = result >= 0;
        if (result != 0) { error = L"Start VoiceMeeter, then connect again. Login code " + std::to_wstring(result); Close(); return false; }
        if (getType(&edition) != 0 || edition < 1 || edition > 3) { error = L"VoiceMeeter is not ready. Connect again."; Close(); return false; }
        commandWindow = window; nextButtonAttempt = GetTickCount64() + 2000;
        return true;
    }
    bool Refresh() {
        if (!Connected() || dirty() < 0) return false;
        LONG current{};
        if (getType(&current) != 0 || current != edition) return false;
        const auto now = GetTickCount64();
        if (!buttonRegistered && ButtonSupported() && commandWindow && now >= nextButtonAttempt) {
            wchar_t label[] = L"MIDI Pickup";
            buttonRegistered = customButton(ButtonIndex, 1, 0, label, commandWindow, ButtonCommand) == 0;
            nextButtonAttempt = now + 10000;
        }
        return true;
    }
    std::optional<double> Read(int target) {
        if (!Connected() || !ValidTarget(target)) return {};
        auto parameter = Parameter(target); float value{};
        return getFloat(parameter.data(), &value) == 0 ? std::optional<double>(value) : std::nullopt;
    }
    bool Write(int target, double gain) {
        if (!Connected() || !ValidTarget(target) || !std::isfinite(gain) || gain < -60 || gain > 12) return false;
        auto parameter = Parameter(target);
        return setFloat(parameter.data(), static_cast<float>(gain)) == 0;
    }
    bool RestartAudioEngine() {
        if (!Connected()) return false;
        char command[] = "Command.Restart";
        return setFloat(command, 1.0f) == 0;
    }
    void Close() {
        if (buttonRegistered && customButton) {
            wchar_t label[] = L"MIDI Pickup", empty[] = L"";
            customButton(ButtonIndex, 1, 0, label, commandWindow, ButtonCommand);
            Sleep(25);
            lastButtonRemoval = customButton(ButtonIndex, -1, 0, empty, commandWindow, ButtonCommand);
            // Type -1 removes the whole button; type 0 only leaves an empty
            // outlined button. Unlike gain writes, Potato drains this command
            // queue only every 16 UI ticks. Logging out after 200 ms can discard
            // removal before it is consumed. Keep the session and HWND alive
            // for 1.5 seconds, including a second removal halfway through.
            for (int i = 0; i < 60; ++i) {
                if (i == 30) lastButtonRemoval = customButton(ButtonIndex, -1, 0, empty, commandWindow, ButtonCommand);
                if (dirty && dirty() < 0) break;
                Sleep(25);
            }
        }
        buttonRegistered = false; commandWindow = nullptr;
        if (loggedIn && logout) logout();
        loggedIn = false; edition = 0;
        if (module) FreeLibrary(module);
        module = nullptr;
        customButton = nullptr;
    }
};
}

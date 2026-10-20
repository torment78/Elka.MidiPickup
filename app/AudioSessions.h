#pragma once
#include "ApplicationMidi.h"
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <atomic>
#include <filesystem>
#include <map>
#include <mutex>

namespace elka::apps {
using Microsoft::WRL::ComPtr;
// All COM ownership, enumeration and volume I/O stays on the preview's MTA
// worker. It never blocks the existing MIDI/VoiceMeeter processing worker.
class AudioSessions {
    class Notifications final : public IAudioSessionNotification {
        std::atomic<ULONG> refs{1};
        std::mutex mutex;
        std::vector<ComPtr<IAudioSessionControl>> incoming;
    public:
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
            if (!out) return E_POINTER;
            *out = nullptr;
            if (id == __uuidof(IUnknown) || id == __uuidof(IAudioSessionNotification)) { *out = static_cast<IAudioSessionNotification*>(this); AddRef(); return S_OK; }
            return E_NOINTERFACE;
        }
        ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
        ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs; if (!n) delete this; return n; }
        HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl* session) override {
            if (!session) return E_POINTER;
            try {
                std::lock_guard lock(mutex);
                if (incoming.size() >= 4096) return E_OUTOFMEMORY; // Periodic enumeration also reconciles the list.
                incoming.emplace_back(session); return S_OK;
            } catch (...) { return E_OUTOFMEMORY; }
        }
        std::vector<ComPtr<IAudioSessionControl>> Drain() { std::lock_guard lock(mutex); auto next = std::move(incoming); incoming.clear(); return next; }
    };
    struct Device {
        Endpoint info;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<Notifications> notifications;
        ~Device() { if (manager && notifications) manager->UnregisterSessionNotification(notifications.Get()); }
    };
    struct Live {
        Session info;
        ComPtr<IAudioSessionControl2> control;
        ComPtr<ISimpleAudioVolume> volume;
    };
    std::map<std::wstring, std::unique_ptr<Device>> devices;
    std::map<std::wstring, Live> live;
    ComPtr<IMMDeviceEnumerator> enumerator;
    static std::wstring Property(IMMDevice* device, REFPROPERTYKEY key) {
        ComPtr<IPropertyStore> store;
        if (FAILED(device->OpenPropertyStore(STGM_READ, &store))) return {};
        PROPVARIANT value; PropVariantInit(&value);
        std::wstring result;
        if (SUCCEEDED(store->GetValue(key, &value)) && value.vt == VT_LPWSTR && value.pwszVal) result = value.pwszVal;
        PropVariantClear(&value); return result;
    }
    static int Route(const std::wstring& label) {
        const auto name = Lower(label);
        if (name.find(L"voicemeeter") == std::wstring::npos) return -1;
        if (name.starts_with(L"voicemeeter vaio3 input")) return 2;
        if (name.starts_with(L"voicemeeter aux input")) return 1;
        if (name.starts_with(L"voicemeeter input")) return 0;
        // In 1..5 are separate endpoints in the newer driver and can be
        // remapped in VoiceMeeter's Audio Map. Never guess their destination.
        return -1;
    }
    void Add(const Endpoint& device, IAudioSessionControl* session) {
        Live item; item.info.endpoint = device.id;
        if (FAILED(session->QueryInterface(IID_PPV_ARGS(&item.control))) || FAILED(session->QueryInterface(IID_PPV_ARGS(&item.volume)))) return;
        LPWSTR raw{};
        if (FAILED(item.control->GetSessionInstanceIdentifier(&raw)) || !raw) return;
        item.info.instance = raw; CoTaskMemFree(raw);
        if (live.contains(item.info.instance)) return;
        DWORD pid{};
        const HRESULT processResult = item.control->GetProcessId(&pid);
        // Cross-process sessions cannot safely be labelled as a single app.
        if (processResult != S_OK) return;
        if (item.control->IsSystemSoundsSession() == S_OK) { item.info.appKey = L"system-sounds"; item.info.name = L"System sounds"; }
        else {
            if (!pid) return;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!process) return;
            wchar_t path[32768]{}; DWORD size = static_cast<DWORD>(std::size(path));
            const bool read = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE; CloseHandle(process);
            if (!read) return;
            item.info.appKey = Lower(path);
            item.info.name = std::filesystem::path(path).stem().wstring();
        }
        const auto id = item.info.instance;
        live.emplace(id, std::move(item));
    }
public:
    std::wstring error;
    bool Initialize() {
        const auto hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (FAILED(hr)) { error = L"Windows audio service unavailable (" + std::to_wstring(hr) + L")."; return false; }
        return true;
    }
    bool Discover() {
        if (!enumerator && !Initialize()) return false;
        ComPtr<IMMDeviceCollection> collection;
        if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) {
            live.clear(); devices.clear(); enumerator.Reset(); error = L"Windows audio endpoints unavailable. Retrying..."; return false;
        }
        UINT count{}; if (FAILED(collection->GetCount(&count))) return false;
        std::set<std::wstring> present;
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> endpoint; LPWSTR raw{};
            if (FAILED(collection->Item(i, &endpoint)) || FAILED(endpoint->GetId(&raw)) || !raw) continue;
            const std::wstring id(raw); CoTaskMemFree(raw); present.insert(id);
            const auto name = Property(endpoint.Get(), PKEY_Device_FriendlyName);
            const bool voiceMeeter = Lower(name + L" " + Property(endpoint.Get(), PKEY_DeviceInterface_FriendlyName)).find(L"voicemeeter") != std::wstring::npos;
            if (!voiceMeeter) continue;
            if (const auto existing = devices.find(id); existing != devices.end()) {
                ComPtr<IAudioSessionEnumerator> sessions;
                int n{};
                if (SUCCEEDED(existing->second->manager->GetSessionEnumerator(&sessions)) && SUCCEEDED(sessions->GetCount(&n))) {
                    for (int j = 0; j < n; ++j) { ComPtr<IAudioSessionControl> control; if (SUCCEEDED(sessions->GetSession(j, &control))) Add(existing->second->info, control.Get()); }
                    continue;
                }
                devices.erase(existing);
                std::erase_if(live, [&](const auto& item) { return item.second.info.endpoint == id; });
            }
            auto device = std::make_unique<Device>();
            device->info = {id, name, -1, voiceMeeter};
            device->info.route = Route(device->info.name);
            if (FAILED(endpoint->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(device->manager.GetAddressOf())))) continue;
            device->notifications.Attach(new Notifications());
            if (FAILED(device->manager->RegisterSessionNotification(device->notifications.Get()))) continue;
            ComPtr<IAudioSessionEnumerator> sessions;
            if (FAILED(device->manager->GetSessionEnumerator(&sessions))) continue;
            int n{}; if (FAILED(sessions->GetCount(&n))) continue; // Starts notifications.
            for (int j = 0; j < n; ++j) { ComPtr<IAudioSessionControl> control; if (SUCCEEDED(sessions->GetSession(j, &control))) Add(device->info, control.Get()); }
            devices.emplace(id, std::move(device));
        }
        std::erase_if(devices, [&](const auto& item) { return !present.contains(item.first); });
        std::erase_if(live, [&](const auto& item) { return !devices.contains(item.second.info.endpoint); });
        error.clear(); return true;
    }
    std::vector<Endpoint> Endpoints() const {
        std::vector<Endpoint> result; for (const auto& [id, d] : devices) { (void)id; result.push_back(d->info); } return result;
    }
    std::vector<Session> Read() {
        for (const auto& [id, device] : devices) { (void)id; for (const auto& control : device->notifications->Drain()) Add(device->info, control.Get()); }
        std::vector<Session> result;
        for (auto it = live.begin(); it != live.end();) {
            auto& s = it->second; AudioSessionState state{};
            if (FAILED(s.control->GetState(&state)) || state == AudioSessionStateExpired) { it = live.erase(it); continue; }
            float gain{}; BOOL mute{};
            s.info.readable = SUCCEEDED(s.volume->GetMasterVolume(&gain)) && SUCCEEDED(s.volume->GetMute(&mute));
            s.info.volume = gain; s.info.muted = mute != FALSE;
            result.push_back(s.info); ++it;
        }
        return result;
    }
    bool Write(const Mapping& mapping, const Target& expected, std::optional<double> gain, std::optional<bool> mute) {
        // Verify exact session membership and fresh levels immediately before
        // changing anything. Never address an app by list position or prefix.
        const auto fresh = Resolve(Read(), mapping.endpoint, mapping.appKey);
        if (!fresh.ready || fresh.instances != expected.instances || fresh.mixedVolume != expected.mixedVolume || fresh.mixedMute != expected.mixedMute ||
            (gain && std::abs(fresh.volume - expected.volume) > 0.0008) || (mute && fresh.muted != expected.muted)) return false;
        for (const auto& id : fresh.instances) {
            auto& s = live.at(id);
            if (gain && FAILED(s.volume->SetMasterVolume(static_cast<float>(*gain), nullptr))) return false;
            if (mute && FAILED(s.volume->SetMute(*mute ? TRUE : FALSE, nullptr))) return false;
        }
        return true;
    }
};
}

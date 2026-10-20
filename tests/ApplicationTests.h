#pragma once
#include "../app/ApplicationProfile.h"
#include "../app/AudioSessions.h"
#include "../app/ApplicationWindow.h"
#include "../app/Devices.h"
#include <audioclient.h>

void ApplicationTests() {
    using namespace elka::apps;
    Check(Decode(0x007f07bf)->source == Source{0, 15, 7}, "CC source keeps channel and number");
    Check(Decode(0x007f2491)->source == Source{1, 1, 36}, "note source distinct from CC");
    Check(Decode(0x007f2481)->value == 0 && Decode(0x00002491)->value == 0, "note-off and zero velocity release");
    Check(!Decode(0xe0) && !Decode(0x008007b0), "pitch bend and malformed data never learn accidentally");
    const Session a{L"vaio3", L"spotify.exe", L"Spotify", L"session-a", 0.7, false, true};
    const Session b{L"aux", L"spotify.exe", L"Spotify", L"session-b", 0.2, false, true};
    const Session c{L"vaio3", L"spotify-helper.exe", L"Other", L"session-c", 0.3, false, true};
    std::vector<Session> sessions{a, b, c};
    auto target = Resolve(sessions, L"vaio3", L"spotify.exe");
    Check(target.ready && target.instances == std::vector<std::wstring>{L"session-a"} && target.volume == 0.7, "resolve requires exact application and endpoint identity");
    std::reverse(sessions.begin(), sessions.end());
    Check(Resolve(sessions, L"vaio3", L"spotify.exe").instances == target.instances, "enumeration reordering cannot change target");
    Row row; row.mapping = {1, 2, L"vaio3", L"spotify.exe", L"Spotify", {0, 0, 7}, Action::Volume, true};
    row.Observe(target, 0); row.Input(25, true);
    Check(!row.VolumeWrite(), "application waits below software level");
    row.Input(100, true);
    Check(row.pickup.tracking && row.VolumeWrite(), "fast crossing acquires application pickup");
    target.volume = *row.VolumeWrite(); row.pickup.Written(Row::Encode(target.volume), 1);
    row.Observe(target, 2);
    Check(row.pickup.tracking, "application write readback preserves tracking");
    target.volume = 0.4; row.Observe(target, 3);
    Check(!row.pickup.tracking && !row.VolumeWrite(), "external Windows or VoiceMeeter app change rearms");
    row.Input(51, true); Check(row.pickup.tracking, "application tolerance is measured in MIDI steps");
    target.instances = {L"restarted"}; row.Observe(target, 4);
    Check(!row.pickup.tracking && !row.VolumeWrite(), "process restart cannot replay old movement");
    row.Observe({}, 5); row.Input(127, true);
    Check(!row.Ready() && !row.VolumeWrite(), "offline application cannot write");
    row.Observe(target, 6); row.Input(127, false);
    Check(!row.VolumeWrite(), "stopped preview cannot replay monitored motion");
    target.volume = 0; row.Observe(target, 7); row.Input(0, true);
    Check(row.pickup.tracking && Row::DecodeGain(-60) == 0 && Row::DecodeGain(0) == 1, "silence and unity are exact scalar endpoints");
    for (int midi = 0; midi < 128; ++midi) {
        Row sweep; target.volume = midi / 127.0; sweep.Observe(target, 0); sweep.Input(midi, true);
        Check(sweep.pickup.tracking, "all 128 application levels can be picked up");
    }
    Session duplicate = a; duplicate.instance = L"second"; duplicate.volume = 0.5;
    auto mixed = Resolve({a, duplicate}, a.endpoint, a.appKey);
    row.Observe(mixed, 8); row.Input(100, true);
    Check(mixed.mixedVolume && !row.Ready() && !row.VolumeWrite(), "differing session volumes never collapse into a jump");
    duplicate.volume = a.volume; mixed = Resolve({a, duplicate}, a.endpoint, a.appKey);
    row.Observe(mixed, 9); row.Input(89, true);
    Check(row.Ready() && row.pickup.tracking, "matching sessions can be controlled as one application");
    duplicate.readable = false; row.Observe(Resolve({a, duplicate}, a.endpoint, a.appKey), 10);
    Check(!row.Ready(), "one unreadable session blocks whole app");
    Row button; button.mapping = {2, 2, a.endpoint, a.appKey, a.name, {1, 0, 36}, Action::Mute, true};
    button.Observe(Resolve({a}, a.endpoint, a.appKey), 0);
    button.Input(127, true); Check(!button.muteWrite, "held button on activation cannot toggle");
    button.Input(0, true); button.Input(127, true);
    Check(button.muteWrite == true, "press after release toggles mute once");
    button.Input(127, true); button.Input(0, true);
    Check(button.muteWrite == true, "repeated press and note-off do not toggle again");
    button.Input(127, true); Check(button.muteWrite == false, "second press toggles back even within one batch");
    button.Reset(); button.Input(0, false); button.Input(127, false);
    Check(!button.muteWrite, "mute monitor cannot write");
    button.mapping.source.kind = 0; button.Input(0, true); button.Input(63, true); Check(!button.muteWrite, "CC button low values are release");
    button.Input(64, true); Check(button.muteWrite == true, "CC button uses rising threshold");
    Row encoder; encoder.mapping = row.mapping; encoder.mapping.action = Action::RelativeTwos;
    target.volume = 0.5; encoder.Observe(target, 0); encoder.Input(1, true); encoder.Input(127, true);
    Check(std::abs(*encoder.VolumeWrite() - 0.5) < 0.00001, "relative encoder opposing deltas cancel");
    Check(Delta(Action::RelativeOffset, 65) == 1 && Delta(Action::RelativeOffset, 63) == -1 && Delta(Action::RelativeOffset, 64) == 0, "offset encoder decoding");
    Check(Conflict({0, 0, 7}, {{0, 0, 7}}, {}, 0), "strip and bus assignments are reserved");
    Check(!Conflict({1, 0, 7}, {{0, 0, 7}}, {}, 0), "note and CC with same number are distinct");
    row.mapping.use = true;
    Check(Conflict(row.mapping.source, {}, {row}, 999) && !Conflict(row.mapping.source, {}, {row}, row.mapping.id), "preview duplicates rejected but relearn self allowed");
    Check(!SameTarget(row.mapping, button.mapping), "separate volume and mute rows allowed");
    Setup profile; profile.endpoints = {L"endpoint 1", L"endpoint 2", L"endpoint 3"}; profile.mappings = {row.mapping, button.mapping}; profile.mappings[0].name = L"Músic \"player\"";
    const auto path = std::filesystem::temp_directory_path() / (L"Elka-AppMidi-test-" + std::to_wstring(GetCurrentProcessId()) + L".applications");
    Check(SaveApplicationSetup(path, profile), "application profile saves atomically"); Setup loaded;
    Check(LoadApplicationSetup(path, loaded) && loaded.mappings.size() == 2 && loaded.mappings[0].name == profile.mappings[0].name && loaded.endpoints == profile.endpoints, "application profile Unicode and mapping identity round trip");
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "broken";
    Check(!LoadApplicationSetup(path, loaded) && loaded.mappings.size() == 2, "corrupt application profile rejected without partial replacement");
    std::filesystem::remove(path);
}

void ControllerIntegration(const elka::apps::Endpoint& endpoint, const std::wstring& key, ISimpleAudioVolume* own) {
    using namespace elka::apps;
    const auto path = std::filesystem::temp_directory_path() / (L"Elka-controller-test-" + std::to_wstring(GetCurrentProcessId()) + L".applications");
    Controller controller; controller.Launch(path);
    std::atomic<bool> connected{true}, reserve{};
    std::jthread heartbeat([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            controller.Bridge(connected, 3, reserve ? std::vector<Source>{{0, 15, 119}} : std::vector<Source>{});
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    auto wait = [&](auto condition, const char* name) {
        for (int i = 0; i < 200; ++i) { if (condition(controller.Snapshot())) { Check(true, name); return; } Sleep(10); }
        Check(false, name);
    };
    auto send = [&](Operation op, uint64_t row = 0, int value = 0) { Command c{op}; c.row = row; c.value = value; controller.Send(std::move(c)); };
    auto input = [&](int value) { controller.Midi(0xbf | (119u << 8) | (static_cast<DWORD>(value) << 16), GetTickCount()); };
    own->SetMasterVolume(0.63f, nullptr); own->SetMute(FALSE, nullptr);
    wait([&](const View& v) { return v.connected && Resolve(v.sessions, endpoint.id, key).ready; }, "controller discovers isolated test session");
    Command choose{Operation::Endpoint}; choose.route = endpoint.route; choose.key = endpoint.id; controller.Send(choose);
    Command add{Operation::Add}; add.route = endpoint.route; add.key = key; controller.Send(add);
    wait([](const View& v) { return v.rows.size() == 1; }, "dynamic add creates application row");
    const auto id = controller.Snapshot().rows[0].mapping.id;
    send(Operation::Learn, id); wait([&](const View& v) { return v.learning == id; }, "controller enters MIDI Learn"); input(0);
    wait([](const View& v) { return !v.learning && v.rows[0].mapping.source == Source{0, 15, 119}; }, "controller learns real message format");
    float gain{}; own->GetMasterVolume(&gain); Check(std::abs(gain - 0.63f) < 0.001, "learning does not change session volume");
    send(Operation::Confirm, 0, 1); send(Operation::Start);
    wait([](const View& v) { return v.active; }, "preview starts independently"); input(0); Sleep(60); own->GetMasterVolume(&gain);
    Check(std::abs(gain - 0.63f) < 0.001, "controller waits before application pickup"); input(127);
    wait([](const View& v) { return v.rows[0].writes > 0 && v.rows[0].target.volume > 0.99; }, "controller crossing writes isolated session");
    for (int value = 127; value >= 0; --value) { input(value); Sleep(2); }
    for (int value = 0; value < 128; ++value) { input(value); Sleep(2); }
    wait([](const View& v) { return v.rows[0].pickup.tracking && !v.rows[0].failed && v.rows[0].target.volume > 0.99; }, "rapid controller sweep and reversal retain pickup");
    own->SetMasterVolume(0.4f, nullptr);
    wait([](const View& v) { return !v.rows[0].pickup.tracking && std::abs(v.rows[0].target.volume - 0.4) < 0.001; }, "external application edit rearms controller");
    input(127); Sleep(70); own->GetMasterVolume(&gain); Check(std::abs(gain - 0.4f) < 0.001, "moving away after external edit cannot jump");
    reserve = true;
    wait([](const View& v) { return v.rows[0].blocked; }, "new strip assignment blocks preview source dynamically");
    input(0); input(127); Sleep(70); own->GetMasterVolume(&gain); Check(std::abs(gain - 0.4f) < 0.001, "reserved strip MIDI cannot control an app");
    reserve = false; wait([](const View& v) { return !v.rows[0].blocked; }, "removing conflict rearms row");
    connected = false;
    wait([](const View& v) { return !v.active && !v.connected; }, "loss of shared MIDI connection stops preview");
    connected = true; wait([](const View& v) { return v.connected; }, "connection restored");
    input(0); input(127); Sleep(70); own->GetMasterVolume(&gain); Check(std::abs(gain - 0.4f) < 0.001, "reconnection requires explicit start");
    controller.Send(add); wait([](const View& v) { return v.rows.size() == 2; }, "second app row creates separate mute control");
    const auto muteId = controller.Snapshot().rows[1].mapping.id;
    send(Operation::Learn, muteId); wait([&](const View& v) { return v.learning == muteId; }, "learn mute row");
    auto note = [&](bool press) { controller.Midi((press ? 0x9fu : 0x8fu) | (118u << 8) | (press ? (127u << 16) : 0), GetTickCount()); };
    note(true); wait([](const View& v) { return !v.learning && v.rows[1].mapping.source.kind == 1 && v.rows[1].mapping.action == Action::Mute; }, "note learns mute action");
    send(Operation::Confirm, 0, 1); send(Operation::Start); wait([](const View& v) { return v.active; }, "start volume and mute together");
    note(false); note(true); note(false);
    wait([](const View& v) { return v.rows[1].writes > 0 && v.rows[1].target.muted; }, "note toggles isolated session mute");
    BOOL muted{}; own->GetMute(&muted); Check(muted != FALSE, "note release does not undo mute");
    send(Operation::Stop); wait([](const View& v) { return !v.active; }, "explicit preview stop");
    note(true); note(false); input(127); Sleep(80); own->GetMute(&muted); own->GetMasterVolume(&gain);
    Check(muted != FALSE && std::abs(gain - 0.4f) < 0.001, "stop blocks both volume and mute writes");
    send(Operation::Save); wait([&](const View& v) { return !v.dirty && std::filesystem::exists(path); }, "controller saves independent preview profile");
    send(Operation::Remove, id); send(Operation::Remove, muteId); wait([](const View& v) { return v.rows.empty(); }, "dynamic removal leaves no controlled target");
    heartbeat.request_stop(); heartbeat.join(); controller.Shutdown();
    Setup saved; Check(LoadApplicationSetup(path, saved) && saved.mappings.size() == 2 && saved.mappings[0].appKey == key, "saved controller mapping retains exact app identity");
    std::filesystem::remove(path);
}

int InspectAudioSessions(bool integration) {
    using namespace elka::apps;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    int result = 0;
    try {
        AudioSessions audio; Check(audio.Discover(), "audio endpoint discovery");
        auto endpoints = audio.Endpoints();
        for (const auto& endpoint : endpoints) {
            std::wcout << L"Endpoint [" << endpoint.route << L"] " << endpoint.name << L"\n";
            for (const auto& session : audio.Read()) if (session.endpoint == endpoint.id)
                std::wcout << L"  " << session.name << L" | scalar=" << session.volume << L" mute=" << session.muted << L" readable=" << session.readable << L"\n";
        }
        if (integration) {
            const auto endpoint = std::find_if(endpoints.begin(), endpoints.end(), [](const Endpoint& e) { return e.route >= 0; });
            Check(endpoint != endpoints.end(), "VoiceMeeter playback endpoint available for isolated test");
            ComPtr<IMMDeviceEnumerator> devices; Check(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))), "test device enumerator");
            ComPtr<IMMDevice> device; Check(SUCCEEDED(devices->GetDevice(endpoint->id.c_str(), &device)), "test endpoint");
            ComPtr<IAudioClient> client; Check(SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()))), "test audio client");
            WAVEFORMATEX* format{}; Check(SUCCEEDED(client->GetMixFormat(&format)), "test mix format");
            GUID session{}; CoCreateGuid(&session);
            const auto hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, 1000000, 0, format, &session); CoTaskMemFree(format);
            Check(SUCCEEDED(hr), "isolated silent session initialized");
            // No Start(), buffers, or audio playback. Only our test session changes.
            ComPtr<ISimpleAudioVolume> own; Check(SUCCEEDED(client->GetService(IID_PPV_ARGS(&own))), "test session volume");
            wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768); const auto key = Lower(path);
            std::vector<Session> sessions;
            for (int retry = 0; retry < 50; ++retry) { sessions = audio.Read(); if (Resolve(sessions, endpoint->id, key).ready) break; Sleep(20); }
            auto target = Resolve(sessions, endpoint->id, key); Check(target.ready, "notification discovers new silent session");
            Mapping mapping{1, endpoint->route, endpoint->id, key, L"Isolated test", {0, 15, 119}, Action::Volume, true};
            Check(audio.Write(mapping, target, 0.37, {}), "backend writes only exact test app");
            float actual{}; own->GetMasterVolume(&actual); Check(std::abs(actual - 0.37) < 0.001, "Core Audio volume write readback");
            own->SetMasterVolume(0.63f, nullptr);
            Check(!audio.Write(mapping, target, 0.12, {}), "stale target cannot overwrite external change");
            target = Resolve(audio.Read(), endpoint->id, key);
            Check(audio.Write(mapping, target, {}, true), "backend test session mute"); BOOL muted{}; own->GetMute(&muted); Check(muted != FALSE, "mute readback");
            target.instances = {L"wrong-instance"}; Check(!audio.Write(mapping, target, 0.9, {}), "stale session identity cannot write");
            ControllerIntegration(*endpoint, key, own.Get());
            std::cout << "PASS: isolated audio-session integration\n";
        }
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; result = 1; }
    CoUninitialize(); return result;
}

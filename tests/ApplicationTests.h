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
    Check(EndpointRoute(L"Voicemeeter In 5 (VB-Audio Voicemeeter VAIO)") == 7 && EndpointRoute(L"Voicemeeter In 1") == 3, "numbered input endpoints have distinct routes");
    Check(EndpointRoute(L"Voicemeeter In 10") == -1 && EndpointRoute(L"Other Input") == -1, "unknown endpoints never guessed");
    for (int edition = 1; edition <= 3; ++edition) {
        int available{}; for (int route = 0; route < RouteCount; ++route) if (RouteAvailable(route, edition)) ++available;
        Check(available == edition, "only virtual input buttons follow VoiceMeeter edition");
    }
    Check(!RouteAvailable(0, 0) && !RouteAvailable(-1, 3) && !RouteAvailable(8, 3), "disconnected and invalid routes unavailable");
    for (int route = 3; route < RouteCount; ++route) {
        Row legacy = row; legacy.mapping.route = route;
        legacy.Observe(Resolve({a}, a.endpoint, a.appKey), 0); legacy.Input(0, true); legacy.Input(127, true);
        Check(!RouteAvailable(route, 3) && !legacy.VolumeWrite() && !Conflict(legacy.mapping.source, {}, {legacy}, 999), "legacy In 1-5 mappings neither write nor reserve MIDI");
    }
    std::vector<Endpoint> endpoints{{L"vaio3", L"VAIO 3", 2, true}, {L"aux", L"AUX", 1, true}};
    std::vector<Row> automatic; uint64_t nextId = 1;
    SyncApplications(automatic, endpoints, {a, a, b, c}, 3, nextId);
    Check(automatic.size() == 3 && std::none_of(automatic.begin(), automatic.end(), [](const Row& r) { return Configured(r.mapping); }), "all present apps appear once per endpoint and start unchecked");
    const auto firstId = automatic[0].mapping.id; automatic[0].mapping.source = {0, 2, 91}; automatic[0].mapping.use = true;
    SyncApplications(automatic, endpoints, {}, 3, nextId);
    Check(automatic.size() == 1 && automatic[0].mapping.id == firstId, "closed apps discard discovery rows but retain learned assignments");
    SyncApplications(automatic, endpoints, {a}, 3, nextId);
    Check(automatic.size() == 1 && automatic[0].mapping.source == Source{0, 2, 91} && automatic[0].mapping.use, "returning app keeps source and Use without duplicate row");
    automatic[0].mapping.route = 0; SyncApplications(automatic, endpoints, {a}, 3, nextId);
    Check(automatic[0].mapping.route == 2 && automatic[0].mapping.endpoint == a.endpoint, "legacy endpoint choice migrates by identity without retargeting");
    SyncApplications(automatic, endpoints, {a, b}, 1, nextId);
    Check(automatic.size() == 1, "edition changes retain saved mappings but do not discover unsupported inputs");
    HideRow(automatic[0]);
    Check(automatic[0].mapping.hidden && !automatic[0].mapping.use && !automatic[0].pickup.tracking, "remove row disables control and resets pickup");
    SyncApplications(automatic, endpoints, {a, b}, 3, nextId);
    Check(automatic.size() == 2 && automatic[0].mapping.hidden && !automatic[1].mapping.hidden, "removed app stays hidden only on its original endpoint");
    SyncApplications(automatic, endpoints, {}, 3, nextId);
    SyncApplications(automatic, endpoints, {a}, 3, nextId);
    Check(automatic.size() == 1 && automatic[0].mapping.hidden && automatic[0].mapping.id == firstId, "removed row does not return after app restart or refresh");
    automatic[0].mapping.use = true; automatic[0].Observe(Resolve({a}, a.endpoint, a.appKey), 0); automatic[0].Input(0, true); automatic[0].Input(127, true);
    Check(!automatic[0].VolumeWrite() && !Conflict(automatic[0].mapping.source, {}, automatic, 999), "hidden flag independently blocks output and source reservation");
    RestoreRow(automatic[0]);
    Check(!automatic[0].mapping.hidden && !automatic[0].mapping.use && automatic[0].mapping.source == Source{0, 2, 91}, "restore keeps learned source but requires explicit Use");
    Row unlearned; unlearned.mapping = {nextId++, 2, a.endpoint, a.appKey, a.name, {}, Action::Volume, false}; HideRow(unlearned);
    Check(Configured(unlearned.mapping), "removing an unlearned row must persist the exclusion");
    Setup profile; profile.endpoints = {L"endpoint 1", L"endpoint 2", L"endpoint 3"}; profile.mappings = {row.mapping, button.mapping}; profile.mappings[0].name = L"Músic \"player\"";
    const auto path = std::filesystem::temp_directory_path() / (L"Elka-AppMidi-test-" + std::to_wstring(GetCurrentProcessId()) + L".applications");
    Check(SaveApplicationSetup(path, profile), "application profile saves atomically"); Setup loaded;
    Check(LoadApplicationSetup(path, loaded) && loaded.mappings.size() == 2 && loaded.mappings[0].name == profile.mappings[0].name && loaded.endpoints == profile.endpoints, "application profile Unicode and mapping identity round trip");
    profile.endpoints[7] = L"in5"; profile.mappings[0].route = 7; profile.mappings[0].endpoint = L"in5";
    Check(SaveApplicationSetup(path, profile) && LoadApplicationSetup(path, loaded) && loaded.mappings[0].route == 7 && loaded.endpoints[7] == L"in5" && !loaded.mappings[0].Enabled(), "legacy In 5 mapping preserved but disabled");
    profile.mappings.push_back(unlearned.mapping);
    Check(SaveApplicationSetup(path, profile) && LoadApplicationSetup(path, loaded) && loaded.mappings.back().hidden && !loaded.mappings.back().use && !loaded.mappings.back().source.Valid(), "removed unlearned row survives save and reload");
    {
        const std::wstring legacy = L"\ufeffElkaApplicationMidi 2\n3\n\"vaio\"\n\"aux\"\n\"vaio3\"\n\"in1\"\n\"in2\"\n\"in3\"\n\"in4\"\n\"in5\"\n2\n2 1 0 0 7 0 \"vaio3\" \"player.exe\" \"Player\"\n7 1 0 0 8 0 \"in5\" \"browser.exe\" \"Browser\"\n";
        std::ofstream file(path, std::ios::binary | std::ios::trunc); file.write(reinterpret_cast<const char*>(legacy.data()), static_cast<std::streamsize>(legacy.size() * sizeof(wchar_t)));
    }
    Check(LoadApplicationSetup(path, loaded) && loaded.mappings.size() == 2 && loaded.mappings[0].Enabled() && !loaded.mappings[0].hidden && !loaded.mappings[1].Enabled(), "v2 profile keeps VAIO MIDI and disables old numbered inputs");
    {
        const std::wstring legacy = L"\ufeffElkaApplicationMidi 1\n3\n\"vaio\"\n\"aux\"\n\"vaio3\"\n1\n2 1 0 0 7 0 \"actual-in5\" \"player.exe\" \"Player\"\n";
        std::ofstream file(path, std::ios::binary | std::ios::trunc); file.write(reinterpret_cast<const char*>(legacy.data()), static_cast<std::streamsize>(legacy.size() * sizeof(wchar_t)));
    }
    Check(LoadApplicationSetup(path, loaded) && loaded.mappings.size() == 1 && loaded.mappings[0].endpoint == L"actual-in5" && loaded.mappings[0].source.number == 7, "v1 profile migrates exact endpoint and MIDI source");
    std::ofstream(path, std::ios::binary | std::ios::trunc) << "broken";
    Check(!LoadApplicationSetup(path, loaded) && loaded.mappings.size() == 1, "corrupt application profile rejected without partial replacement");
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
    auto snapshot = [&] {
        auto v = controller.Snapshot();
        std::erase_if(v.rows, [&](const Row& r) { return r.mapping.appKey != key || r.mapping.endpoint != endpoint.id; });
        return v;
    };
    auto wait = [&](auto condition, const char* name) {
        for (int i = 0; i < 200; ++i) { if (condition(snapshot())) { Check(true, name); return; } Sleep(10); }
        Check(false, name);
    };
    auto send = [&](Operation op, uint64_t row = 0, int value = 0) { Command c{op}; c.row = row; c.value = value; controller.Send(std::move(c)); };
    auto input = [&](int value) { controller.Midi(0xbf | (119u << 8) | (static_cast<DWORD>(value) << 16), GetTickCount()); };
    own->SetMasterVolume(0.63f, nullptr); own->SetMute(FALSE, nullptr);
    wait([&](const View& v) { return v.connected && Resolve(v.sessions, endpoint.id, key).ready; }, "controller discovers isolated test session");
    wait([](const View& v) { return v.rows.size() == 1 && !v.rows[0].mapping.use; }, "live session automatically creates unchecked application row");
    const auto initial = controller.Snapshot();
    Check(std::none_of(initial.rows.begin(), initial.rows.end(), [](const Row& r) { return r.mapping.use; }), "discovery never enables another app");
    const auto id = snapshot().rows[0].mapping.id;
    send(Operation::Use, id, 1);
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
    Command add{Operation::Add}; add.row = id; controller.Send(add); wait([](const View& v) { return v.rows.size() == 2; }, "extra control creates separate mute row");
    const auto muteId = snapshot().rows[1].mapping.id; send(Operation::Use, muteId, 1);
    send(Operation::Learn, muteId); wait([&](const View& v) { return v.learning == muteId; }, "learn mute row");
    auto note = [&](bool press) { controller.Midi((press ? 0x9fu : 0x8fu) | (118u << 8) | (press ? (127u << 16) : 0), GetTickCount()); };
    note(true); wait([](const View& v) { return !v.learning && v.rows[1].mapping.source.kind == 1 && v.rows[1].mapping.action == Action::Mute; }, "note learns mute action");
    send(Operation::Confirm, 0, 1); send(Operation::Start); wait([](const View& v) { return v.active; }, "start volume and mute together");
    note(false); note(true); note(false);
    wait([](const View& v) { return v.rows[1].writes > 0 && v.rows[1].target.muted; }, "note toggles isolated session mute");
    BOOL muted{}; own->GetMute(&muted); Check(muted != FALSE, "note release does not undo mute");
    send(Operation::Hide, id);
    wait([](const View& v) { return v.active && v.rows[0].mapping.hidden && !v.rows[0].mapping.use && !v.rows[1].mapping.hidden; }, "remove row disables only that control while other rows keep running");
    Check(!controller.Reserved({0, 15, 119}) && controller.Reserved({1, 15, 118}), "removed row releases its MIDI source without releasing other controls");
    input(0); input(127); Sleep(80); own->GetMasterVolume(&gain);
    Check(std::abs(gain - 0.4f) < 0.001, "removed row cannot change its application's live volume");
    send(Operation::Refresh);
    wait([](const View& v) { return v.rows.size() == 2 && v.rows[0].mapping.hidden && v.rows[0].target.ready; }, "refresh cannot rediscover a removed row");
    send(Operation::Save); wait([&](const View& v) { return !v.dirty && std::filesystem::exists(path); }, "removed row can be saved");
    Setup hiddenSetup;
    Check(LoadApplicationSetup(path, hiddenSetup) && hiddenSetup.mappings.size() == 2 && hiddenSetup.mappings[0].hidden && !hiddenSetup.mappings[0].use, "saved removal keeps exact mapping disabled");
    Command restore{Operation::RestoreHidden}; restore.route = endpoint.route; controller.Send(restore);
    wait([](const View& v) { return !v.rows[0].mapping.hidden && !v.rows[0].mapping.use && v.rows[0].mapping.source == Source{0, 15, 119}; }, "restored live row retains MIDI and requires explicit Use");
    send(Operation::Stop); wait([](const View& v) { return !v.active; }, "explicit preview stop");
    note(true); note(false); input(127); Sleep(80); own->GetMute(&muted); own->GetMasterVolume(&gain);
    Check(muted != FALSE && std::abs(gain - 0.4f) < 0.001, "stop blocks both volume and mute writes");
    send(Operation::Refresh); wait([&](const View& v) { return v.rows.size() == 2 && v.rows[0].mapping.id == id && v.rows[0].target.ready; }, "refresh keeps learned identity and paused live session");
    send(Operation::Save); wait([&](const View& v) { return !v.dirty && std::filesystem::exists(path); }, "controller saves independent preview profile");
    send(Operation::Remove, id); send(Operation::Remove, muteId); wait([](const View& v) { return std::none_of(v.rows.begin(), v.rows.end(), [](const Row& r) { return Configured(r.mapping); }); }, "clear MIDI leaves only unchecked discovery rows");
    heartbeat.request_stop(); heartbeat.join(); controller.Shutdown();
    Setup saved; Check(LoadApplicationSetup(path, saved) && saved.mappings.size() == 2 && saved.mappings[0].appKey == key, "saved controller mapping retains exact app identity");
    std::filesystem::remove(path);
}

// A separate test-owned process holds a silent session, then exits normally.
// Keeping the monitor alive across its exit exercises stale-session cleanup.
int SessionChild(const char* endpointArg, const char* readyArg, const char* exitArg) {
    using namespace elka::apps;
    auto wide = [](const char* s) { return std::wstring(s, s + strlen(s)); }; // These arguments are GUIDs and ASCII event names.
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    int result = 1;
    {
        ComPtr<IMMDeviceEnumerator> devices; ComPtr<IMMDevice> device; ComPtr<IAudioClient> client;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices))) &&
            SUCCEEDED(devices->GetDevice(wide(endpointArg).c_str(), &device)) &&
            SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf())))) {
            WAVEFORMATEX* format{};
            if (SUCCEEDED(client->GetMixFormat(&format))) {
                GUID session{}; CoCreateGuid(&session);
                const auto hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_NOPERSIST, 1000000, 0, format, &session); CoTaskMemFree(format);
                if (SUCCEEDED(hr)) {
                    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, wide(readyArg).c_str()), exit = OpenEventW(SYNCHRONIZE, FALSE, wide(exitArg).c_str());
                    if (ready && exit) { SetEvent(ready); WaitForSingleObject(exit, 8000); result = 0; }
                    if (ready) CloseHandle(ready); if (exit) CloseHandle(exit);
                }
            }
        }
    }
    CoUninitialize(); return result;
}
void ClosedProcessIntegration(elka::apps::AudioSessions& audio, const elka::apps::Endpoint& endpoint) {
    using namespace elka::apps;
    const auto prefix = L"Local\\ElkaSessionTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    const auto readyName = prefix + L"-ready", exitName = prefix + L"-exit";
    struct Child {
        HANDLE ready{}, exit{}, process{}, thread{};
        ~Child() { if (exit) SetEvent(exit); if (process) { WaitForSingleObject(process, 9000); CloseHandle(process); } if (thread) CloseHandle(thread); if (ready) CloseHandle(ready); if (exit) CloseHandle(exit); }
    } child;
    child.ready = CreateEventW(nullptr, TRUE, FALSE, readyName.c_str()); child.exit = CreateEventW(nullptr, TRUE, FALSE, exitName.c_str());
    Check(child.ready && child.exit, "session lifecycle test events");
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, 32768);
    auto command = L"\"" + std::wstring(executable) + L"\" --session-child \"" + endpoint.id + L"\" \"" + readyName + L"\" \"" + exitName + L"\"";
    STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE; PROCESS_INFORMATION process{};
    Check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE, "launch isolated session child");
    child.process = process.hProcess; child.thread = process.hThread;
    Check(WaitForSingleObject(child.ready, 5000) == WAIT_OBJECT_0, "child silent session ready");
    const auto key = Lower(executable);
    Target target;
    for (int i = 0; i < 100; ++i) { target = Resolve(audio.Read(), endpoint.id, key); if (target.ready) break; Sleep(20); }
    Check(target.ready, "monitor discovers session owned by child process");
    SetEvent(child.exit); Check(WaitForSingleObject(child.process, 5000) == WAIT_OBJECT_0, "child exits without disturbing other apps");
    for (int i = 0; i < 100; ++i) { target = Resolve(audio.Read(), endpoint.id, key); if (!target.ready) break; Sleep(20); }
    Check(!target.ready, "closed process disappears from monitored sessions");
    audio.Refresh(); Check(!Resolve(audio.Read(), endpoint.id, key).ready, "refresh cannot resurrect a closed application");
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
            const auto endpoint = std::find_if(endpoints.begin(), endpoints.end(), [](const Endpoint& e) { return RouteAvailable(e.route, 3); });
            Check(endpoint != endpoints.end(), "VoiceMeeter playback endpoint available for isolated test");
            ClosedProcessIntegration(audio, *endpoint);
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

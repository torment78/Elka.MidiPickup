#include "ApplicationControl.h"
#include "AudioSessions.h"

namespace elka::apps {
void Controller::Launch(const std::filesystem::path& path) {
    if (launched.exchange(true)) return;
    worker = std::jthread([this, path](std::stop_token stop) {
        try { Run(stop, path); }
        catch (...) {
            std::lock_guard lock(viewMutex); published.active = false; published.connected = false; published.learning = 0;
            published.notice = L"Application preview stopped after an internal error. Strip/bus processing continues. Restart Elka before retrying the preview.";
        }
    });
}
void Controller::Shutdown() {
    if (!launched) return;
    emergencyStop = true; worker.request_stop(); wake.notify_all();
    if (worker.joinable()) worker.join();
    launched = false;
}
void Controller::Bridge(bool connected, int edition, const std::vector<Source>& reserved) {
    if (!launched) return;
    std::lock_guard lock(inputMutex); link = {connected, edition, GetTickCount64(), reserved};
}
void Controller::Run(std::stop_token stop, std::filesystem::path path) {
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(apartment)) { std::lock_guard lock(viewMutex); published.notice = L"Could not initialize Windows audio sessions."; return; }
    struct Uninitialize { ~Uninitialize() { CoUninitialize(); } } uninitialize;
    {
        AudioSessions audio;
        View state; Setup saved;
        if (LoadApplicationSetup(path, saved)) {
            state.selected = saved.endpoints; state.tolerance = saved.tolerance;
            for (auto& m : saved.mappings) { Row row; row.mapping = std::move(m); state.rows.push_back(std::move(row)); }
            state.notice = L"Application mappings loaded. Preview is stopped; confirm routing and click Start applications.";
        } else if (std::filesystem::exists(path)) state.notice = L"Application setup could not be read. No mappings were loaded.";
        uint64_t nextId = state.rows.size() + 1, discoverAt{}, publishAt{}, lastTick = GetTickCount64(), drops{};
        MidiInbox::Batch batch;
        auto reset = [&] { inbox.Clear(); for (auto& row : state.rows) row.Reset(); };
        auto halt = [&] { state.active = false; state.learning = 0; reset(); };
        bool resumeAfterReset{};
        while (!stop.stop_requested()) {
            const auto now = GetTickCount64();
            Link current; std::vector<Command> pending;
            { std::lock_guard lock(inputMutex); current = link; pending.swap(commands); }
            // Finish edits/saves already queued before acknowledging the reset.
            // Their ordering must not be lost when entering the suspended loop.
            const auto beginReset = std::find_if(pending.begin(), pending.end(), [](const Command& c) {
                return c.operation == Operation::BeginAudioReset;
            });
            if (beginReset != pending.end() && beginReset != pending.begin()) {
                std::lock_guard lock(inputMutex);
                commands.insert(commands.begin(), std::make_move_iterator(beginReset), std::make_move_iterator(pending.end()));
                pending.erase(beginReset, pending.end());
            }
            state.connected = current.connected && now - current.at < 300;
            state.edition = current.edition;
            // Release this client's session references before the service reset.
            // Do not enumerate or accept MIDI again until both restarts finish.
            for (const auto& command : pending) {
                if (command.operation == Operation::BeginAudioReset && !state.audioResetting) {
                    resumeAfterReset = state.active; halt(); audio.Clear(); state.audioResetting = true;
                    for (auto& row : state.rows) row.pickup.Disconnect();
                    state.notice = L"Audio reset in progress. MIDI output is temporarily paused.";
                } else if (command.operation == Operation::EndAudioReset && state.audioResetting) {
                    state.audioResetting = false; reset(); discoverAt = 0;
                    for (auto& row : state.rows) row.pickup.Disconnect();
                    state.active = resumeAfterReset && command.value && state.connected && state.confirmed;
                    state.notice = command.name;
                    resumeAfterReset = false;
                } else if (command.operation == Operation::Stop && state.audioResetting) resumeAfterReset = false;
            }
            if (state.audioResetting) {
                if (emergencyStop.exchange(false) || !state.connected) resumeAfterReset = false;
                inbox.Clear();
                { std::lock_guard lock(viewMutex); published = state; }
                std::unique_lock lock(waitMutex);
                wake.wait_for(lock, stop, std::chrono::milliseconds(25), [] { return false; });
                continue;
            }
            if (emergencyStop.exchange(false) || !state.connected) {
                if (state.active || state.learning) state.notice = L"Application control stopped: MIDI/VoiceMeeter connection changed. Reconnect, then Start applications.";
                halt();
            }
            if (now - lastTick > 250 || inbox.dropped != drops) { reset(); drops = inbox.dropped; }
            lastTick = now;
            if (now >= discoverAt) {
                audio.Discover(); state.endpoints = audio.Endpoints(); discoverAt = now + 2000;
                for (int route = 0; route < RouteCount; ++route) {
                    std::vector<Endpoint> matches;
                    for (const auto& endpoint : state.endpoints) if (endpoint.route == route) matches.push_back(endpoint);
                    state.selected[route] = matches.size() == 1 ? matches.front().id : L"";
                }
            }
            state.sessions = audio.Read();
            SyncApplications(state.rows, state.endpoints, state.sessions, state.edition, nextId);
            auto reconcile = [&] { for (auto& row : state.rows) {
                const bool conflict = Conflict(row.mapping.source, current.reserved, state.rows, row.mapping.id) || !RouteAvailable(row.mapping.route, state.edition) ||
                    std::any_of(state.rows.begin(), state.rows.end(), [&](const Row& other) { return other.mapping.id != row.mapping.id && other.mapping.Enabled() && SameTarget(row.mapping, other.mapping); });
                if (conflict != row.blocked) row.Reset();
                row.blocked = conflict;
                row.pickup.tolerance = state.tolerance;
                row.Observe(Resolve(state.sessions, row.mapping.endpoint, row.mapping.appKey), now);
            } };
            reconcile();
            if (state.learning && std::none_of(state.rows.begin(), state.rows.end(), [&](const Row& r) { return r.mapping.id == state.learning && r.target.ready; })) {
                halt(); state.notice = L"Learn cancelled because that application disconnected. Its saved assignment is retained.";
            }
            for (const auto& command : pending) {
                auto found = std::find_if(state.rows.begin(), state.rows.end(), [&](const Row& r) { return r.mapping.id == command.row; });
                Row* row = found == state.rows.end() ? nullptr : &*found;
                if (row && !Configured(row->mapping) &&
                    (command.operation == Operation::Learn || command.operation == Operation::Mode || command.operation == Operation::Hide || (command.operation == Operation::Use && command.value)) &&
                    std::count_if(state.rows.begin(), state.rows.end(), [](const Row& r) { return Configured(r.mapping); }) >= 128) {
                    state.notice = L"Save up to 128 application controls. Clear an unused assignment first."; continue;
                }
                switch (command.operation) {
                case Operation::BeginAudioReset: case Operation::EndAudioReset: break;
                case Operation::Endpoint:
                    if (!RouteAvailable(command.route, state.edition)) break;
                    if (std::none_of(state.endpoints.begin(), state.endpoints.end(), [&](const Endpoint& e) { return e.id == command.key && e.route == command.route; })) break;
                    // Existing mappings retain their explicit endpoint identity.
                    state.selected[command.route] = command.key; state.dirty = true; break;
                case Operation::Add: {
                    const int route = row ? row->mapping.route : command.route;
                    if (!RouteAvailable(route, state.edition)) break;
                    if (std::count_if(state.rows.begin(), state.rows.end(), [](const Row& r) { return Configured(r.mapping); }) >= 128) { state.notice = L"Save up to 128 application controls."; break; }
                    const auto endpoint = row ? row->mapping.endpoint : state.selected[route];
                    const auto key = row ? row->mapping.appKey : command.key;
                    const auto app = std::find_if(state.sessions.begin(), state.sessions.end(), [&](const Session& s) { return s.endpoint == endpoint && s.appKey == key; });
                    if (app == state.sessions.end()) { state.notice = L"That application is no longer connected. Start playback and refresh the list."; break; }
                    Row added; added.mapping = {nextId++, route, endpoint, app->appKey, app->name, {}, Action::Volume, false};
                    if (std::any_of(state.rows.begin(), state.rows.end(), [&](const Row& r) { return SameTarget(r.mapping, added.mapping); })) {
                        // Allow a second row for a separate mute button.
                        added.mapping.action = Action::Mute;
                        if (std::any_of(state.rows.begin(), state.rows.end(), [&](const Row& r) { return SameTarget(r.mapping, added.mapping); })) { state.notice = L"This app already has volume and mute rows."; break; }
                    }
                    halt(); state.rows.push_back(std::move(added)); state.confirmed = false; state.dirty = true;
                    state.notice = L"Extra control added. Tick Use, select its row, then Learn."; break;
                }
                case Operation::Remove:
                    if (row) { halt(); state.rows.erase(found); state.dirty = true; } break;
                case Operation::Hide:
                    if (row) {
                        HideRow(*row); if (state.learning == row->mapping.id) state.learning = 0;
                        state.dirty = true;
                        state.notice = L"Row removed from Elka and its MIDI control disabled. Save applications to remember; Restore removed brings it back. VoiceMeeter's list is unchanged.";
                    } break;
                case Operation::RestoreHidden:
                    if (RouteAvailable(command.route, state.edition)) {
                        for (auto& r : state.rows) if (r.mapping.route == command.route && r.mapping.hidden) RestoreRow(r);
                        state.dirty = true;
                        state.notice = L"Removed rows restored on this input. Tick Use to enable them again, then Save applications.";
                    } break;
                case Operation::Learn:
                    if (row && !row->mapping.hidden && state.connected) {
                        const bool cancel = state.learning == command.row; halt();
                        state.learning = cancel ? 0 : command.row;
                        state.notice = cancel ? L"Learn cancelled." : L"Move one control. CC learns a fader/knob; a note learns a mute button. No application writes during Learn.";
                    } else state.notice = L"Connect MIDI and VoiceMeeter before learning.";
                    break;
                case Operation::Use:
                    if (row && !row->mapping.hidden) {
                        halt(); row->mapping.use = command.value != 0; row->failed = false; state.dirty = true;
                    } break;
                case Operation::Mode:
                    if (row && command.value >= 0 && command.value <= 3) {
                        halt(); row->mapping.action = static_cast<Action>(command.value);
                        if (row->mapping.source.kind == 1 && row->mapping.action != Action::Mute) row->mapping.source = {};
                        row->failed = false; state.dirty = true; state.confirmed = false;
                    } break;
                case Operation::Start: {
                    halt();
                    if (!state.connected) { state.notice = L"Connect MIDI and VoiceMeeter in the main window first."; break; }
                    if (!state.confirmed) { state.notice = L"Disable the new controls' duplicate mappings, then tick the confirmation."; break; }
                    const bool any = std::any_of(state.rows.begin(), state.rows.end(), [&](const Row& r) { return r.mapping.Enabled() && r.mapping.source.Valid() && RouteAvailable(r.mapping.route, state.edition); });
                    const bool invalid = std::any_of(state.rows.begin(), state.rows.end(), [&](const Row& r) {
                        return r.mapping.Enabled() && RouteAvailable(r.mapping.route, state.edition) && (!r.mapping.source.Valid() || Conflict(r.mapping.source, current.reserved, state.rows, r.mapping.id) || r.blocked);
                    });
                    if (!any || invalid) { state.notice = L"Learn each enabled row and resolve duplicate MIDI sources or application targets. Strip/bus controls are reserved."; break; }
                    for (auto& r : state.rows) r.failed = false;
                    state.active = true; state.notice = L"Application control running. Faders wait for pickup. Release a mute button once before its first press."; break;
                }
                case Operation::Stop: halt(); state.notice = L"Application control stopped. Strip/bus pickup is unchanged."; break;
                case Operation::Rearm: reset(); for (auto& r : state.rows) r.failed = false; state.notice = L"Applications re-armed. Move each fader toward its current volume."; break;
                case Operation::Confirm: state.confirmed = command.value != 0; if (!state.confirmed) halt(); break;
                case Operation::Tolerance: state.tolerance = std::clamp(command.value, 0, 3); state.dirty = true; break;
                case Operation::Refresh:
                    reset(); audio.Refresh(); state.endpoints = audio.Endpoints(); state.sessions = audio.Read(); discoverAt = 0;
                    SyncApplications(state.rows, state.endpoints, state.sessions, state.edition, nextId);
                    state.notice = L"Application list refreshed. Closed apps are hidden; saved assignments return with them. Faders re-armed."; break;
                case Operation::Save: {
                    Setup setup; setup.endpoints = state.selected; setup.tolerance = state.tolerance;
                    for (const auto& r : state.rows) if (Configured(r.mapping)) setup.mappings.push_back(r.mapping);
                    if (SaveApplicationSetup(path, setup)) { state.dirty = false; state.notice = L"Application setup saved separately. This preview always starts stopped."; }
                    else state.notice = L"Could not save application setup beside the executable.";
                    break;
                }
                }
            }
            // UI edits can introduce conflicts or new targets in this same
            // cycle. Recompute before accepting any queued MIDI movement.
            if (!pending.empty()) reconcile();
            inbox.Drain(batch);
            for (size_t i = 0; i < batch.size; ++i) {
                const auto& packet = batch.messages[i];
                if (static_cast<DWORD>(GetTickCount() - packet.at) > 250) { reset(); continue; }
                const auto message = Decode(packet.data); if (!message) continue;
                if (state.learning) {
                    auto row = std::find_if(state.rows.begin(), state.rows.end(), [&](const Row& r) { return r.mapping.id == state.learning; });
                    if (row == state.rows.end() || (message->source.kind == 1 && message->value == 0)) continue;
                    if (Conflict(message->source, current.reserved, state.rows, row->mapping.id)) { state.notice = L"That MIDI control is already used. Move a different control, or disable its other row first."; continue; }
                    row->mapping.source = message->source; if (message->source.kind == 1) row->mapping.action = Action::Mute;
                    row->Reset(); row->pickup.hardware = message->value; row->failed = false;
                    state.learning = 0; state.confirmed = false; state.dirty = true;
                    state.notice = L"MIDI learned. Check the action, disable duplicate direct mappings, then Start applications.";
                    // The learn gesture is never replayed as volume/mute output.
                    break;
                }
                for (auto& row : state.rows) if (row.mapping.source == message->source) row.Input(message->value, state.active);
            }
            // A command or enumeration may have taken time. Require a fresh
            // main-worker heartbeat and discard output after any long stall.
            if (GetTickCount64() - now > 250) reset();
            bool fresh{};
            { std::lock_guard lock(inputMutex); fresh = link.connected && GetTickCount64() - link.at < 300; }
            if (state.active && fresh && !emergencyStop && !stop.stop_requested()) for (auto& row : state.rows) {
                if (!row.mapping.Enabled() || !row.Ready()) continue;
                { std::lock_guard lock(inputMutex);
                    if (Conflict(row.mapping.source, link.reserved, state.rows, row.mapping.id)) { row.Reset(); row.blocked = true; continue; }
                }
                const auto gain = row.VolumeWrite(); const auto mute = row.muteWrite;
                if (!gain && !mute) continue;
                if (!audio.Write(row.mapping, row.target, gain, mute)) {
                    row.Reset(); row.failed = true; state.notice = L"An application changed or rejected a write. That row is stopped; check its level and click Re-arm."; continue;
                }
                if (gain && row.mapping.action == Action::Volume) row.pickup.Written(Row::Encode(*gain), GetTickCount64());
                row.muteWrite.reset(); row.relativeWrite.reset(); ++row.writes;
            }
            if (!audio.error.empty()) state.notice = audio.error;
            if (now >= publishAt || !pending.empty()) { std::lock_guard lock(viewMutex); published = state; publishAt = now + 100; }
            std::unique_lock lock(waitMutex); wake.wait_for(lock, stop, std::chrono::milliseconds(state.active || state.learning ? 10 : 50), [] { return false; });
        }
    }
}
}

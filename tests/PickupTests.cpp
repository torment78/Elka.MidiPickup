#include "../app/Pickup.h"
#include "../app/MidiInbox.h"
#include "../app/Profile.h"
#include <array>
#include <thread>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

int checks{};
void Check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
#include "ApplicationTests.h"
#include "ApplicationPreview.h"
#include "AudioResetTests.h"
void BurstTests() {
    elka::MidiInbox inbox;
    elka::MidiInbox::Batch batch;
    for (DWORD i = 0; i < 4096; ++i) inbox.Receive(i % 2 ? MIM_MOREDATA : MIM_DATA, i, 100 + i);
    inbox.Drain(batch);
    Check(batch.size == 4096 && inbox.count == 4096 && inbox.bursts == 2048, "busy notifications preserve every valid MIDI packet");
    Check(inbox.errors == 0 && inbox.dropped == 0, "MIM_MOREDATA is not an error");
    for (DWORD i = 0; i < batch.size; ++i) Check(batch.messages[i].data == i && batch.messages[i].at == i + 100, "burst order and timestamp preserved");
    inbox.Receive(MIM_ERROR, 0, 0);
    Check(inbox.errors == 1 && inbox.count == 4096, "malformed input is reported separately");
    inbox.Reset();
    for (DWORD i = 0; i <= elka::MidiInbox::Capacity; ++i) inbox.Receive(MIM_DATA, i, 0);
    Check(inbox.dropped == 1 && inbox.errors == 0, "buffer overflow is counted without corrupting data");
    inbox.Clear(); inbox.Drain(batch);
    Check(batch.size == 0, "rearming discards queued old motion");
    inbox.Reset();
    std::atomic<bool> complete{};
    std::thread producer([&] {
        for (DWORD i = 1; i <= 100000; ++i) inbox.Receive(MIM_MOREDATA, i, i);
        complete = true;
    });
    uint64_t consumed{};
    DWORD previous{};
    bool ordered = true;
    do {
        inbox.Drain(batch);
        for (size_t i = 0; i < batch.size; ++i) {
            const auto packet = batch.messages[i];
            ordered = ordered && packet.data > previous && packet.data == packet.at;
            previous = packet.data; ++consumed;
        }
        std::this_thread::yield();
    } while (!complete || batch.size != 0);
    producer.join();
    // Drain once after joining in case the completion flag changed just after
    // an empty drain; no callback can still append at this point.
    inbox.Drain(batch);
    for (size_t i = 0; i < batch.size; ++i) {
        ordered = ordered && batch.messages[i].data > previous;
        previous = batch.messages[i].data; ++consumed;
    }
    Check(ordered && consumed + inbox.dropped == 100000 && inbox.errors == 0, "concurrent callback and worker preserve accounting and order");
}
void SweepTests() {
    struct Channel {
        struct Command { double value; uint64_t due; };
        elka::Pickup pickup;
        double mixer{-60};
        std::deque<Command> queued;
        uint64_t writes{};
    };
    for (const int tolerance : {0, 1, 2, 3}) {
        std::array<Channel, 16> channels;
        for (auto& c : channels) {
            c.pickup.range.high = 12; c.pickup.tolerance = tolerance;
            c.pickup.Observe(c.mixer, 0); c.pickup.Input(0, true);
        }
        for (uint64_t now = 1; now <= 6000; ++now) {
            for (size_t i = 0; i < channels.size(); ++i) {
                auto& c = channels[i];
                // Each physical fader crosses the entire range and reverses
                // every 127 ms. UI rendering is intentionally absent.
                const auto phase = static_cast<int>((now * (i + 1)) % 254);
                c.pickup.Input(phase <= 127 ? phase : 254 - phase, true);
                while (!c.queued.empty() && now >= c.queued.front().due) { c.mixer = std::round(c.queued.front().value * 10) / 10; c.queued.pop_front(); }
                if (now % 10 == 0) {
                    const auto result = c.pickup.Observe(c.mixer, now);
                    Check(result == elka::Pickup::Observation::Stable && c.pickup.tracking && !c.pickup.fault, "fast reversals and rounded asynchronous replies keep all faders tracking");
                    if (auto value = c.pickup.NextWrite()) {
                        c.queued.push_back({*value, now + 27 + i * 3});
                        c.pickup.Written(*value, now); ++c.writes;
                    }
                }
            }
        }
        for (auto& c : channels) {
            // Stop input on a non-integer dB value, then settle both pending
            // and coalesced writes. The final hardware position must win.
            c.pickup.Input(77, true);
            for (uint64_t now = 6010; now < 6500; now += 10) {
                while (!c.queued.empty() && now >= c.queued.front().due) { c.mixer = std::round(c.queued.front().value * 10) / 10; c.queued.pop_front(); }
                c.pickup.Observe(c.mixer, now);
                if (auto value = c.pickup.NextWrite()) { c.queued.push_back({*value, now + 40}); c.pickup.Written(*value, now); }
            }
            Check(c.writes > 500 && std::abs(c.mixer - c.pickup.range.Gain(77)) <= 0.051, "updates do not wait for readback and settle at newest physical position");
            c.pickup.Observe(-50, 6500);
            Check(!c.pickup.tracking && !c.pickup.NextWrite(), "real mouse edit still releases tracking after fast sweep");
            c.pickup.Input(70, true);
            Check(!c.pickup.tracking, "external edit clears crossing history even at tolerance three");
            c.pickup.Input(10, true);
            Check(c.pickup.tracking, "subsequent physical crossing reacquires edited target");
        }
    }
}
void ReadbackTests() {
    elka::Pickup p, other;
    p.range.high = 12; p.Observe(-60, 0); p.Input(0, true);
    other.Observe(-30, 0); other.Input(64, true);
    p.Input(80, true); const auto up = *p.NextWrite(); p.Written(up, 1);
    p.Input(0, true);
    Check(p.NextWrite() == -60, "returning to baseline must cancel an in-flight higher gain");
    p.Written(-60, 2);
    p.Observe(-60, 3);
    Check(p.tracking && p.Pending(), "old cached baseline cannot acknowledge a future reversal");
    p.Observe(up, 4); p.Observe(-60, 5);
    Check(p.tracking && !p.Pending(), "delayed up then down replies preserve tracking");
    p.Input(30, true); p.Written(*p.NextWrite(), 10);
    p.Input(50, true); const auto latest = *p.NextWrite(); p.Written(latest, 20);
    p.Observe(latest, 700);
    Check(p.tracking && !p.Pending(), "skipped intermediate replies and delays over 500 ms are accepted");
    p.Input(127, true); p.Written(*p.NextWrite(), 710);
    const auto timeout = p.Observe(latest, 2710);
    Check(timeout == elka::Pickup::Observation::ReadbackTimeout && !p.tracking && other.tracking, "unconfirmed write only rearms affected fader");
    p.Input(0, true); p.Input(127, true);
    Check(p.tracking && p.NextWrite().has_value(), "timeout recovers on fresh target crossing");
    p.Written(*p.NextWrite(), 2720); p.Observe(-15.3, 2721);
    Check(!p.tracking && !p.Pending(), "unmatched external edits cancel all pending desired movement");
}
void ProfileTests() {
    const auto path = std::filesystem::temp_directory_path() / (L"ElkaPickup-tests-" + std::to_wstring(GetCurrentProcessId()) + L".ini");
    elka::Setup source, loaded;
    source.input = L"MIDI device \u00f6"; source.launchTray = source.launchActive = source.launchWindows = source.confirmed = true;
    for (int i = 0; i < 16; ++i) source.mappings[i] = {i, 7, i, true};
    Check(elka::SaveProfile(path, source) && elka::LoadProfile(path, loaded), "saved setup roundtrips from disk");
    Check(loaded.input == source.input && loaded.top == 2 && loaded.tolerance == 3, "unicode device and gain settings survive restart");
    Check(loaded.launchTray && loaded.launchActive && loaded.launchWindows && loaded.confirmed, "startup settings and mapping acknowledgement survive restart");
    for (int i = 0; i < 16; ++i) Check(loaded.mappings[i].channel == i && loaded.mappings[i].target == i && loaded.mappings[i].use, "all sixteen strip and bus mappings survive restart");
    // Simulate a v1 file with mixed bus/strip assignments, including disabled ones.
    WritePrivateProfileStringW(L"App", L"Version", L"1", path.c_str());
    WritePrivateProfileStringW(L"Fader1", L"Target", L"11", path.c_str());
    WritePrivateProfileStringW(L"Fader2", L"Target", L"8", path.c_str());
    Check(elka::LoadProfile(path, loaded), "legacy mixed profile loads");
    Check(loaded.mappings[8].channel == 0 && loaded.mappings[8].target == 11 && loaded.mappings[9].channel == 1 && loaded.mappings[9].target == 8, "legacy buses migrate to bus page without changing destinations");
    Check(loaded.mappings[0].channel == 2 && loaded.mappings[0].target == 2 && !loaded.confirmed, "legacy strips retained and absent acknowledgement is not invented");
    WritePrivateProfileStringW(L"App", L"Version", L"999", path.c_str());
    Check(!elka::LoadProfile(path, loaded), "unknown profile cannot replace setup");
    Check(elka::SaveProfile(path, source), "restore valid fixture");
    WritePrivateProfileStringW(L"Fader1", L"Target", L"12", path.c_str());
    Check(elka::LoadProfile(path, loaded) && !loaded.mappings[0].use && !loaded.confirmed, "invalid cross-page target cannot silently control a different fader");
    std::filesystem::remove(path);
}
int MainWindowPreview();
int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--preview-main") return MainWindowPreview();
    if (argc == 5 && std::string(argv[1]) == "--session-child") return SessionChild(argv[2], argv[3], argv[4]);
    if (argc > 1 && std::string(argv[1]) == "--preview-window") return ApplicationPreview();
    if (argc > 1 && std::string(argv[1]) == "--inspect-audio") return InspectAudioSessions(false);
    if (argc > 1 && std::string(argv[1]) == "--audio-integration") return InspectAudioSessions(true);
    try {
        elka::Pickup p;
        p.tolerance = 0;
        p.Observe(p.range.Gain(89), 0);
        p.Input(25, true);
        Check(!p.tracking && !p.NextWrite(), "below target must wait");
        p.Input(80, true);
        Check(!p.tracking, "still below target");
        p.Input(90, true);
        Check(p.tracking && p.NextWrite().has_value(), "crossing picks up even without exact sample");
        const auto written = *p.NextWrite();
        p.Written(written, 10);
        Check(!p.NextWrite(), "unchanged input does not send duplicate writes");
        p.Observe(p.range.Gain(89), 20);
        Check(p.tracking && p.Pending(), "stale baseline is not an external edit");
        p.Input(95, true);
        Check(p.NextWrite().has_value(), "new input can advance before previous echo");
        const auto newer = *p.NextWrite(); p.Written(newer, 25);
        p.Observe(written, 30);
        Check(p.tracking && p.Pending() && !p.NextWrite(), "delayed own echo preserves tracking and newer pending write");
        p.Observe(newer, 35);
        Check(p.tracking && !p.Pending(), "newest readback retires pending commands");
        p.Observe(p.range.Gain(40), 40);
        Check(!p.tracking && !p.NextWrite(), "mouse change rearms and drops queued desired gain");
        p.Input(90, true);
        Check(!p.tracking, "stale crossing history cleared on external change");
        p.Input(30, true);
        Check(p.tracking, "downward crossing picks up");

        p.Reset(); p.Observe(-30, 100); p.Input(64, true);
        Check(!p.tracking, "zero tolerance requires crossing or exact match");
        p.tolerance = 1; p.Input(64, true);
        Check(p.tracking, "quantization tolerance permits nearby pickup");
        p.Written(*p.NextWrite(), 100);
        p.Observe(-30, 601);
        Check(!p.fault && p.tracking && p.Pending(), "half-second readback delay must not turn pickup off");
        const auto timeout = p.Observe(-30, 2100);
        Check(timeout == elka::Pickup::Observation::ReadbackTimeout && !p.fault && !p.tracking && !p.NextWrite(), "two-second timeout rearms only affected fader and drops old desired gain");
        p.Input(100, true);
        Check(!p.NextWrite(), "movement away from target after timeout cannot replay old motion");
        p.Input(60, true);
        Check(p.tracking && p.NextWrite().has_value(), "fresh crossing recovers without Start or reconnect");

        p.Reset(); p.Observe(6, 700); p.Input(127, true);
        Check(!p.Reachable() && !p.tracking, "software gain outside range must never be clamped into pickup");
        p.range.high = 12; p.Reset(); p.Input(116, true); p.Input(118, true);
        Check(p.tracking, "extended range allows positive target");
        p.Disconnect(); p.Observe(-10, 800); p.Input(0, true);
        Check(!p.tracking && !p.Pending(), "reconnect clears latch and pending write");
        p.Reset(); p.Input(127, false);
        Check(!p.tracking && !p.NextWrite(), "monitor mode cannot write");
        p.Observe(std::numeric_limits<double>::quiet_NaN(), 900);
        Check(p.fault && !p.Reachable(), "nonfinite software value blocks output");

        elka::Pickup a, b;
        a.Observe(-30, 0); b.Observe(-10, 0);
        a.Input(64, true); b.Input(10, true);
        Check(a.tracking && !b.tracking, "fader pickup is independent");
        a.WriteFailed();
        Check(a.fault && !a.NextWrite() && !b.fault, "write failure isolates engine and stops writes");
        b.Input(-1, true); b.Input(128, true);
        Check(b.hardware == 10, "invalid MIDI values are ignored");
        b.range = {-60, -60};
        Check(!b.Reachable(), "invalid gain range rejected");

        elka::Pickup external;
        external.Observe(-30, 0); external.Input(64, true);
        external.Written(*external.NextWrite(), 1);
        external.Observe(-20, 2);
        Check(!external.tracking && !external.Pending() && !external.NextWrite(), "external edit during pending write rearms");
        for (int value = 0; value < 128; ++value) {
            elka::GainRange range;
            Check(std::abs(range.Position(range.Gain(value)) - value) < 0.00001, "gain mapping round trip");
        }
        BurstTests();
        SweepTests();
        ReadbackTests();
        ProfileTests();
        ApplicationTests();
        AudioResetTests();
        std::cout << "PASS: " << checks << " pickup checks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}

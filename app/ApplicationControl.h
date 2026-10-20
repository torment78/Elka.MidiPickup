#pragma once
#include "ApplicationMidi.h"
#include "ApplicationProfile.h"
#include "MidiInbox.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace elka::apps {
enum class Operation { Add, Remove, Learn, Use, Mode, Endpoint, Start, Stop, Rearm, Save, Confirm, Tolerance, Refresh, Hide, RestoreHidden, BeginAudioReset, EndAudioReset };
struct Command {
    Operation operation;
    uint64_t row{};
    int value{}, route{};
    std::wstring key, name;
};
struct View {
    std::vector<Endpoint> endpoints;
    std::vector<Session> sessions;
    std::vector<Row> rows;
    std::array<std::wstring, RouteCount> selected;
    std::wstring notice = L"Connect MIDI and VoiceMeeter in the main window. Application control starts stopped.";
    bool active{}, connected{}, confirmed{}, dirty{}, audioResetting{};
    int edition{}, tolerance{3};
    uint64_t learning{};
};
class Controller {
    struct Link {
        bool connected{};
        int edition{};
        uint64_t at{};
        std::vector<Source> reserved;
    };
    std::mutex inputMutex, viewMutex, waitMutex;
    Link link;
    std::vector<Command> commands;
    MidiInbox inbox;
    std::condition_variable_any wake;
    std::jthread worker;
    std::atomic<bool> launched{}, emergencyStop{};
    View published;
    void Run(std::stop_token stop, std::filesystem::path path);
public:
    ~Controller() { Shutdown(); }
    void Launch(const std::filesystem::path& path);
    void Shutdown();
    bool Launched() const { return launched; }
    void Bridge(bool connected, int edition, const std::vector<Source>& reserved);
    void Midi(DWORD data, DWORD at) { if (launched) inbox.Receive(MIM_DATA, data, at); }
    void Suspend() { if (launched) emergencyStop = true; }
    void Send(Command command) { std::lock_guard lock(inputMutex); commands.push_back(std::move(command)); wake.notify_all(); }
    View Snapshot() { std::lock_guard lock(viewMutex); return published; }
    bool Reserved(const Source& source) {
        std::lock_guard lock(viewMutex);
        return std::any_of(published.rows.begin(), published.rows.end(), [&](const Row& r) { return r.mapping.Enabled() && RouteAvailable(r.mapping.route, published.edition) && r.mapping.source == source; });
    }
};
}

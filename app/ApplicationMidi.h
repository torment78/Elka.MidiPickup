#pragma once
#include "Pickup.h"
#include <string>
#include <vector>
#include <set>
#include <cwctype>

namespace elka::apps {
inline std::wstring Lower(std::wstring value) {
    for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c));
    return value;
}
struct Source {
    int kind{-1}, channel{-1}, number{-1}; // 0 = CC, 1 = note (on/off share a source)
    bool Valid() const { return kind >= 0 && kind <= 1 && channel >= 0 && channel < 16 && number >= 0 && number < 128; }
    bool operator==(const Source&) const = default;
};
struct Message { Source source; int value{}; };
inline std::optional<Message> Decode(uint32_t data) {
    const int status = data & 0xf0;
    if (status != 0xb0 && status != 0x90 && status != 0x80) return {};
    if ((data & 0x00808000) != 0) return {};
    return Message{{status == 0xb0 ? 0 : 1, static_cast<int>(data & 15), static_cast<int>((data >> 8) & 127)},
        status == 0x80 ? 0 : static_cast<int>((data >> 16) & 127)};
}
enum class Action { Volume, Mute, RelativeTwos, RelativeOffset };
inline int Delta(Action action, int value) {
    if (action == Action::RelativeTwos) return value < 64 ? value : value - 128;
    if (action == Action::RelativeOffset) return value - 64;
    return 0;
}
struct Endpoint { std::wstring id, name; int route{-1}; bool voiceMeeter{}; };
inline constexpr int RouteCount = 8; // Keep preview.2 endpoint/profile slots readable.
inline constexpr int InputCount = 3; // Application control is limited to VAIO, AUX, VAIO 3.
inline bool RouteAvailable(int route, int edition) {
    return edition >= 1 && edition <= 3 && route >= 0 && route < InputCount && route < edition;
}
inline const wchar_t* RouteName(int route) {
    constexpr const wchar_t* names[] = {L"VAIO", L"AUX", L"VAIO 3", L"In 1", L"In 2", L"In 3", L"In 4", L"In 5"};
    return route >= 0 && route < RouteCount ? names[route] : L"Unknown input";
}
inline int EndpointRoute(const std::wstring& label) {
    const auto name = Lower(label);
    if (name.starts_with(L"voicemeeter vaio3 input")) return 2;
    if (name.starts_with(L"voicemeeter aux input")) return 1;
    if (name.starts_with(L"voicemeeter input")) return 0;
    for (int i = 1; i <= 5; ++i) {
        const auto prefix = L"voicemeeter in " + std::to_wstring(i);
        if (name == prefix || name.starts_with(prefix + L" ")) return i + 2;
    }
    return -1;
}
struct Session {
    std::wstring endpoint, appKey, name, instance;
    double volume{};
    bool muted{}, readable{};
};
struct Target {
    std::vector<std::wstring> instances;
    double volume{};
    bool muted{}, ready{}, mixedVolume{}, mixedMute{};
};
inline Target Resolve(const std::vector<Session>& sessions, const std::wstring& endpoint, const std::wstring& key) {
    Target result;
    bool first = true, valid = true;
    for (const auto& s : sessions) if (s.endpoint == endpoint && s.appKey == key) {
        result.instances.push_back(s.instance);
        valid = valid && s.readable && std::isfinite(s.volume) && s.volume >= 0 && s.volume <= 1;
        if (first) { result.volume = s.volume; result.muted = s.muted; first = false; }
        else { result.mixedVolume |= std::abs(result.volume - s.volume) > 0.0008; result.mixedMute |= result.muted != s.muted; }
    }
    std::sort(result.instances.begin(), result.instances.end());
    result.ready = !first && valid;
    return result;
}
struct Mapping {
    uint64_t id{};
    int route{};
    std::wstring endpoint, appKey, name;
    Source source;
    Action action{Action::Volume};
    bool use{true};
    bool hidden{};
    bool Enabled() const { return use && !hidden && route >= 0 && route < InputCount; }
};
struct Row {
    Mapping mapping;
    Pickup pickup;
    Target target;
    bool blocked{}, failed{}, down{}, releaseRequired{true};
    std::optional<bool> muteWrite;
    std::optional<double> relativeWrite;
    uint64_t writes{};
    // Use the proven pickup engine in a linear scalar coordinate. Displayed
    // dB is computed separately; MIDI 0 is real silence, MIDI 127 is unity.
    static double Encode(double scalar) { return scalar * 60.0 - 60.0; }
    static double DecodeGain(double coordinate) { return std::clamp((coordinate + 60.0) / 60.0, 0.0, 1.0); }
    void Reset() { pickup.Reset(); down = false; releaseRequired = true; muteWrite.reset(); relativeWrite.reset(); }
    void Observe(Target next, uint64_t now) {
        if (next.instances != target.instances || next.ready != target.ready || next.mixedVolume != target.mixedVolume || next.mixedMute != target.mixedMute) Reset();
        target = std::move(next);
        if (target.ready && !target.mixedVolume) pickup.Observe(Encode(target.volume), now);
        else { pickup.Reset(); pickup.software.reset(); }
    }
    bool Ready() const { return target.ready && !blocked && !failed && (mapping.action == Action::Mute ? !target.mixedMute : !target.mixedVolume); }
    void Input(int value, bool active) {
        if (mapping.action == Action::Volume) { pickup.Input(value, active && mapping.Enabled() && Ready()); return; }
        pickup.hardware = value;
        if (!active || !mapping.Enabled() || !Ready()) { down = false; releaseRequired = true; return; }
        if (mapping.action == Action::Mute) {
            const bool pressed = mapping.source.kind == 1 ? value > 0 : value >= 64;
            if (!pressed) { releaseRequired = false; down = false; }
            else if (!down && !releaseRequired) { muteWrite = !muteWrite.value_or(target.muted); down = true; }
        } else {
            const int delta = Delta(mapping.action, value);
            if (delta) relativeWrite = std::clamp(relativeWrite.value_or(target.volume) + delta / 127.0, 0.0, 1.0);
        }
    }
    std::optional<double> VolumeWrite() const {
        if (!Ready() || !mapping.Enabled()) return {};
        if (mapping.action == Action::Volume) { const auto value = pickup.NextWrite(); return value ? std::optional<double>(DecodeGain(*value)) : std::nullopt; }
        return relativeWrite;
    }
};
inline bool Conflict(const Source& source, const std::vector<Source>& reserved, const std::vector<Row>& rows, uint64_t except) {
    if (!source.Valid()) return false;
    if (std::find(reserved.begin(), reserved.end(), source) != reserved.end()) return true;
    return std::any_of(rows.begin(), rows.end(), [&](const Row& r) { return r.mapping.id != except && r.mapping.Enabled() && r.mapping.source == source; });
}
inline bool SameTarget(const Mapping& a, const Mapping& b) {
    return a.endpoint == b.endpoint && a.appKey == b.appKey && (a.action == Action::Mute) == (b.action == Action::Mute);
}
inline bool Configured(const Mapping& m) { return m.hidden || m.use || m.source.Valid() || m.action != Action::Volume; }
inline bool VisibleRow(const Row& row, int route) {
    return row.mapping.route == route && !row.mapping.hidden && (Configured(row.mapping) || !row.target.instances.empty());
}
inline bool VisibleRoute(const std::vector<Row>& rows, int route, int edition) {
    return route >= 0 && route < InputCount && (RouteAvailable(route, edition) ||
        std::any_of(rows.begin(), rows.end(), [&](const Row& row) { return VisibleRow(row, route); }));
}
inline std::wstring UnavailableNotice(const Row& row, int edition, const std::vector<Endpoint>& endpoints, const std::vector<Session>& sessions) {
    if (!RouteAvailable(row.mapping.route, edition)) return L"Input unavailable";
    if (std::none_of(endpoints.begin(), endpoints.end(), [&](const Endpoint& e) { return e.id == row.mapping.endpoint; })) return L"Input offline";
    if (row.target.ready) return {};
    if (!row.target.instances.empty()) return L"Session unavailable";
    std::set<int> elsewhere;
    for (const auto& s : sessions) if (s.appKey == row.mapping.appKey && s.endpoint != row.mapping.endpoint) {
        const auto endpoint = std::find_if(endpoints.begin(), endpoints.end(), [&](const Endpoint& e) { return e.id == s.endpoint; });
        if (endpoint != endpoints.end()) elsewhere.insert(endpoint->route);
    }
    if (elsewhere.size() == 1) return L"Offline / on " + std::wstring(RouteName(*elsewhere.begin()));
    return elsewhere.empty() ? L"App offline" : L"On other inputs";
}
inline void HideRow(Row& row) {
    // Suppress rediscovery for this run only. No MIDI binding survives deletion.
    row.mapping.hidden = true; row.mapping.use = false;
    row.mapping.source = {}; row.mapping.action = Action::Volume; row.Reset();
}
inline void RestoreRow(Row& row) {
    row.mapping.hidden = false; row.mapping.use = false; row.Reset();
}
// Reconcile by endpoint + executable, never by row order. Configured offline
// rows remain visible on their saved input, even without a present session.
inline void SyncApplications(std::vector<Row>& rows, const std::vector<Endpoint>& endpoints, const std::vector<Session>& sessions, int edition, uint64_t& nextId) {
    std::erase_if(rows, [&](const Row& r) {
        return !Configured(r.mapping) && std::none_of(sessions.begin(), sessions.end(), [&](const Session& s) { return s.endpoint == r.mapping.endpoint && s.appKey == r.mapping.appKey; });
    });
    for (auto& row : rows) {
        const auto endpoint = std::find_if(endpoints.begin(), endpoints.end(), [&](const Endpoint& e) { return e.id == row.mapping.endpoint; });
        // Migrates old manually chosen endpoints without retargeting audio.
        if (endpoint != endpoints.end()) row.mapping.route = endpoint->route;
    }
    for (const auto& s : sessions) {
        const auto endpoint = std::find_if(endpoints.begin(), endpoints.end(), [&](const Endpoint& e) { return e.id == s.endpoint && e.voiceMeeter && RouteAvailable(e.route, edition); });
        if (endpoint == endpoints.end()) continue;
        if (std::any_of(rows.begin(), rows.end(), [&](const Row& r) { return r.mapping.endpoint == s.endpoint && r.mapping.appKey == s.appKey; })) continue;
        Row row; row.mapping = {nextId++, endpoint->route, s.endpoint, s.appKey, s.name, {}, Action::Volume, false};
        rows.push_back(std::move(row));
    }
}
}

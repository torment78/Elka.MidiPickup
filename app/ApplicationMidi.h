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
        if (mapping.action == Action::Volume) { pickup.Input(value, active && mapping.use && Ready()); return; }
        pickup.hardware = value;
        if (!active || !mapping.use || !Ready()) { down = false; releaseRequired = true; return; }
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
        if (!Ready() || !mapping.use) return {};
        if (mapping.action == Action::Volume) { const auto value = pickup.NextWrite(); return value ? std::optional<double>(DecodeGain(*value)) : std::nullopt; }
        return relativeWrite;
    }
};
inline bool Conflict(const Source& source, const std::vector<Source>& reserved, const std::vector<Row>& rows, uint64_t except) {
    if (!source.Valid()) return false;
    if (std::find(reserved.begin(), reserved.end(), source) != reserved.end()) return true;
    return std::any_of(rows.begin(), rows.end(), [&](const Row& r) { return r.mapping.id != except && r.mapping.use && r.mapping.source == source; });
}
inline bool SameTarget(const Mapping& a, const Mapping& b) {
    return a.endpoint == b.endpoint && a.appKey == b.appKey && (a.action == Action::Mute) == (b.action == Action::Mute);
}
}

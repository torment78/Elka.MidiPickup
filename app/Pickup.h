#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>

namespace elka {
struct GainRange {
    double low{-60.0}, high{0.0};
    bool Valid() const { return std::isfinite(low) && std::isfinite(high) && low >= -60.0 && high <= 12.0 && low < high; }
    double Gain(int value) const { return low + (high - low) * std::clamp(value, 0, 127) / 127.0; }
    double Position(double gain) const { return (gain - low) * 127.0 / (high - low); }
};

// Match asynchronous readbacks against recent writes. This lets hardware
// advance without waiting for every intermediate mixer acknowledgement.
// The application serializes access.
class Pickup {
public:
    enum class Observation { Stable, ExternalEdit, ReadbackTimeout, Invalid };
    static constexpr uint64_t ReadbackTimeoutMs = 2000;
    GainRange range;
    int tolerance{3};
    std::optional<int> hardware;
    std::optional<double> software;
    bool tracking{}, fault{};

    void Reset() {
        tracking = false; fault = false; previous.reset(); desired.reset(); pending.clear();
    }
    void Disconnect() { Reset(); software.reset(); hardware.reset(); }
    bool Reachable() const {
        return range.Valid() && software && std::isfinite(*software) && *software >= range.low - epsilon && *software <= range.high + epsilon;
    }
    bool Pending() const { return !pending.empty(); }
    Observation Observe(double value, uint64_t now) {
        if (!std::isfinite(value)) { Reset(); software.reset(); fault = true; return Observation::Invalid; }
        if (!pending.empty()) {
            // Re-reading the old cache is not an acknowledgement of a later
            // command returning to that same gain during a fast reversal.
            if (Near(value, *software)) {
                if (now - pending.front().sentAt < ReadbackTimeoutMs) return Observation::Stable;
                if (Near(value, pending.back().value)) { pending.clear(); return Observation::Stable; }
                Reset(); software = value; return Observation::ReadbackTimeout;
            }
            // Use the first match: a fast reversal can send the same value
            // twice, and retiring the second would misidentify an intervening
            // delayed echo as an external edit. Snapshots may skip writes.
            for (size_t i = 0; i < pending.size(); ++i) if (Near(value, pending[i].value)) {
                pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(i + 1));
                software = value; return Observation::Stable;
            }
            // A different value while awaiting readback is an external edit.
            Reset(); software = value; return Observation::ExternalEdit;
        }
        const bool changed = software && !Near(value, *software);
        if (changed) Reset();
        software = value;
        return changed ? Observation::ExternalEdit : Observation::Stable;
    }
    void Input(int value, bool active) {
        if (value < 0 || value > 127) return;
        hardware = value;
        if (!active || fault || !Reachable()) { previous.reset(); return; }
        if (!tracking) {
            const double target = range.Position(*software);
            const bool near = std::abs(value - target) <= std::clamp(tolerance, 0, 3);
            const bool crossed = previous && ((*previous < target && value >= target) || (*previous > target && value <= target));
            if (near || crossed) tracking = true;
        }
        previous = value;
        if (tracking) desired = range.Gain(value);
    }
    std::optional<double> NextWrite() const {
        if (fault || !tracking || !desired || !Reachable() || pending.size() >= 256) return {};
        // Also send a return to the last confirmed gain if a different command
        // is still in flight, otherwise a fast reversal could finish too high.
        if (Near(*desired, pending.empty() ? *software : pending.back().value)) return {};
        return desired;
    }
    void Written(double value, uint64_t now) { pending.push_back(Write{value, now}); }
    std::optional<double> PendingValue() const { return pending.empty() ? std::nullopt : std::optional<double>(pending.back().value); }
    void WriteFailed() { Reset(); fault = true; }
private:
    // Accept a nearest-tenth-dB readback (max 0.05 dB rounding error).
    // Pickup tolerance is separate: it must never cause an acquired fader
    // to drop tracking just because hardware is ahead of mixer readback.
    static constexpr double epsilon = 0.051;
    static bool Near(double a, double b) { return std::abs(a - b) <= epsilon; }
    struct Write { double value; uint64_t sentAt; };
    std::deque<Write> pending;
    std::optional<int> previous;
    std::optional<double> desired;
};
}

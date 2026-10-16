#pragma once
#include <Windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace elka {
struct MidiMessage { DWORD data{}, at{}; };

// The driver callback only copies into fixed storage. In particular,
// MIM_MOREDATA carries valid data and must not PostMessage or stop pickup.
class MidiInbox {
public:
    static constexpr size_t Capacity = 8192;
    struct Batch { std::array<MidiMessage, Capacity> messages{}; size_t size{}; };
    std::atomic<uint64_t> count{}, errors{}, bursts{}, dropped{};
    void Receive(UINT event, DWORD data, DWORD at) {
        if (event == MIM_DATA || event == MIM_MOREDATA) {
            count.fetch_add(1, std::memory_order_relaxed);
            if (event == MIM_MOREDATA) bursts.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard lock(mutex);
            if (size == Capacity) { dropped.fetch_add(1, std::memory_order_relaxed); return; }
            messages[size++] = {data & 0x00ffffff, at};
        } else if (event == MIM_ERROR || event == MIM_LONGERROR) errors.fetch_add(1, std::memory_order_relaxed);
    }
    void Drain(Batch& batch) {
        std::lock_guard lock(mutex);
        batch.size = size;
        std::copy_n(messages.begin(), size, batch.messages.begin());
        size = 0;
    }
    void Clear() { std::lock_guard lock(mutex); size = 0; }
    void Reset() { Clear(); count = 0; errors = 0; bursts = 0; dropped = 0; }
private:
    std::mutex mutex;
    std::array<MidiMessage, Capacity> messages{};
    size_t size{};
};
}

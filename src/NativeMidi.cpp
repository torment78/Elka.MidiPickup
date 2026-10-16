#include "Probe.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Midi2.h>
#include <winrt/Windows.Devices.Midi2.Enumeration.h>
#include <winrt/Windows.Devices.Midi2.Transports.Loopback.h>
#include <array>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <vector>

using namespace winrt::Windows::Devices::Midi2;
using namespace winrt::Windows::Devices::Midi2::Enumeration;

namespace {
struct SessionScope {
    MidiSession session{nullptr};
    ~SessionScope() { if (session) { try { session.Close(); } catch (...) {} } }
};

struct Received {
    std::mutex mutex;
    std::condition_variable ready;
    std::array<uint32_t, 128> words{};
    size_t count{};
};

// Sends only to the two Windows diagnostic loopback endpoints. Never sends to
// a hardware device, a normal loopback port, or VoiceMeeter.
bool CheckDiagnosticDirection(MidiEndpointConnection const& sender, MidiEndpointConnection const& receiver) {
    auto received = std::make_shared<Received>();
    auto subscription = receiver.MessageReceived(winrt::auto_revoke,
        [received](IMidiMessageReceivedEventSource const&, MidiMessageReceivedEventArgs const& args) {
            const auto word = args.PeekFirstWord();
            if ((word & 0xffffff80u) != 0x20bf7700u) return;
            std::lock_guard lock(received->mutex);
            if (received->count < received->words.size()) received->words[received->count] = word;
            ++received->count;
            received->ready.notify_one();
        });
    for (uint32_t i = 0; i < 128; ++i) {
        const auto result = sender.SendSingleMessageWords(0, 0x20bf7700u | i);
        if (MidiEndpointConnection::SendMessageFailed(result)) {
            std::wcout << L"Diagnostic send failed at message " << i << L"; flags=" << static_cast<uint32_t>(result) << L'\n';
            return false;
        }
    }
    std::unique_lock lock(received->mutex);
    received->ready.wait_for(lock, std::chrono::seconds(5), [&] { return received->count >= 128; });
    bool matches = received->count == 128;
    for (uint32_t i = 0; matches && i < 128; ++i) matches = received->words[i] == (0x20bf7700u | i);
    std::wcout << L"  Sent 128, received " << received->count << L"; exact ordered payload: " << (matches ? L"PASS" : L"FAIL") << L'\n';
    return matches;
}
}

int RunNativeProbe() {
    if (WindowsBuild() < 26200) {
        std::wcout << L"Native preview API requires Windows 11 25H2 or newer.\n";
        return 3;
    }
    if (!MidiServiceRunning()) {
        std::wcout << L"MIDI service is not confirmed running; no service changes attempted.\n";
        return 3;
    }
    winrt::init_apartment();
    int result = 2;
    try {
        std::wcout << L"Native API source: " << (MidiApi::IsSystemProvided() ? L"Windows" : L"app-local Microsoft Preview 11") << L'\n';
        std::wcout << L"API mode=" << static_cast<int32_t>(MidiApi::GetCurrentlySelectedApiMode()) << L'\n';
        if (!MidiApi::EnsureServiceAvailable()) throw std::runtime_error("Native API reports service unavailable.");
        const auto endpoints = MidiEndpointDeviceInformation::FindAll(
            MidiEndpointDeviceInformationSortOrder::Name,
            MidiEndpointDeviceInformationFilters::AllStandardEndpoints | MidiEndpointDeviceInformationFilters::DiagnosticLoopback);
        std::vector<winrt::hstring> diagnostics;
        std::wcout << L"Native endpoints: " << endpoints.Size() << L'\n';
        for (auto const& endpoint : endpoints) {
            std::wcout << L"  " << endpoint.Name().c_str() << L"; purpose=" << static_cast<int32_t>(endpoint.EndpointPurpose()) << L'\n';
            if (endpoint.EndpointPurpose() == MidiEndpointDevicePurpose::DiagnosticLoopback)
                diagnostics.push_back(endpoint.EndpointDeviceId());
        }
        std::wcout << L"Native MIDI 2 loopback transport available: "
            << winrt::Windows::Devices::Midi2::Transports::Loopback::MidiLoopbackManager::IsTransportAvailable() << L'\n';
        if (diagnostics.size() != 2) {
            std::wcout << L"Expected exactly two diagnostic loopback endpoints; no test messages sent.\n";
            result = 3;
        } else {
            SessionScope scope{MidiSession::Create(L"Elka MIDI Pickup isolated compatibility probe")};
            if (!scope.session) throw std::runtime_error("MIDI session creation returned null.");
            const auto a = scope.session.CreateEndpointConnection(diagnostics[0]);
            const auto b = scope.session.CreateEndpointConnection(diagnostics[1]);
            if (!a || !b || !a.Open() || !b.Open()) throw std::runtime_error("Diagnostic endpoint open failed.");
            std::wcout << L"Native diagnostic loopback A -> B:\n";
            const bool forward = CheckDiagnosticDirection(a, b);
            std::wcout << L"Native diagnostic loopback B -> A:\n";
            const bool backward = CheckDiagnosticDirection(b, a);
            result = forward && backward ? 0 : 2;
            scope.session.DisconnectEndpointConnection(a.ConnectionId());
            scope.session.DisconnectEndpointConnection(b.ConnectionId());
        }
        std::wcout << L"\nApp-owned virtual endpoint: NOT CREATED (Microsoft #1047; installed transport "
            << FileVersion(SystemFile(L"Midi2.VirtualMidiTransport.dll")) << L").\n"
            << L"User loopback creation: NOT ATTEMPTED; compatibility with the installed transport is not established.\n"
            << L"VoiceMeeter virtual-port recognition and physical pass-through remain NOT VERIFIED.\n";
    } catch (winrt::hresult_error const& error) {
        std::wcout << L"Native probe failed: HRESULT 0x" << std::hex << static_cast<uint32_t>(error.code().value)
            << std::dec << L" " << error.message().c_str() << L'\n';
    } catch (std::exception const& error) {
        std::cerr << "Native probe failed: " << error.what() << '\n';
    }
    winrt::clear_factory_cache();
    winrt::uninit_apartment();
    return result;
}

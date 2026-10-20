#pragma once
#include "../app/AudioReset.h"

void AudioResetTests() {
    using namespace elka::audio_reset;
    Sequence sequence;
    Check(sequence.Begin() && !sequence.Begin(), "reset rejects repeated clicks");
    Check(!sequence.TakeVoiceMeeterRestart(50000), "VoiceMeeter cannot restart before Windows Audio");
    sequence.WindowsStarted();
    sequence.WindowsFinished(true, 50000);
    Check(!sequence.TakeVoiceMeeterRestart(50999), "VoiceMeeter waits a full second after Windows Audio returns");
    Check(sequence.TakeVoiceMeeterRestart(51000), "VoiceMeeter restart is issued after one second");
    Check(!sequence.TakeVoiceMeeterRestart(52000), "VoiceMeeter restart is issued once");
    Check(!sequence.ReadyToRead(51999) && sequence.ReadyToRead(52000), "MIDI remains held during engine recovery");
    sequence.Finish();
    Check(!sequence.Busy() && sequence.Begin(), "completed reset permits another explicit request");
    sequence.WindowsStarted(); sequence.WindowsFinished(false, 60000);
    Check(!sequence.Busy() && !sequence.TakeVoiceMeeterRestart(99999), "failed or cancelled Windows reset never restarts VoiceMeeter");

    std::vector<int> calls;
    auto stop = [&](size_t i) { calls.push_back(static_cast<int>(i + 1)); return true; };
    auto start = [&](size_t i) { calls.push_back(-static_cast<int>(i + 1)); return true; };
    Check(RestartPlan(3, stop, start) == Result::Success && calls == std::vector<int>{1, 2, 3, -3, -2, -1},
        "services stop children first and restore Windows Audio before its dependents");
    calls.clear();
    Check(RestartPlan(3, [&](size_t i) { stop(i); return i != 1; }, start) == Result::StopFailed &&
        calls == std::vector<int>{1, 2, -2, -1}, "partial stop failure restores every attempted service without stopping the root");
    calls.clear();
    Check(RestartPlan(3, stop, [&](size_t i) { start(i); return i != 2; }) == Result::RestoreFailed &&
        calls == std::vector<int>{1, 2, 3, -3, -2, -1}, "restoration failure does not skip remaining recovery attempts");
}

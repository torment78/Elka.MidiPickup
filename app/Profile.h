#pragma once
#include <Windows.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace elka {
struct Mapping { int channel{-1}, cc{-1}, target{}; bool use{}; };
struct Setup {
    std::wstring input;
    int top{2}, tolerance{3};
    bool launchTray{}, launchActive{}, launchWindows{}, confirmed{};
    std::array<Mapping, 16> mappings{};
    Setup() { for (int i = 0; i < 16; ++i) mappings[i].target = i; }
};
inline bool LoadProfile(const std::filesystem::path& path, Setup& result) {
    auto get = [&](const std::wstring& section, const wchar_t* key, int fallback) {
        return static_cast<int>(GetPrivateProfileIntW(section.c_str(), key, fallback, path.c_str()));
    };
    const int version = get(L"App", L"Version", 0);
    if (version != 1 && version != 2) return false;
    Setup next;
    wchar_t input[256]{};
    GetPrivateProfileStringW(L"App", L"Input", L"", input, 256, path.c_str()); next.input = input;
    next.top = std::clamp(get(L"App", L"Top", 2), 0, 2);
    next.tolerance = std::clamp(get(L"App", L"Tolerance", 3), 0, 3);
    next.launchTray = get(L"App", L"LaunchTray", 0) == 1;
    next.launchActive = get(L"App", L"LaunchActive", 0) == 1;
    next.launchWindows = get(L"App", L"LaunchWindows", 0) == 1;
    next.confirmed = version == 2 && get(L"App", L"Confirmed", 0) == 1;
    std::array<bool, 16> occupied{};
    for (int i = 0; i < (version == 1 ? 8 : 16); ++i) {
        Mapping m; const auto section = L"Fader" + std::to_wstring(i + 1);
        m.channel = get(section, L"Channel", -1); m.cc = get(section, L"CC", -1);
        if (m.channel < 0 || m.channel > 15 || m.cc < 0 || m.cc > 127) { m.channel = -1; m.cc = -1; }
        m.target = std::clamp(get(section, L"Target", i), 0, 15);
        m.use = get(section, L"Use", 0) == 1 && m.channel >= 0;
        int destination = i;
        if (version == 1) {
            // Preserve every old mapping, grouping by destination, not row number.
            destination = m.target < 8 ? 0 : 8;
            while (destination < 16 && occupied[destination]) ++destination;
        } else if ((m.target / 8) != (i / 8)) {
            // Invalid cross-page data is disabled, never silently retargeted live.
            m.target = i; m.use = false; next.confirmed = false;
        }
        if (destination < 16) { next.mappings[destination] = m; occupied[destination] = true; }
    }
    result = next; return true;
}
inline bool SaveProfile(const std::filesystem::path& path, const Setup& setup) {
    std::wostringstream text;
    text << L"[App]\nVersion=2\nInput=" << setup.input << L"\nTop=" << setup.top << L"\nTolerance=" << setup.tolerance
         << L"\nLaunchTray=" << setup.launchTray << L"\nLaunchActive=" << setup.launchActive
         << L"\nLaunchWindows=" << setup.launchWindows << L"\nConfirmed=" << setup.confirmed << L"\n";
    for (size_t i = 0; i < setup.mappings.size(); ++i) {
        const auto& m = setup.mappings[i];
        text << L"[Fader" << i + 1 << L"]\nChannel=" << m.channel << L"\nCC=" << m.cc << L"\nTarget=" << m.target << L"\nUse=" << m.use << L"\n";
    }
    const auto data = text.str(); const auto temporary = path.wstring() + L".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    const uint16_t bom = 0xfeff;
    file.write(reinterpret_cast<const char*>(&bom), sizeof(bom));
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size() * sizeof(wchar_t))); file.close();
    if (!file || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return false;
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str()); return true;
}
}

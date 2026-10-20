#pragma once
#include <Windows.h>
#include <ShlObj.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <stdexcept>

namespace elka::storage {
namespace fs = std::filesystem;
inline constexpr auto InstalledMarker = L"Elka.MidiPickup.installed";
inline constexpr auto AppFolder = L"Elka MIDI Pickup";
inline constexpr const wchar_t* ProfileFiles[] = {L"Elka.MidiPickup.ini", L"Elka.MidiPickup.applications"};
struct Paths {
    fs::path base, saves, logs, migrationMarker;
    bool installed{};
};
inline Paths Resolve(const fs::path& base, bool installed, const fs::path& roaming, const fs::path& local) {
    if (!installed) return {base, base, base, {}, false};
    const auto localRoot = local / L"ElkaSoft" / AppFolder;
    return {base, roaming / L"ElkaSoft" / AppFolder / L"Saves", localRoot / L"Logs",
        localRoot / L"storage-layout-v1.complete", true};
}
inline fs::path KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR value{};
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &value))) throw std::runtime_error("Windows data folder unavailable");
    const fs::path path(value); CoTaskMemFree(value); return path;
}
inline Paths Current() {
    wchar_t executable[32768]{};
    const auto length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768) throw std::runtime_error("Executable folder unavailable");
    const auto base = fs::path(executable).parent_path();
    const bool installed = fs::is_regular_file(base / InstalledMarker);
    return installed ? Resolve(base, true, KnownFolder(FOLDERID_RoamingAppData), KnownFolder(FOLDERID_LocalAppData))
        : Resolve(base, false, {}, {});
}
// Called by the regular user at startup, never by the elevated installer.
// Copy a legacy adjacent pair together, only if no Saves directory exists.
// Existing data and original files are never merged, overwritten, or deleted.
inline bool Prepare(const Paths& paths, std::wstring& error) {
    if (!paths.installed) return true;
    fs::path stage;
    bool stageCreated{};
    try {
        fs::create_directories(paths.logs);
        if (!fs::exists(paths.migrationMarker) && !fs::exists(paths.saves)) {
            fs::create_directories(paths.saves.parent_path());
            stage = paths.saves.parent_path() / (L"Saves.import-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
            if (!fs::create_directory(stage)) throw std::runtime_error("Cannot stage profile import");
            stageCreated = true;
            for (const auto* name : ProfileFiles) if (fs::exists(paths.base / name))
                fs::copy_file(paths.base / name, stage / name, fs::copy_options::none);
            fs::rename(stage, paths.saves);
            stage.clear();
        }
        fs::create_directories(paths.saves);
        if (!fs::is_directory(paths.saves)) throw std::runtime_error("Saves is not a directory");
        if (!fs::exists(paths.migrationMarker)) {
            std::ofstream marker(paths.migrationMarker, std::ios::binary);
            marker << "Elka MIDI Pickup storage layout 1\n"; marker.close();
            if (!marker) throw std::runtime_error("Cannot write storage marker");
        }
        return true;
    } catch (...) {
        // Only remove the exact temporary files created by this attempt.
        if (stageCreated && !stage.empty()) {
            std::error_code ignored;
            for (const auto* name : ProfileFiles) fs::remove(stage / name, ignored);
            fs::remove(stage, ignored);
        }
        error = L"Could not prepare MIDI Pickup's saved settings. No audio connections were opened.\n\nCheck access to:\n" +
            paths.saves.wstring() + L"\n" + paths.logs.wstring() + L"\n\nExisting settings were not replaced. Try launching again after fixing folder access.";
        return false;
    }
}
}

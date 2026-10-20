#pragma once
#include "../app/Storage.h"

void StorageTests() {
    using namespace elka::storage;
    const auto root = fs::temp_directory_path() / (L"ElkaStorageTest-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    Check(fs::create_directory(root), "create isolated storage fixture");
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
    const auto base = root / L"App", roaming = root / L"Roaming", local = root / L"Local";
    fs::create_directory(base);
    auto write = [](const fs::path& path, const std::string& value) { std::ofstream(path, std::ios::binary) << value; };
    auto read = [](const fs::path& path) { std::ifstream f(path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {}); };
    write(base / ProfileFiles[0], "strip settings"); write(base / ProfileFiles[1], "application settings");
    auto paths = Resolve(base, false, roaming, local); std::wstring error;
    Check(Prepare(paths, error) && paths.saves == base && paths.logs == base && !fs::exists(roaming) && !fs::exists(local), "portable mode never creates or migrates installed data");
    paths = Resolve(base, true, roaming, local);
    Check(paths.saves == roaming / L"ElkaSoft" / AppFolder / L"Saves" && paths.logs == local / L"ElkaSoft" / AppFolder / L"Logs", "installed paths follow ElkaSoft saves and logs convention");
    // A locked second source must not leave a partially imported pair.
    const HANDLE locked = CreateFileW((base / ProfileFiles[1]).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "lock test-only migration source");
    const bool failed = !Prepare(paths, error);
    CloseHandle(locked);
    Check(failed && !error.empty() && !fs::exists(paths.saves) && !fs::exists(paths.migrationMarker), "failed migration leaves no partial saves or completion marker");
    error.clear();
    Check(Prepare(paths, error) && error.empty() && fs::is_regular_file(paths.migrationMarker), "migration retries successfully after source unlock");
    for (const auto* name : ProfileFiles) Check(read(base / name) == read(paths.saves / name), "migration copies both exact files and leaves originals intact");
    write(paths.saves / ProfileFiles[0], "new strip setup");
    write(base / ProfileFiles[0], "obsolete strip setup");
    Check(Prepare(paths, error) && read(paths.saves / ProfileFiles[0]) == "new strip setup", "upgrade never replaces current saved settings");
    fs::remove(paths.saves / ProfileFiles[1]);
    Check(Prepare(paths, error) && !fs::exists(paths.saves / ProfileFiles[1]), "completed migration cannot resurrect deleted application assignments");
    auto existing = Resolve(base, true, root / L"ExistingRoaming", root / L"ExistingLocal");
    fs::create_directories(existing.saves); write(existing.saves / ProfileFiles[0], "existing settings");
    Check(Prepare(existing, error) && read(existing.saves / ProfileFiles[0]) == "existing settings" && !fs::exists(existing.saves / ProfileFiles[1]), "existing destination is never partially merged with legacy profiles");
    auto fresh = Resolve(root / L"FreshApp", true, root / L"FreshRoaming", root / L"FreshLocal");
    Check(Prepare(fresh, error) && fs::is_directory(fresh.saves) && fs::is_directory(fresh.logs) && fs::is_empty(fresh.saves), "fresh install creates writable empty settings and log folders");
    auto blocked = Resolve(base, true, root / L"BlockedRoaming", root / L"BlockedLocal");
    fs::create_directories(blocked.saves.parent_path()); write(blocked.saves, "not a directory");
    Check(!Prepare(blocked, error) && read(blocked.saves) == "not a directory", "invalid storage fails visibly without fallback or overwrite");
}

#pragma once
#include "ApplicationMidi.h"
#include <Windows.h>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace elka::apps {
struct Setup {
    std::array<std::wstring, RouteCount> endpoints;
    std::vector<Mapping> mappings;
    int tolerance{3};
};
inline bool SaveApplicationSetup(const std::filesystem::path& path, const Setup& setup) {
    std::wostringstream out;
    out << L"ElkaApplicationMidi 3\n" << setup.tolerance << L'\n';
    for (const auto& e : setup.endpoints) out << std::quoted(e) << L'\n';
    out << setup.mappings.size() << L'\n';
    for (const auto& m : setup.mappings) {
        out << m.route << L' ' << m.use << L' ' << m.source.kind << L' ' << m.source.channel << L' ' << m.source.number << L' ' << static_cast<int>(m.action)
            << L' ' << m.hidden << L' ' << std::quoted(m.endpoint) << L' ' << std::quoted(m.appKey) << L' ' << std::quoted(m.name) << L'\n';
    }
    const auto data = out.str(), temporary = path.wstring() + L".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    const uint16_t bom = 0xfeff; file.write(reinterpret_cast<const char*>(&bom), sizeof(bom));
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size() * sizeof(wchar_t))); file.close();
    return file && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
inline bool LoadApplicationSetup(const std::filesystem::path& path, Setup& setup) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return false;
    const auto bytes = file.tellg();
    if (bytes < 2 || bytes > 1024 * 1024 || bytes % 2 != 0) return false;
    std::wstring data(static_cast<size_t>(bytes) / sizeof(wchar_t), L'\0');
    file.seekg(0); file.read(reinterpret_cast<char*>(data.data()), bytes);
    if (!file || data[0] != 0xfeff) return false;
    std::wistringstream in(data.substr(1)); std::wstring magic; int version{};
    Setup next;
    if (!(in >> magic >> version >> next.tolerance) || magic != L"ElkaApplicationMidi" || version < 1 || version > 3 || next.tolerance < 0 || next.tolerance > 3) return false;
    for (int i = 0; i < (version == 1 ? 3 : RouteCount); ++i) if (!(in >> std::quoted(next.endpoints[i])) || next.endpoints[i].size() > 32768) return false;
    size_t count{}; if (!(in >> count) || count > 128) return false;
    for (size_t i = 0; i < count; ++i) {
        Mapping m; int action{}, use{}, hidden{};
        if (!(in >> m.route >> use >> m.source.kind >> m.source.channel >> m.source.number >> action)) return false;
        if (version == 3 && (!(in >> hidden) || hidden < 0 || hidden > 1)) return false;
        if (!(in >> std::quoted(m.endpoint) >> std::quoted(m.appKey) >> std::quoted(m.name))) return false;
        if (m.route < -1 || m.route >= (version == 1 ? 3 : RouteCount) || use < 0 || use > 1 || action < 0 || action > 3 || m.endpoint.empty() || m.appKey.empty() ||
            m.endpoint.size() > 32768 || m.appKey.size() > 32768 || m.name.size() > 1024) return false;
        if (!m.source.Valid() && m.source != Source{}) return false;
        if (m.source.kind == 1 && action != static_cast<int>(Action::Mute)) return false;
        m.id = i + 1; m.hidden = hidden != 0; m.use = use != 0 && !m.hidden; m.action = static_cast<Action>(action); next.mappings.push_back(std::move(m));
    }
    in >> std::ws; if (!in.eof()) return false;
    setup = std::move(next); return true;
}
}

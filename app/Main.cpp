#include "Pickup.h"
#include "Devices.h"
#include "Profile.h"
#include "resource.h"
#include "Version.h"
#include "ApplicationWindow.h"
#include "ControlLayout.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <CommCtrl.h>
#include <windowsx.h>
#include <shellapi.h>
#include <array>
#include <condition_variable>
#include <thread>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace {
constexpr int Device = 100, ConnectMidi = 101, Refresh = 102, ConnectVm = 103, Demo = 104,
    Range = 105, Tolerance = 106, Confirm = 107, Start = 108, Stop = 109, Rearm = 110,
    Save = 111, Load = 112, Help = 113, DemoHardware = 114, DemoSoftware = 115,
    Hide = 116, Exit = 117, StripTab = 130, BusTab = 131, SettingsTab = 132,
    LaunchWindows = 133, LaunchTray = 134, LaunchActive = 135, Donate = 136, Applications = 137,
    VmStatus = 120, Banner = 121, Counts = 122, Hint = 123, LastEvent = 124;
constexpr int RowBase = 200, RowStride = 10;
constexpr UINT TrayMessage = WM_APP + 30;
using Mapping = elka::Mapping;
constexpr COLORREF Back = RGB(17,20,23), Panel = RGB(26,32,37), Field = RGB(17,23,27),
    Ink = RGB(237,243,246), Muted = RGB(154,168,178), Accent = RGB(34,166,179), Orange = RGB(240,138,62);
struct Row { Mapping mapping; elka::Pickup pickup; uint64_t writes{}; };
struct Placement { HWND control; int x, y, width, height, font, page; };

std::wstring Text(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1); value.resize(length); return value;
}
void SetText(HWND control, const std::wstring& text) { if (Text(control) != text) SetWindowTextW(control, text.c_str()); }
void EnableIfNeeded(HWND control, bool enabled) { if ((IsWindowEnabled(control) != FALSE) != enabled) EnableWindow(control, enabled); }
std::wstring Db(double value) { std::wostringstream s; s << std::fixed << std::setprecision(1) << value << L" dB"; return s.str(); }
int Selection(HWND control) { return static_cast<int>(SendMessageW(control, CB_GETCURSEL, 0, 0)); }
bool Checked(HWND control) { return SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void Check(HWND control, bool check) { SendMessageW(control, BM_SETCHECK, check ? BST_CHECKED : BST_UNCHECKED, 0); }
HICON AppIcon(HINSTANCE instance, int size) {
    // Shared module resources live for the process lifetime, including tray
    // restoration after Explorer restarts. Windows chooses the nearest frame.
    return static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(IDI_MIDI_PICKUP), IMAGE_ICON, size, size, LR_SHARED));
}

class Application {
public:
    HWND window{};
private:
    HINSTANCE instance{};
    HBRUSH background = CreateSolidBrush(Back), fieldBrush = CreateSolidBrush(Field);
    int buildingPage{-1}, page{};
    elka::Setup savedSetup;
    bool startupPending{}, starting{}, shuttingDown{}, forceTray{}, forceWindow{}, configDirty{};
    uint64_t nextStartupAttempt{};
    std::wstring savedInput;
    std::array<HFONT, 3> fonts{};
    std::vector<Placement> placements;
    elka::ControlLayout controlLayout;
    std::vector<elka::MidiPort> ports;
    std::array<Row, 16> rows;
    std::array<Mapping, 16> beforeDemo{};
    std::array<double, 16> demoGains{};
    elka::MidiInput midi;
    elka::VoiceMeeter vm;
    elka::apps::Controller appControl;
    elka::apps::Window appWindow{appControl};
    elka::MidiInbox::Batch batch;
    std::optional<DWORD> learnedMessage;
    std::filesystem::path profile;
    std::filesystem::path diagnosticPath;
    // Win32 can re-enter the window procedure while updating controls.
    // The worker never calls window/control APIs and owns all periodic I/O.
    std::recursive_mutex stateMutex;
    std::condition_variable_any workerWake;
    std::mutex waitMutex;
    std::jthread worker;
    bool demo{}, active{};
    bool trayAdded{}, buttonWasRegistered{};
    UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    int learning{-1}, scroll{}, dpi{96};
    uint64_t lastFrame{}, errorsSeen{}, dropsSeen{}, received{}, readbackRecoveries{}, externalRearms{}, timingRecoveries{}, maxGap{};
    int savedTop{}, savedTolerance{3};
    std::wstring notice = L"Monitor mode. Connect your MIDI device and VoiceMeeter, then learn a fader.";
    HWND C(int id) const { return GetDlgItem(window, id); }
    HWND R(int row, int field) const { return C(RowBase + row * RowStride + field); }
    int S(int value) const { return MulDiv(value, dpi, 96); }

    static LRESULT CALLBACK DarkControl(HWND control, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR context) {
        auto& app = *reinterpret_cast<Application*>(context);
        if (message == WM_NCDESTROY) RemoveWindowSubclass(control, DarkControl, id);
        if (message == WM_MOUSEWHEEL && id == 2 && !SendMessageW(control, CB_GETDROPPEDSTATE, 0, 0))
            return SendMessageW(app.window, message, w, l);
        if (message == WM_PAINT && id == 1) {
            PAINTSTRUCT paint{}; const auto dc = BeginPaint(control, &paint); RECT rect{}; GetClientRect(control, &rect);
            app.PaintButton(control, dc, rect, (SendMessageW(control, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0, GetFocus() == control, true);
            EndPaint(control, &paint); return 0;
        }
        const auto result = DefSubclassProc(control, message, w, l);
        if (message == WM_PAINT && id == 2) {
            const auto dc = GetDC(control); RECT rect{}; GetClientRect(control, &rect);
            COMBOBOXINFO info{sizeof(info)}; GetComboBoxInfo(control, &info);
            FillRect(dc, &info.rcButton, app.fieldBrush);
            const auto pen = CreatePen(PS_SOLID, 1, Ink); const auto old = SelectObject(dc, pen);
            const int x = (info.rcButton.left + info.rcButton.right) / 2, y = (info.rcButton.top + info.rcButton.bottom) / 2;
            MoveToEx(dc, x - app.S(4), y - app.S(2), nullptr); LineTo(dc, x, y + app.S(2)); LineTo(dc, x + app.S(4), y - app.S(2));
            SelectObject(dc, old); DeleteObject(pen);
            const auto border = CreateSolidBrush(GetFocus() == control ? Accent : RGB(51,65,74));
            FrameRect(dc, &rect, border); InflateRect(&rect, -1, -1); FrameRect(dc, &rect, border); DeleteObject(border); ReleaseDC(control, dc);
        }
        return result;
    }

    HWND Add(const wchar_t* type, const std::wstring& title, DWORD style, int id, int x, int y, int w, int h, int font = 0) {
        HWND control = CreateWindowExW(0, type, title.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
            window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        placements.push_back({control, x, y, w, h, font, buildingPage});
        SetWindowTheme(control, L"", L"");
        if (wcscmp(type, L"BUTTON") == 0 && (style & BS_TYPEMASK) == BS_AUTOCHECKBOX)
            SetWindowSubclass(control, DarkControl, 1, reinterpret_cast<DWORD_PTR>(this));
        if (wcscmp(type, L"COMBOBOX") == 0) SetWindowSubclass(control, DarkControl, 2, reinterpret_cast<DWORD_PTR>(this));
        return control;
    }
    void Label(const std::wstring& text, int id, int x, int y, int w, int h = 24, int font = 0) { Add(L"STATIC", text, SS_LEFT, id, x, y, w, h, font); }
    void Button(const std::wstring& text, int id, int x, int y, int w) { Add(L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, id, x, y, w, 32); }
    HWND Combo(int id, int x, int y, int w) { return Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_TABSTOP | WS_VSCROLL, id, x, y, w, 240); }
    void Fonts() {
        for (auto font : fonts) if (font) DeleteObject(font);
        fonts[0] = CreateFontW(-S(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        fonts[1] = CreateFontW(-S(28), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        fonts[2] = CreateFontW(-S(15), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        for (const auto& p : placements) SendMessageW(p.control, WM_SETFONT, reinterpret_cast<WPARAM>(fonts[p.font]), TRUE);
    }
    void Layout() {
        RECT area{}; GetClientRect(window, &area);
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMin = 0; info.nMax = S(728); info.nPage = static_cast<UINT>(area.bottom); info.nPos = scroll;
        SetScrollInfo(window, SB_VERT, &info, TRUE);
        GetScrollInfo(window, SB_VERT, &info); scroll = info.nPos;
        const double horizontal = static_cast<double>(area.right) / 980.0;
        for (const auto& p : placements) controlLayout.Place(p.control, static_cast<int>(p.x * horizontal), S(p.y) - scroll,
            static_cast<int>(p.width * horizontal), S(p.height));
        controlLayout.Finish(window);
    }
    void CreateControls() {
        Label(L"Elka MIDI Pickup", 1, 24, 14, 600, 38, 1);
        Label(ELKA_VERSION_WSTRING L"  /  Preview", 2, 710, 28, 245);
        Label(L"MONITOR ONLY  /  No gain changes sent", Banner, 24, 60, 930, 26, 2);
        Label(L"MIDI device", 3, 24, 103, 110, 24, 2);
        Combo(Device, 138, 96, 480); Button(L"Connect MIDI", ConnectMidi, 634, 94, 152); Button(L"Refresh", Refresh, 800, 94, 154);
        Label(L"VoiceMeeter", 4, 24, 144, 110, 24, 2); Label(L"Not connected", VmStatus, 138, 144, 480);
        Button(L"Connect VM", ConnectVm, 634, 134, 152);
        Button(L"Strips", StripTab, 24, 182, 136); Button(L"Buses", BusTab, 166, 182, 136); Button(L"Settings", SettingsTab, 308, 182, 136);
        Button(L"Application MIDI Control", Applications, 458, 182, 238);
        Label(L"Strips + buses run together", 9, 708, 190, 246);
        for (int group = 0; group < 2; ++group) {
            buildingPage = group;
            for (const auto& item : {std::pair{24, L"Use"}, {90, L"MIDI source"}, {250, L"Target"}, {447, L"Hardware"}, {585, L"Software"}, {701, L"Pickup state"}})
                Label(item.second, 0, item.first, 232, 155, 22, 2);
            for (int slot = 0; slot < 8; ++slot) {
                const int i = group * 8 + slot, y = 266 + slot * 35, base = RowBase + i * RowStride;
                rows[i].mapping.target = i;
                rows[i].pickup.range.high = 12;
                Add(L"BUTTON", std::to_wstring(slot + 1), BS_AUTOCHECKBOX | WS_TABSTOP, base, 24, y, 60, 26);
                Label(L"Not learned", base + 1, 90, y + 3, 151);
                Combo(base + 2, 250, y, 180);
                for (int j = 0; j < 8; ++j) {
                    const auto target = std::wstring(group == 0 ? L"Strip " : L"Bus ") + std::to_wstring(j + 1) + L" / Gain";
                    SendMessageW(C(base + 2), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(target.c_str()));
                }
                SendMessageW(C(base + 2), CB_SETCURSEL, slot, 0);
                Label(L"--", base + 3, 447, y + 3, 132); Label(L"--", base + 4, 585, y + 3, 109);
                Label(L"Not learned", base + 5, 701, y + 3, 169); Button(L"Learn", base + 6, 876, y - 2, 78);
            }
        }
        buildingPage = 2;
        Label(L"Pickup behavior", 10, 24, 238, 300, 24, 2);
        Label(L"Top gain", 5, 24, 280, 90); Combo(Range, 120, 274, 140);
        for (auto text : {L"0 dB", L"+6 dB", L"+12 dB"}) SendMessageW(C(Range), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        SendMessageW(C(Range), CB_SETCURSEL, 2, 0);
        Label(L"Tolerance", 6, 300, 280, 100); Combo(Tolerance, 410, 274, 140);
        for (auto text : {L"0 steps", L"1 step", L"2 steps", L"3 steps"}) SendMessageW(C(Tolerance), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        SendMessageW(C(Tolerance), CB_SETCURSEL, 3, 0);
        Label(L"Startup from your last saved setup", 11, 24, 325, 700, 24, 2);
        Add(L"BUTTON", L"Launch with Windows", BS_AUTOCHECKBOX | WS_TABSTOP, LaunchWindows, 24, 360, 270, 28);
        Add(L"BUTTON", L"Launch in tray", BS_AUTOCHECKBOX | WS_TABSTOP, LaunchTray, 320, 360, 230, 28);
        Add(L"BUTTON", L"Launch active", BS_AUTOCHECKBOX | WS_TABSTOP, LaunchActive, 600, 360, 300, 28);
        Label(L"Save setup to apply. Active startup waits for your saved MIDI device and VoiceMeeter.", 12, 24, 397, 920);
        Label(L"Faders always wait for pickup. Stop cancels automatic startup for this session.", 13, 24, 424, 920);
        Add(L"BUTTON", L"Demo mode (no live audio)", BS_AUTOCHECKBOX | WS_TABSTOP, Demo, 24, 466, 450, 28);
        Label(L"Demo hardware (Strip 1)", 7, 24, 506, 220);
        Add(TRACKBAR_CLASSW, L"Demo hardware", WS_TABSTOP | TBS_AUTOTICKS, DemoHardware, 239, 498, 235, 38);
        Label(L"Demo software target", 8, 500, 506, 200);
        Add(TRACKBAR_CLASSW, L"Demo software", WS_TABSTOP | TBS_AUTOTICKS, DemoSoftware, 700, 498, 254, 38);
        for (auto id : {DemoHardware, DemoSoftware}) { SendMessageW(C(id), TBM_SETRANGE, TRUE, MAKELPARAM(0, 127)); EnableWindow(C(id), FALSE); }
        buildingPage = -1;
        Add(L"BUTTON", L"Selected faders' direct mappings are disabled in VoiceMeeter / Bome.", BS_AUTOCHECKBOX | WS_TABSTOP, Confirm, 24, 552, 930, 26);
        Button(L"Start pickup", Start, 24, 590, 136); Button(L"Stop", Stop, 170, 590, 72); Button(L"Re-arm all", Rearm, 252, 590, 105);
        Button(L"Save setup", Save, 367, 590, 116); Button(L"Load setup", Load, 493, 590, 116);
        Button(L"Hide", Hide, 619, 590, 70); Button(L"Exit", Exit, 699, 590, 70); Button(L"How to test", Help, 779, 590, 175);
        Label(L"", Counts, 24, 638, 930);
        Label(notice, Hint, 24, 673, 645, 46);
        Button(L"♥  Donate on Ko-fi", Donate, 699, 676, 255);
        Fonts();
#ifdef ELKA_UI_INSPECTION
        // Test executable only: exercise the real layout without devices,
        // profiles, registry changes, tray registration, or processing.
        SelectPage(0); Render(); Layout(); return;
#else
        RefreshPorts();
        wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768);
        profile = std::filesystem::path(path).parent_path() / L"Elka.MidiPickup.ini";
        diagnosticPath = std::filesystem::path(path).parent_path() / L"Elka.MidiPickup.log";
        std::error_code ec;
        if (std::filesystem::file_size(diagnosticPath, ec) > 1024 * 1024 && !ec)
            std::ofstream(diagnosticPath, std::ios::trunc).close();
        Log(L"Started build " ELKA_VERSION_WSTRING L"; MIDI/mixer worker nominal interval 10 ms.");
        if (std::filesystem::exists(profile)) LoadSetup();
        startupPending = savedSetup.launchActive;
        lastFrame = GetTickCount64();
        SetTimer(window, 1, 100, nullptr);
        InstallTray();
        UpdateIcons();
        worker = std::jthread([this](std::stop_token stop) {
            while (!stop.stop_requested()) {
                {
                    std::lock_guard lock(stateMutex);
                    if (stop.stop_requested()) break;
                    Tick();
                }
                std::unique_lock waitLock(waitMutex);
                workerWake.wait_for(waitLock, stop, std::chrono::milliseconds(10), [] { return false; });
            }
        });
        SelectPage(0); Render(); Layout();
#endif
    }
    void Log(const std::wstring& text) {
        if (diagnosticPath.empty()) return;
        SYSTEMTIME time{}; GetSystemTime(&time);
        // Normal input/write traffic does not log per event.
        const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<size_t>(length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), length, nullptr, nullptr);
        std::ofstream file(diagnosticPath, std::ios::app);
        file << time.wYear << '-' << time.wMonth << '-' << time.wDay << ' '
            << time.wHour << ':' << time.wMinute << ':' << time.wSecond << '.' << time.wMilliseconds
            << " UTC [" << GetTickCount64() << "] " << utf8 << '\n';
    }
    void Tell(const std::wstring& text) { notice = text; Log(text); }
    void Pause(const std::wstring& text) {
        active = false; midi.Clear(); learnedMessage.reset();
        for (auto& row : rows) row.pickup.Reset();
        if (!text.empty()) Tell(text);
    }
    void RefreshPorts() {
        if (midi.Open()) return;
        const auto previous = Text(C(Device));
        ports = elka::MidiInputs(); SendMessageW(C(Device), CB_RESETCONTENT, 0, 0);
        int chosen = 0;
        for (size_t i = 0; i < ports.size(); ++i) {
            SendMessageW(C(Device), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ports[i].name.c_str()));
            if (ports[i].name == previous) chosen = static_cast<int>(i);
        }
        if (!ports.empty()) SendMessageW(C(Device), CB_SETCURSEL, chosen, 0);
    }
    void DisconnectMidi() {
        appControl.Suspend();
        midi.Close(); learnedMessage.reset(); learning = -1; Check(C(Confirm), false);
        for (auto& row : rows) row.pickup.Disconnect();
        Pause(L"MIDI disconnected. Control is stopped.");
    }
    void ToggleMidi() {
        startupPending = false;
        if (midi.Open()) DisconnectMidi();
        else {
            const int selected = Selection(C(Device));
            if (selected < 0 || selected >= static_cast<int>(ports.size())) { Tell(L"Select a MIDI input first."); return; }
            // Device indexes can change after a hotplug. Resolve the selected
            // name against a fresh enumeration, rejecting ambiguous names.
            std::vector<elka::MidiPort> matches;
            for (const auto& port : elka::MidiInputs()) if (port.name == ports[selected].name) matches.push_back(port);
            if (matches.size() != 1) { Tell(L"Input missing or ambiguous. Refresh and select the correct device."); return; }
            Pause(L"");
            Check(C(Confirm), false);
            const auto result = midi.Connect(matches[0]);
            errorsSeen = 0; dropsSeen = 0; received = 0;
            if (result != 0) Tell(L"MIDI connection failed: " + elka::MidiError(result));
            else Tell(L"MIDI connected in monitor mode. Click Learn beside a fader, then move it.");
        }
        Render();
    }
    void ToggleVm() {
        appControl.Suspend();
        startupPending = false;
        Pause(L"");
        for (auto& row : rows) row.pickup.Disconnect();
        if (vm.Connected()) { vm.Close(); Tell(L"VoiceMeeter disconnected. Control is stopped."); }
        else if (!vm.Connect(window)) Tell(vm.error);
        else Tell(L"VoiceMeeter connected for reading. No gain changes are sent until Start pickup.");
        Render();
    }
    void SettingsChanged() {
        startupPending = false; configDirty = true;
        Pause(L"Settings changed. Pickup is stopped; start it again when ready."); learning = -1;
        const double high = std::max(0, Selection(C(Range))) * 6.0;
        for (int i = 0; i < 16; ++i) {
            auto& row = rows[i]; row.mapping.use = Checked(R(i, 0)); row.mapping.target = (i / 8) * 8 + Selection(R(i, 2));
            row.pickup.range = {-60, high}; row.pickup.tolerance = std::max(0, Selection(C(Tolerance)));
            row.pickup.software.reset();
        }
        Render();
    }
    void BeginLearn(int index) {
        appControl.Suspend();
        startupPending = false; configDirty = true;
        if (!demo && !midi.Open()) { Tell(L"Connect the MIDI input before learning a fader."); return; }
        Pause(L""); learning = learning == index ? -1 : index;
        Tell(learning < 0 ? L"Learn cancelled." : L"Move one absolute CC fader for row " + std::to_wstring(index + 1) + L". Notes and buttons are ignored.");
        Render();
    }
    bool Validate() {
        if (!demo && (!midi.Open() || !vm.Connected())) { Tell(L"Connect both the MIDI input and VoiceMeeter first."); return false; }
        if (!demo && !Checked(C(Confirm))) { Tell(L"Disable the selected faders' other direct mappings, then tick the confirmation above."); return false; }
        bool any = false;
        for (int i = 0; i < 16; ++i) if (rows[i].mapping.use) {
            const auto& m = rows[i].mapping;
            if (m.channel < 0 || m.cc < 0 || !rows[i].pickup.software || (!demo && !vm.ValidTarget(m.target))) {
                Tell(L"Fader " + std::to_wstring(i + 1) + L" needs a learned CC and an available VoiceMeeter target."); return false;
            }
            if (!rows[i].pickup.Reachable()) { Tell(L"Fader " + std::to_wstring(i + 1) + L": software gain is outside the chosen range. Change Top gain or the software fader."); return false; }
            for (int j = 0; j < i; ++j) if (rows[j].mapping.use) {
                const auto& other = rows[j].mapping;
                if ((m.channel == other.channel && m.cc == other.cc) || m.target == other.target) {
                    Tell(L"Each enabled fader needs a different MIDI source and a different VoiceMeeter target."); return false;
                }
            }
            any = true;
        }
        if (!any) Tell(L"Learn at least one fader and select its Use checkbox.");
        return any;
    }
    void StartPickup() {
        if (!starting) startupPending = false;
        Pause(L""); learning = -1; PollSoftware(GetTickCount64());
        if (!Validate()) { Render(); return; }
        active = true;
        lastFrame = GetTickCount64(); maxGap = 0;
        for (auto& row : rows) row.pickup.Reset();
        Tell(L"Move the hardware toward the software position. Each fader waits independently until pickup."); Render();
    }
    void ProcessMidi(DWORD message, bool finishLearn = false) {
        ++received;
        const int status = message & 0xff, channel = status & 0x0f, cc = (message >> 8) & 0x7f, value = (message >> 16) & 0x7f;
        if ((status & 0xf0) != 0xb0) return;
        if (learning >= 0) {
            if (!finishLearn) { if (!learnedMessage) learnedMessage = message; return; }
            if (appControl.Reserved({0, channel, cc})) {
                Tell(L"That CC is assigned in Application MIDI Control. Use a different control or disable its application row first."); return;
            }
            for (int i = 0; i < 16; ++i) if (i != learning && rows[i].mapping.channel == channel && rows[i].mapping.cc == cc) {
                Tell(L"That CC is already assigned to fader " + std::to_wstring(i + 1) + L". Move a different fader."); return;
            }
            auto& row = rows[learning]; row.mapping.channel = channel; row.mapping.cc = cc; row.mapping.use = true;
            Check(R(learning, 0), true); row.pickup.Reset(); row.pickup.hardware = value;
            Check(C(Confirm), false);
            Tell(L"Learned channel " + std::to_wstring(channel + 1) + L", CC " + std::to_wstring(cc) + L". Choose its target, then Start pickup.");
            learning = -1;
        }
        for (auto& row : rows) if (row.mapping.channel == channel && row.mapping.cc == cc)
            row.pickup.Input(value, active && row.mapping.use);
    }
    void PollSoftware(uint64_t now) {
        if (!demo && !vm.Connected()) return;
        if (!demo && !vm.Refresh()) {
            vm.Close(); Pause(L"VoiceMeeter connection lost. Reconnect and start pickup again.");
            for (auto& row : rows) row.pickup.Disconnect(); return;
        }
        for (auto& row : rows) {
            const auto value = demo ? std::optional<double>(demoGains[row.mapping.target]) : vm.Read(row.mapping.target);
            if (value) {
                const bool wasTracking = row.pickup.tracking;
                const auto pending = row.pickup.PendingValue();
                const auto result = row.pickup.Observe(*value, now);
                if (active && row.mapping.use && result == elka::Pickup::Observation::ReadbackTimeout) {
                    ++readbackRecoveries;
                    Tell(L"Fader " + std::to_wstring(&row - rows.data() + 1) + L": slow readback; move to its software level to pick up again. Other faders remain running.");
                    Log(L"Readback timeout: expected " + std::to_wstring(pending.value_or(0)) + L", observed " + std::to_wstring(*value));
                } else if (active && row.mapping.use && wasTracking && result == elka::Pickup::Observation::ExternalEdit) {
                    ++externalRearms;
                    Log(L"External re-arm, fader " + std::to_wstring(&row - rows.data() + 1) + L", observed " + std::to_wstring(*value) +
                        (pending ? L", pending " + std::to_wstring(*pending) : L", no pending write"));
                }
            }
            else {
                row.pickup.software.reset();
                if (active && row.mapping.use) Pause(L"A target could not be read. Output has stopped.");
            }
            if (active && row.pickup.fault && row.mapping.use) Pause(L"VoiceMeeter returned an invalid gain. Output has stopped.");
        }
    }
    void RecoverInput(const std::wstring& reason) {
        appControl.Suspend();
        midi.Clear(); learnedMessage.reset();
        for (auto& row : rows) row.pickup.Reset();
        ++timingRecoveries;
        if (active) Tell(reason + L" Old motion discarded; pickup remains running. Move to the software level again.");
    }
    void Tick() {
        const auto now = GetTickCount64();
        const auto gap = now - lastFrame;
        if (active) maxGap = std::max(maxGap, gap);
        if (active && gap > 300) RecoverInput(L"Processing resumed after a delay.");
        lastFrame = now;
        if (midi.Open() && midi.Errors() != errorsSeen) { errorsSeen = midi.Errors(); appControl.Suspend(); Pause(L"MIDI input reported malformed data. Output stopped; reconnect before testing again."); }
        if (midi.Open() && midi.Dropped() != dropsSeen) { dropsSeen = midi.Dropped(); RecoverInput(L"MIDI buffer filled during a delay."); }
        PollSoftware(now);
        if (appControl.Launched()) {
            std::vector<elka::apps::Source> reserved;
            for (const auto& row : rows) if (row.mapping.use && row.mapping.channel >= 0)
                reserved.push_back({0, row.mapping.channel, row.mapping.cc});
            appControl.Bridge(!demo && midi.Open() && vm.Connected() && learning < 0 && !shuttingDown, vm.edition, reserved);
        }
        midi.Drain(batch);
        bool stale = false;
        for (size_t i = 0; i < batch.size; ++i) {
            const auto message = batch.messages[i];
            if (static_cast<DWORD>(GetTickCount() - message.at) > 250) {
                if (!stale) { for (auto& row : rows) row.pickup.Reset(); appControl.Suspend(); ++timingRecoveries; }
                stale = true; continue;
            }
            if (!demo && learning < 0) appControl.Midi(message.data, message.at);
            ProcessMidi(message.data);
        }
        if (active) for (auto& row : rows) if (row.mapping.use) {
            const auto value = row.pickup.NextWrite();
            if (!value) continue;
            if (demo) demoGains[row.mapping.target] = *value;
            else if (!vm.Write(row.mapping.target, *value)) { row.pickup.WriteFailed(); Pause(L"VoiceMeeter rejected a gain change. Output has stopped."); break; }
            row.pickup.Written(*value, now); ++row.writes;
        }
    }
    void UiTick() {
        TryStartup();
        if (vm.ButtonRegistered() && !buttonWasRegistered) Log(L"Registered MIDI Pickup on VoiceMeeter custom display button 2.");
        buttonWasRegistered = vm.ButtonRegistered();
        if (learnedMessage) {
            const auto message = *learnedMessage; learnedMessage.reset();
            if (learning >= 0) ProcessMidi(message, true);
        }
        if (demo) {
            const auto position = static_cast<LPARAM>(std::lround(rows[0].pickup.range.Position(demoGains[rows[0].mapping.target])));
            if (SendMessageW(C(DemoSoftware), TBM_GETPOS, 0, 0) != position) SendMessageW(C(DemoSoftware), TBM_SETPOS, TRUE, position);
        }
        if (IsWindowVisible(window) && !IsIconic(window)) Render();
    }
    void ToggleDemo() {
        startupPending = false;
        const bool requested = Checked(C(Demo));
        if (requested == demo) return;
        Pause(L""); DisconnectMidi(); vm.Close(); Check(C(Confirm), false);
        if (requested) {
            savedTop = Selection(C(Range)); savedTolerance = Selection(C(Tolerance));
            SendMessageW(C(Range), CB_SETCURSEL, 0, 0); SendMessageW(C(Tolerance), CB_SETCURSEL, 1, 0);
            for (int i = 0; i < 16; ++i) {
                beforeDemo[i] = rows[i].mapping; rows[i].mapping = {-1, -1, i, false}; rows[i].pickup.Disconnect(); rows[i].writes = 0;
                Check(R(i, 0), false); SendMessageW(R(i, 2), CB_SETCURSEL, i % 8, 0);
            }
            rows[0].mapping = {0, 7, 0, true}; Check(R(0, 0), true);
            demoGains.fill(-18.0); SendMessageW(C(DemoHardware), TBM_SETPOS, TRUE, 25);
            rows[0].pickup.hardware = 25;
        } else {
            SendMessageW(C(Range), CB_SETCURSEL, savedTop, 0); SendMessageW(C(Tolerance), CB_SETCURSEL, savedTolerance, 0);
            for (int i = 0; i < 16; ++i) {
                rows[i].mapping = beforeDemo[i]; rows[i].pickup.Disconnect(); rows[i].writes = 0;
                Check(R(i, 0), rows[i].mapping.use); SendMessageW(R(i, 2), CB_SETCURSEL, rows[i].mapping.target % 8, 0);
            }
        }
        demo = requested; SettingsChanged();
        Tell(demo ? L"Demo: click Start pickup, then move the hardware slider toward -18 dB. The right slider simulates a mouse edit." : L"Live mode. Connect MIDI and VoiceMeeter; output is stopped.");
        Render();
    }
    bool ApplyWindowsStartup(bool enabled) {
        HKEY key{};
        auto result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (result == ERROR_SUCCESS) {
            if (enabled) {
                wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768);
                const std::wstring command = L"\"" + std::wstring(path) + L"\"";
                result = RegSetValueExW(key, L"ElkaMidiPickup", 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()), static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
            } else { result = RegDeleteValueW(key, L"ElkaMidiPickup"); if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS; }
            RegCloseKey(key);
        }
        if (result != ERROR_SUCCESS) Tell(L"Could not update Windows startup (code " + std::to_wstring(result) + L"). Setup is saved; try Save setup again.");
        return result == ERROR_SUCCESS;
    }
    void SaveSetup() {
        elka::Setup setup;
        setup.input = midi.Open() ? midi.name : Text(C(Device));
        if (setup.input.empty()) setup.input = savedInput;
        setup.top = Selection(C(Range)); setup.tolerance = Selection(C(Tolerance));
        setup.confirmed = Checked(C(Confirm)); setup.launchTray = Checked(C(LaunchTray));
        setup.launchActive = Checked(C(LaunchActive)); setup.launchWindows = Checked(C(LaunchWindows));
        for (size_t i = 0; i < rows.size(); ++i) setup.mappings[i] = rows[i].mapping;
        if (!elka::SaveProfile(profile, setup)) { Tell(L"Could not save setup beside the app. Check folder permissions."); return; }
        savedSetup = setup; savedInput = setup.input; configDirty = false;
        if (ApplyWindowsStartup(setup.launchWindows)) Tell(L"Setup saved. Startup uses these mappings and options. Unsaved changes are not loaded next time.");
    }
    void LoadSetup() {
        startupPending = false; Pause(L""); learning = -1;
        elka::Setup setup;
        if (!elka::LoadProfile(profile, setup)) { Tell(L"No supported saved setup found beside the app."); return; }
        if (midi.Open()) DisconnectMidi();
        SendMessageW(C(Range), CB_SETCURSEL, setup.top, 0);
        SendMessageW(C(Tolerance), CB_SETCURSEL, setup.tolerance, 0);
        for (int i = 0; i < 16; ++i) {
            rows[i].mapping = setup.mappings[i]; rows[i].pickup.Disconnect();
            Check(R(i, 0), rows[i].mapping.use); SendMessageW(R(i, 2), CB_SETCURSEL, rows[i].mapping.target % 8, 0);
        }
        RefreshPorts(); SendMessageW(C(Device), CB_SETCURSEL, static_cast<WPARAM>(-1), 0);
        for (size_t i = 0; i < ports.size(); ++i) if (ports[i].name == setup.input) SendMessageW(C(Device), CB_SETCURSEL, i, 0);
        SettingsChanged(); savedSetup = setup; savedInput = setup.input; configDirty = false;
        Check(C(Confirm), setup.confirmed); Check(C(LaunchTray), setup.launchTray);
        Check(C(LaunchActive), setup.launchActive); Check(C(LaunchWindows), setup.launchWindows);
        Tell(L"Last saved setup loaded. Both Strip and Bus pages are ready.");
    }
    void TryStartup() {
        if (!startupPending || demo || shuttingDown || GetTickCount64() < nextStartupAttempt) return;
        nextStartupAttempt = GetTickCount64() + 2000;
        if (savedInput.empty()) { startupPending = false; Tell(L"Save a MIDI device before using active startup."); return; }
        if (!midi.Open()) {
            std::vector<elka::MidiPort> matches;
            for (const auto& port : elka::MidiInputs()) if (port.name == savedInput) matches.push_back(port);
            if (matches.size() != 1) { if (notice != L"Startup: waiting for the saved MIDI device.") Tell(L"Startup: waiting for the saved MIDI device."); return; }
            if (midi.Connect(matches[0]) != 0) { if (notice != L"Startup: waiting for the MIDI device to become available.") Tell(L"Startup: waiting for the MIDI device to become available."); return; }
            errorsSeen = dropsSeen = received = 0;
            RefreshDeviceSelection(matches[0]);
        }
        if (!vm.Connected() && !vm.Connect(window)) { if (notice != L"Startup: waiting for VoiceMeeter.") Tell(L"Startup: waiting for VoiceMeeter."); return; }
        if (!Checked(C(Confirm))) { startupPending = false; Tell(L"Startup connected. Confirm direct mappings are disabled, then save setup to enable automatic pickup."); return; }
        startupPending = false; starting = true; StartPickup(); starting = false;
    }
    void RefreshDeviceSelection(const elka::MidiPort& port) {
        ports = elka::MidiInputs(); SendMessageW(C(Device), CB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < ports.size(); ++i) {
            SendMessageW(C(Device), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ports[i].name.c_str()));
            if (ports[i].id == port.id) SendMessageW(C(Device), CB_SETCURSEL, i, 0);
        }
    }
    void SelectPage(int selected) {
        page = selected;
        for (const auto& p : placements) ShowWindow(p.control, p.page < 0 || p.page == page ? SW_SHOW : SW_HIDE);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    void PaintButton(HWND control, HDC dc, RECT rect, bool pressed, bool focused, bool checkbox) {
        const int id = GetDlgCtrlID(control);
        const bool enabled = IsWindowEnabled(control) != FALSE;
        const bool selected = (id >= StripTab && id <= SettingsTab && id - StripTab == page);
        const auto fill = checkbox ? Back : id == Donate ? Orange : selected || id == Start ? Accent : Panel;
        const auto brush = CreateSolidBrush(pressed ? RGB(51,65,74) : fill);
        if (checkbox) FillRect(dc, &rect, brush);
        else {
            FillRect(dc, &rect, background);
            const auto pen = CreatePen(PS_SOLID, 1, selected ? Accent : RGB(51,65,74));
            const auto oldBrush = SelectObject(dc, brush), oldPen = SelectObject(dc, pen);
            RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, S(10), S(10));
            SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(pen);
        }
        DeleteObject(brush);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, !enabled ? Muted : id == Donate || selected || id == Start ? Back : Ink);
        SelectObject(dc, fonts[0]);
        if (checkbox) {
            RECT box{rect.left + S(2), rect.top + (rect.bottom - rect.top - S(16)) / 2, 0, 0};
            box.right = box.left + S(16); box.bottom = box.top + S(16);
            const auto mark = CreateSolidBrush(Checked(control) ? Accent : RGB(51,65,74));
            if (Checked(control)) FillRect(dc, &box, mark); else FrameRect(dc, &box, mark);
            DeleteObject(mark);
            if (Checked(control)) { SetTextColor(dc, Back); DrawTextW(dc, L"\u2713", -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
            SetTextColor(dc, enabled ? Ink : Muted); rect.left += S(24);
        }
        auto text = Text(control);
        DrawTextW(dc, text.c_str(), -1, &rect, (checkbox ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (focused) { InflateRect(&rect, -S(3), -S(3)); DrawFocusRect(dc, &rect); }
    }
    void ExitMain() {
        if (shuttingDown) return;
        shuttingDown = true; startupPending = false; Pause(L"");
        appControl.Suspend();
        worker.request_stop(); workerWake.notify_all();
        midi.Close(); vm.Close();
        Log(L"Button 2 removal result: " + std::to_wstring(vm.lastButtonRemoval));
        // Remove the remote button while its registered HWND is still alive.
        RemoveTray(); DestroyWindow(window);
    }
    void HelpMessage() {
        MessageBoxW(window,
            L"1. Choose your MIDI device and click Connect MIDI. Move a fader to see the received count.\n\n"
            L"2. Connect VM. Open Strips or Buses, click Learn, and move an absolute fader. Both pages work together.\n\n"
            L"3. Disable that fader's original direct mapping in VoiceMeeter/Bome. Other controls keep using their existing routes. Tick the confirmation.\n\n"
            L"4. Click Start pickup. Move toward the displayed software gain. The row changes from Move up/down to Tracking.\n\n"
            L"5. Move the VoiceMeeter fader with your mouse. Elka waits for pickup again.\n\n"
            L"VoiceMeeter display button 2 (MIDI Pickup) shows or hides this window while pickup keeps running. The tray icon reopens it; Exit stops and closes the app.\n\n"
            L"Settings contains Launch with Windows, Launch in tray and Launch active. Save setup applies these options. Startup waits for your saved device and VoiceMeeter, then arms pickup from their current values.\n\n"
            L"Demo mode disconnects live devices and provides two sliders for a no-audio test.\n\n"
            L"This build supports 7-bit absolute CC faders using a linear -60 dB to Top gain curve. It is not a virtual MIDI pass-through. No messages are sent back to your hardware.",
            L"How to test Elka MIDI Pickup", MB_OK | MB_ICONINFORMATION);
    }
    void Render() {
        SetText(C(Hint), notice);
        SetText(C(Save), configDirty ? L"Save setup *" : L"Save setup");
        SetText(C(ConnectMidi), midi.Open() ? L"Disconnect" : L"Connect MIDI");
        SetText(C(ConnectVm), vm.Connected() ? L"Disconnect VM" : L"Connect VM");
        SetText(C(VmStatus), demo ? L"Simulated mixer  /  no audio writes" : vm.Connected() ?
            std::wstring(vm.edition == 3 ? L"Potato" : vm.edition == 2 ? L"Banana" : L"Standard") +
            (vm.ButtonRegistered() ? L" connected / Button 2 ready" : vm.ButtonSupported() ? L" connected / Adding button 2" : L" connected / No display button") : L"Not connected");
        SetText(C(Banner), active ? (demo ? L"DEMO PICKUP RUNNING  /  Live audio disconnected" : L"PICKUP RUNNING  /  Selected faders control VoiceMeeter") : L"STRIP / BUS MONITOR  /  No strip or bus gain changes sent");
        EnableIfNeeded(C(Device), !demo && !midi.Open()); EnableIfNeeded(C(Refresh), !demo && !midi.Open());
        EnableIfNeeded(C(ConnectMidi), !demo); EnableIfNeeded(C(ConnectVm), !demo); EnableIfNeeded(C(Confirm), !demo);
        EnableIfNeeded(C(DemoHardware), demo); EnableIfNeeded(C(DemoSoftware), demo); EnableIfNeeded(C(Save), !demo); EnableIfNeeded(C(Load), !demo);
        EnableIfNeeded(C(Start), !active); EnableIfNeeded(C(Stop), active || startupPending || learning >= 0);
        EnableIfNeeded(C(Hide), trayAdded || vm.ButtonRegistered());
        uint64_t writes{};
        for (int i = 0; i < 16; ++i) {
            auto& row = rows[i]; const auto& m = row.mapping; const auto& p = row.pickup; writes += row.writes;
            SetText(R(i, 1), m.channel < 0 ? L"Not learned" : L"Ch " + std::to_wstring(m.channel + 1) + L"  /  CC " + std::to_wstring(m.cc));
            SetText(R(i, 3), p.hardware ? std::to_wstring(*p.hardware) + L" / " + Db(p.range.Gain(*p.hardware)) : L"--");
            SetText(R(i, 4), p.software ? Db(*p.software) : L"--");
            std::wstring state;
            if (learning == i) state = L"Move fader...";
            else if (m.channel < 0) state = L"Not learned";
            else if (!m.use) state = L"Disabled";
            else if (!p.software) state = L"Target unavailable";
            else if (!p.Reachable()) state = L"Outside range";
            else if (!active) state = L"Monitor";
            else if (p.tracking) state = p.Pending() ? L"Tracking / syncing" : L"Tracking";
            else if (!p.hardware) state = L"Move fader";
            else state = p.range.Gain(*p.hardware) < *p.software ? L"Move up to pick up" : L"Move down to pick up";
            SetText(R(i, 5), state); SetText(R(i, 6), learning == i ? L"Cancel" : L"Learn");
        }
        SetText(C(Counts), (demo ? L"Demo events: " : L"MIDI messages: ") + std::to_wstring(demo ? received : midi.Count()) +
            L"  |  Writes: " + std::to_wstring(writes) + L"  |  Bursts: " + std::to_wstring(midi.Bursts()) +
            L"  |  Readback re-arms: " + std::to_wstring(readbackRecoveries) + L"  |  External: " + std::to_wstring(externalRearms) +
            L"  |  Input resets: " + std::to_wstring(timingRecoveries) + L"  |  Max gap: " + std::to_wstring(maxGap) + L" ms");
    }
    void Command(int id, int code) {
        if (id >= RowBase && id < RowBase + 160) {
            const int field = (id - RowBase) % RowStride, index = (id - RowBase) / RowStride;
            if (field == 6 && code == BN_CLICKED) BeginLearn(index);
            else if ((field == 0 && code == BN_CLICKED) || (field == 2 && code == CBN_SELCHANGE)) { Check(C(Confirm), false); SettingsChanged(); }
            return;
        }
        if (code == CBN_SELCHANGE && id == Tolerance) {
            configDirty = true;
            for (auto& row : rows) row.pickup.tolerance = std::max(0, Selection(C(Tolerance)));
            Tell(L"Pickup tolerance updated. Faders already tracking keep control."); Render(); return;
        }
        if (code == CBN_SELCHANGE && id == Range) { SettingsChanged(); return; }
        if (code == CBN_SELCHANGE && id == Device) { startupPending = false; configDirty = true; return; }
        if (code != BN_CLICKED) return;
        switch (id) {
        case ConnectMidi: ToggleMidi(); break;
        case ConnectVm: ToggleVm(); break;
        case Refresh: RefreshPorts(); Tell(L"MIDI input list refreshed."); break;
        case Demo: ToggleDemo(); break;
        case Confirm: configDirty = true; if (!Checked(C(Confirm))) { startupPending = false; Pause(L"Direct mapping confirmation cleared. Control stopped."); } break;
        case Start: StartPickup(); break;
        case Stop: startupPending = false; learning = -1; Pause(L"Control stopped. Live values remain visible."); break;
        case Rearm: for (auto& row : rows) row.pickup.Reset(); midi.Clear(); Tell(L"All faders re-armed. Move each one to its software position."); break;
        case Save: SaveSetup(); break;
        case Load: LoadSetup(); break;
        case Help: HelpMessage(); break;
        case Hide: HideMain(); break;
        case Exit: ExitMain(); return;
        case StripTab: SelectPage(0); break;
        case BusTab: SelectPage(1); break;
        case SettingsTab: SelectPage(2); break;
        case LaunchWindows: case LaunchTray: case LaunchActive: configDirty = true; Tell(L"Click Save setup to apply these startup options."); break;
        }
        Render();
    }
    void InstallTray() {
        NOTIFYICONDATAW icon{sizeof(icon)};
        icon.hWnd = window; icon.uID = 1; icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        icon.uCallbackMessage = TrayMessage; icon.hIcon = AppIcon(instance, GetSystemMetricsForDpi(SM_CXSMICON, static_cast<UINT>(dpi)));
        wcscpy_s(icon.szTip, L"Elka MIDI Pickup - double-click to open");
        trayAdded = Shell_NotifyIconW(NIM_ADD, &icon) != FALSE;
    }
    void UpdateIcons() {
        const auto iconSmall = AppIcon(instance, GetSystemMetricsForDpi(SM_CXSMICON, static_cast<UINT>(dpi)));
        const auto iconLarge = AppIcon(instance, GetSystemMetricsForDpi(SM_CXICON, static_cast<UINT>(dpi)));
        SendMessageW(window, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall));
        SendMessageW(window, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconLarge));
        if (trayAdded) {
            NOTIFYICONDATAW icon{sizeof(icon)}; icon.hWnd = window; icon.uID = 1; icon.uFlags = NIF_ICON; icon.hIcon = iconSmall;
            Shell_NotifyIconW(NIM_MODIFY, &icon);
        }
    }
    void RemoveTray() {
        if (!trayAdded) return;
        NOTIFYICONDATAW icon{sizeof(icon)}; icon.hWnd = window; icon.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &icon); trayAdded = false;
    }
    void ShowMain() {
        { std::lock_guard lock(stateMutex); Render(); }
        ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
        SetForegroundWindow(window);
    }
    void HideMain() {
        if (trayAdded || vm.ButtonRegistered()) ShowWindow(window, SW_HIDE);
        else ShowWindow(window, SW_MINIMIZE);
    }
    void TrayMenu() {
        const auto menu = CreatePopupMenu();
        if (!menu) return;
        AppendMenuW(menu, MF_STRING, 1, L"Open Elka MIDI Pickup");
        AppendMenuW(menu, MF_STRING, 2, L"Hide window");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, 3, L"Exit");
        POINT point{}; GetCursorPos(&point); SetForegroundWindow(window);
        const auto choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (choice == 1) ShowMain();
        if (choice == 2) { std::lock_guard lock(stateMutex); HideMain(); }
        if (choice == 3) { std::lock_guard lock(stateMutex); ExitMain(); }
        else PostMessageW(window, WM_NULL, 0, 0);
    }
public:
    bool Dialog(MSG& message) { return appWindow.Dialog(message); }
    explicit Application(HINSTANCE appInstance, const std::wstring& arguments) : instance(appInstance) {
        forceTray = arguments.find(L"--tray") != std::wstring::npos; forceWindow = arguments.find(L"--show") != std::wstring::npos;
    }
    bool StartHidden() const { return !forceWindow && trayAdded && (forceTray || savedSetup.launchTray); }
    ~Application() { worker.request_stop(); workerWake.notify_all(); if (worker.joinable()) worker.join(); appControl.Shutdown(); midi.Close(); vm.Close(); for (auto f : fonts) if (f) DeleteObject(f); DeleteObject(background); DeleteObject(fieldBrush); }
    LRESULT Handle(UINT message, WPARAM w, LPARAM l) {
#ifdef ELKA_UI_INSPECTION
        if (message == WM_COMMAND && LOWORD(w) != StripTab && LOWORD(w) != BusTab && LOWORD(w) != SettingsTab && LOWORD(w) != Exit) return 0;
#endif
        // Creating the preview window must not hold up strip/bus processing.
        if (message == WM_COMMAND && LOWORD(w) == Applications && HIWORD(w) == BN_CLICKED) {
            appControl.Launch(profile.parent_path() / L"Elka.MidiPickup.applications");
            appWindow.Show(window, instance); return 0;
        }
        // A modal help dialog must not hold the engine lock for its lifetime.
        if (message == WM_COMMAND && LOWORD(w) == Help && HIWORD(w) == BN_CLICKED) { HelpMessage(); return 0; }
        // Launching the browser may be slow; keep MIDI processing independent.
        if (message == WM_COMMAND && LOWORD(w) == Donate && HIWORD(w) == BN_CLICKED) {
            const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(window, L"open", L"https://ko-fi.com/msffixit", nullptr, nullptr, SW_SHOWNORMAL));
            if (result <= 32) { std::lock_guard lock(stateMutex); Tell(L"Could not open the donation page: https://ko-fi.com/msffixit"); }
            return 0;
        }
        if (message == TrayMessage && l == WM_RBUTTONUP) { TrayMenu(); return 0; }
        // Enumerating drivers can take time; never hold up the processing worker.
        std::optional<std::vector<elka::MidiPort>> changedPorts;
        if (message == WM_DEVICECHANGE) changedPorts = elka::MidiInputs();
        std::unique_lock lock(stateMutex);
        if (message == taskbarCreated) { trayAdded = false; InstallTray(); return 0; }
        switch (message) {
        case WM_CREATE: dpi = static_cast<int>(GetDpiForWindow(window)); CreateControls(); return 0;
        case WM_COMMAND:
            if (LOWORD(w) == elka::VoiceMeeter::ButtonCommand) {
                // VoiceMeeter sends a press and a release. Toggle once on the
                // press, after its own click handling has released foreground.
                if (l != 0 && vm.ButtonRegistered()) SetTimer(window, 2, 100, nullptr);
            } else Command(LOWORD(w), HIWORD(w));
            return 0;
        case WM_TIMER:
            if (w == 2) { KillTimer(window, 2); if (IsWindowVisible(window) && !IsIconic(window)) HideMain(); else ShowMain(); }
            else UiTick();
            return 0;
        case TrayMessage: if (l == WM_LBUTTONDBLCLK) ShowMain(); return 0;
        case WM_DEVICECHANGE:
            if (midi.Open() && changedPorts && std::none_of(changedPorts->begin(), changedPorts->end(), [this](const auto& port) { return port.name == midi.name; }))
                { DisconnectMidi(); Tell(L"MIDI input was unplugged. Refresh and reconnect it."); }
            return 0;
        case WM_HSCROLL:
            if (demo) {
                const int id = GetDlgCtrlID(reinterpret_cast<HWND>(l));
                if (id == DemoHardware) ProcessMidi(0xb0 | (7 << 8) | (static_cast<DWORD>(SendMessageW(C(DemoHardware), TBM_GETPOS, 0, 0)) << 16));
                if (id == DemoSoftware) demoGains[rows[0].mapping.target] = rows[0].pickup.range.Gain(static_cast<int>(SendMessageW(C(DemoSoftware), TBM_GETPOS, 0, 0)));
                PollSoftware(GetTickCount64()); Render();
            }
            return 0;
        case WM_SIZE: Layout(); return 0;
        case WM_VSCROLL: {
            SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(window, SB_VERT, &info);
            if (LOWORD(w) == SB_LINEUP) scroll -= S(24);
            if (LOWORD(w) == SB_LINEDOWN) scroll += S(24);
            if (LOWORD(w) == SB_PAGEUP) scroll -= static_cast<int>(info.nPage);
            if (LOWORD(w) == SB_PAGEDOWN) scroll += static_cast<int>(info.nPage);
            if (LOWORD(w) == SB_THUMBTRACK) scroll = info.nTrackPos;
            Layout(); return 0;
        }
        case WM_MOUSEWHEEL: scroll -= GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * S(60); Layout(); return 0;
        case WM_DPICHANGED: {
            dpi = HIWORD(w); Fonts(); UpdateIcons(); const auto rect = reinterpret_cast<RECT*>(l);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE); Layout(); return 0;
        }
        case WM_GETMINMAXINFO: {
            auto info = reinterpret_cast<MINMAXINFO*>(l); info->ptMinTrackSize = {S(900), S(520)}; return 0;
        }
        case WM_DRAWITEM: {
            const auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(l);
            if (draw->CtlType == ODT_BUTTON) {
                PaintButton(draw->hwndItem, draw->hDC, draw->rcItem, (draw->itemState & ODS_SELECTED) != 0, (draw->itemState & ODS_FOCUS) != 0, false);
                return TRUE;
            }
            if (draw->CtlType == ODT_COMBOBOX) {
                const bool selected = (draw->itemState & ODS_SELECTED) != 0;
                const auto brush = CreateSolidBrush(selected ? Accent : Field);
                FillRect(draw->hDC, &draw->rcItem, brush); DeleteObject(brush);
                SetBkMode(draw->hDC, TRANSPARENT); SetTextColor(draw->hDC, selected ? Back : Ink);
                SelectObject(draw->hDC, fonts[0]);
                if (draw->itemID != static_cast<UINT>(-1)) {
                    const auto length = SendMessageW(draw->hwndItem, CB_GETLBTEXTLEN, draw->itemID, 0);
                    if (length >= 0) {
                        std::wstring item(static_cast<size_t>(length) + 1, L'\0');
                        SendMessageW(draw->hwndItem, CB_GETLBTEXT, draw->itemID, reinterpret_cast<LPARAM>(item.data()));
                        RECT rect = draw->rcItem; rect.left += S(7);
                        DrawTextW(draw->hDC, item.c_str(), -1, &rect, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                    }
                }
                return TRUE;
            }
            break;
        }
        case WM_MEASUREITEM: reinterpret_cast<MEASUREITEMSTRUCT*>(l)->itemHeight = S(25); return TRUE;
        case WM_NOTIFY: {
            const auto header = reinterpret_cast<NMHDR*>(l);
            if (header->code == NM_CUSTOMDRAW && (GetWindowLongPtrW(header->hwndFrom, GWL_STYLE) & BS_TYPEMASK) == BS_AUTOCHECKBOX) {
                const auto draw = reinterpret_cast<NMCUSTOMDRAW*>(l);
                if (draw->dwDrawStage == CDDS_PREPAINT) {
                    PaintButton(header->hwndFrom, draw->hdc, draw->rc, (draw->uItemState & CDIS_SELECTED) != 0, (draw->uItemState & CDIS_FOCUS) != 0, true);
                    return CDRF_SKIPDEFAULT;
                }
            }
            break;
        }
        case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
            auto dc = reinterpret_cast<HDC>(w); SetBkColor(dc, Field); SetTextColor(dc, Ink); return reinterpret_cast<LRESULT>(fieldBrush);
        }
        case WM_CTLCOLORBTN: case WM_CTLCOLORSTATIC: {
            auto dc = reinterpret_cast<HDC>(w); SetBkColor(dc, Back); SetTextColor(dc, Ink);
            const int id = GetDlgCtrlID(reinterpret_cast<HWND>(l));
            if (id == 2 || id == Counts || id == Hint || id == 9) SetTextColor(dc, Muted);
            if (id == Banner) SetTextColor(dc, active ? RGB(85,194,122) : Accent);
            return reinterpret_cast<LRESULT>(background);
        }
        case WM_ERASEBKGND: { RECT rect{}; GetClientRect(window, &rect); FillRect(reinterpret_cast<HDC>(w), &rect, background); return 1; }
        case WM_CLOSE: if (trayAdded || vm.ButtonRegistered()) HideMain(); else ExitMain(); return 0;
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION: if (w) ExitMain(); return 0;
        case WM_DESTROY: KillTimer(window, 1); KillTimer(window, 2); worker.request_stop(); workerWake.notify_all(); midi.Close(); vm.Close(); RemoveTray(); PostQuitMessage(0); return 0;
        }
        // DefWindowProc can run a modal move/resize loop. Keep processing
        // alive while the user holds or drags the window title bar.
        lock.unlock();
        return DefWindowProcW(window, message, w, l);
    }
};
LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        app->window = window; SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    return app ? app->Handle(message, w, l) : DefWindowProcW(window, message, w, l);
}
}

#ifdef ELKA_UI_INSPECTION
int MainWindowPreview() {
    const HINSTANCE instance = GetModuleHandleW(nullptr); const auto arguments = L"--show"; const int show = SW_SHOW;
    constexpr auto windowClass = L"ElkaMidiPickupUiInspection", mutexName = L"Local\\ElkaMidiPickupUiInspection";
#else
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int show) {
    constexpr auto windowClass = L"ElkaMidiPickupTestWindow", mutexName = L"Local\\ElkaMidiPickupTestApp";
#endif
    const HANDLE singleInstance = CreateMutexW(nullptr, FALSE, mutexName);
    if (singleInstance && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (const auto existing = FindWindowW(windowClass, nullptr)) {
            ShowWindow(existing, IsIconic(existing) ? SW_RESTORE : SW_SHOW); SetForegroundWindow(existing);
        }
        CloseHandle(singleInstance); return 0;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
    Application application(instance, arguments ? arguments : L"");
    WNDCLASSEXW type{sizeof(type)}; type.hInstance = instance; type.lpfnWndProc = WindowProcedure;
    type.lpszClassName = windowClass; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    type.hIcon = AppIcon(instance, GetSystemMetrics(SM_CXICON)); type.hIconSm = AppIcon(instance, GetSystemMetrics(SM_CXSMICON));
    RegisterClassExW(&type);
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int dpi = static_cast<int>(GetDpiForSystem());
    const int width = std::min(MulDiv(1010, dpi, 96), static_cast<int>(work.right - work.left - 32));
    const int height = std::min(MulDiv(780, dpi, 96), static_cast<int>(work.bottom - work.top - 32));
    HWND window = CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"Elka MIDI Pickup", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL,
        work.left + (work.right - work.left - width) / 2, work.top + (work.bottom - work.top - height) / 2, width, height, nullptr, nullptr, instance, &application);
    if (!window) { if (singleInstance) CloseHandle(singleInstance); return 1; }
    const BOOL dark = TRUE; DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
    const COLORREF caption = Back, captionText = Ink;
    DwmSetWindowAttribute(window, 35, &caption, sizeof(caption)); DwmSetWindowAttribute(window, 36, &captionText, sizeof(captionText));
    ShowWindow(window, application.StartHidden() ? SW_HIDE : show); UpdateWindow(window);
#ifdef ELKA_UI_INSPECTION
    SetWindowTextW(window, L"Elka MIDI Pickup — UI inspection (no connections)"); ShowWindow(window, SW_SHOW);
#endif
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) if (!application.Dialog(message) && !IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (singleInstance) CloseHandle(singleInstance);
    return 0;
}

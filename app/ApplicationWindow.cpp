#include "ApplicationWindow.h"
#include "resource.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <sstream>
#include <iomanip>
#include <map>

namespace elka::apps {
namespace {
constexpr COLORREF Back = RGB(23, 21, 24), Field = RGB(33, 30, 33), Ink = RGB(242, 235, 229), Muted = RGB(185, 173, 167), Amber = RGB(239, 168, 97);
enum Id { Title = 10, Subtitle, Vaio, Aux, Vaio3, EndpointLabel, EndpointList, AppLabel, AppList, AddApp, Rows,
    Learn, Mode, Remove, ToleranceLabel, ToleranceList, Confirm, Start, Stop, Rearm, Save, Close, Status, SelectionHint };
const wchar_t* ActionName(Action action) {
    switch (action) { case Action::Volume: return L"Volume / pickup"; case Action::Mute: return L"Mute button";
    case Action::RelativeTwos: return L"Encoder / 2's comp"; case Action::RelativeOffset: return L"Encoder / offset 64"; }
    return L"";
}
void Set(HWND control, const std::wstring& value) {
    const int n = GetWindowTextLengthW(control); std::wstring old(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(control, old.data(), n + 1); old.resize(n);
    if (old != value) SetWindowTextW(control, value.c_str());
}
std::wstring Db(double scalar) {
    if (scalar <= 0) return L"Silence";
    std::wostringstream out; out << std::fixed << std::setprecision(1) << 20 * std::log10(scalar) << L" dB"; return out.str();
}
std::wstring State(const Row& row, const View& view) {
    if (row.mapping.id == view.learning) return L"Move control...";
    if (!row.mapping.use) return L"Disabled";
    if (!row.mapping.source.Valid()) return L"Not learned";
    if (row.mapping.route >= view.edition) return L"Input unavailable";
    if (row.blocked) return L"Assignment conflict";
    if (row.failed) return L"Stopped / re-arm";
    if (!row.target.ready) return L"App offline";
    if (!row.Ready()) return L"Mixed session levels";
    if (!view.active) return L"Monitor";
    if (row.mapping.action == Action::Mute) return row.releaseRequired ? L"Release button once" : row.target.muted ? L"Muted" : L"Unmuted";
    if (row.mapping.action != Action::Volume) return L"Relative control";
    if (row.pickup.tracking) return L"Tracking";
    if (!row.pickup.hardware) return L"Move fader";
    return *row.pickup.hardware / 127.0 < row.target.volume ? L"Move up to pick up" : L"Move down to pick up";
}
}
HWND Window::Add(const wchar_t* type, const wchar_t* text, DWORD style, int id) {
    const HWND control = CreateWindowExW(0, type, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0,
        window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SetWindowTheme(control, L"", L"");
    if (wcscmp(type, L"COMBOBOX") == 0) SetWindowSubclass(control, DarkControl, 1, reinterpret_cast<DWORD_PTR>(this));
    if (wcscmp(type, L"BUTTON") == 0 && (style & BS_TYPEMASK) == BS_AUTOCHECKBOX) SetWindowSubclass(control, DarkControl, 2, reinterpret_cast<DWORD_PTR>(this));
    return control;
}
LRESULT CALLBACK Window::DarkControl(HWND control, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR context) {
    auto& self = *reinterpret_cast<Window*>(context);
    if (message == WM_NCDESTROY) RemoveWindowSubclass(control, DarkControl, id);
    if (id == 3 && (message == WM_NOTIFY || message == WM_DRAWITEM)) {
        const auto result = SendMessageW(self.window, message, w, l);
        if (result) return result;
    }
    if (message == WM_PAINT && id == 2) {
        PAINTSTRUCT paint{}; const auto dc = BeginPaint(control, &paint); RECT rect{}; GetClientRect(control, &rect);
        FillRect(dc, &rect, self.background); SetBkMode(dc, TRANSPARENT); SelectObject(dc, self.font);
        RECT box{self.Scale(2), (rect.bottom - self.Scale(16)) / 2, self.Scale(18), (rect.bottom + self.Scale(16)) / 2};
        const bool checked = SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED;
        const auto brush = CreateSolidBrush(Amber); if (checked) FillRect(dc, &box, brush); else FrameRect(dc, &box, brush); DeleteObject(brush);
        if (checked) { SetTextColor(dc, Back); DrawTextW(dc, L"\u2713", -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE); }
        rect.left += self.Scale(28); SetTextColor(dc, Ink); wchar_t text[256]{}; GetWindowTextW(control, text, 256);
        DrawTextW(dc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE); if (GetFocus() == control) DrawFocusRect(dc, &rect);
        EndPaint(control, &paint); return 0;
    }
    const auto result = DefSubclassProc(control, message, w, l);
    if (message == WM_PAINT && id == 1) {
        const auto dc = GetDC(control); RECT rect{}; GetClientRect(control, &rect); COMBOBOXINFO info{sizeof(info)}; GetComboBoxInfo(control, &info);
        FillRect(dc, &info.rcButton, self.field); const int x = (info.rcButton.left + info.rcButton.right) / 2, y = (info.rcButton.top + info.rcButton.bottom) / 2;
        const auto pen = CreatePen(PS_SOLID, 1, Ink); const auto old = SelectObject(dc, pen);
        MoveToEx(dc, x - self.Scale(4), y - self.Scale(2), nullptr); LineTo(dc, x, y + self.Scale(2)); LineTo(dc, x + self.Scale(4), y - self.Scale(2));
        SelectObject(dc, old); DeleteObject(pen); const auto border = CreateSolidBrush(GetFocus() == control ? Amber : RGB(76, 66, 63));
        FrameRect(dc, &rect, border); InflateRect(&rect, -1, -1); FrameRect(dc, &rect, border); DeleteObject(border); ReleaseDC(control, dc);
    }
    return result;
}
Window::~Window() {
    if (window && IsWindow(window)) DestroyWindow(window);
    if (font) DeleteObject(font); if (heading) DeleteObject(heading);
    if (background) DeleteObject(background); if (field) DeleteObject(field);
}
HWND Window::Show(HWND parent, HINSTANCE module) {
    if (!window) {
        owner = parent; instance = module; dpi = static_cast<int>(parent ? GetDpiForWindow(parent) : GetDpiForSystem());
        background = CreateSolidBrush(Back); field = CreateSolidBrush(Field);
        WNDCLASSEXW type{sizeof(type)}; type.hInstance = module; type.lpfnWndProc = Procedure;
        type.lpszClassName = L"ElkaApplicationMidiPreview"; type.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        type.hIcon = LoadIconW(module, MAKEINTRESOURCEW(IDI_MIDI_PICKUP)); type.hIconSm = type.hIcon;
        RegisterClassExW(&type);
        window = CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"Application MIDI Control — Preview", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, Scale(1130), Scale(740), parent, nullptr, module, this);
        const BOOL dark = TRUE; DwmSetWindowAttribute(window, 20, &dark, sizeof(dark));
        DwmSetWindowAttribute(window, 35, &Back, sizeof(Back)); DwmSetWindowAttribute(window, 36, &Ink, sizeof(Ink));
    }
    Render(); ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW); SetForegroundWindow(window);
    return window;
}
void Window::Fonts() {
    if (font) DeleteObject(font); if (heading) DeleteObject(heading);
    font = CreateFontW(-Scale(15), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    heading = CreateFontW(-Scale(27), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    for (HWND child = GetWindow(window, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetDlgCtrlID(child) == Title ? heading : font), TRUE);
}
void Window::Layout() {
    if (!Control(Rows)) return;
    RECT rect{}; GetClientRect(window, &rect);
    const int width = MulDiv(rect.right, 96, dpi), height = MulDiv(rect.bottom, 96, dpi), right = width - 24;
    auto move = [&](int id, int x, int y, int w, int h) { MoveWindow(Control(id), Scale(x), Scale(y), Scale(std::max(w, 10)), Scale(h), TRUE); };
    move(Title, 24, 15, right - 24, 38); move(Subtitle, 24, 59, right - 24, 24);
    move(Vaio, 24, 94, 130, 32); move(Aux, 164, 94, 130, 32); move(Vaio3, 304, 94, 130, 32);
    move(EndpointLabel, 24, 145, 150, 24); move(EndpointList, 182, 139, right - 182, 250);
    move(AppLabel, 24, 188, 150, 24); move(AppList, 182, 182, right - 326, 300); move(AddApp, right - 130, 182, 130, 32);
    move(Rows, 24, 232, right - 24, height - 444);
    const int bottom = height - 196;
    move(Learn, 24, bottom, 140, 32); move(Mode, 176, bottom, 236, 180); move(Remove, 424, bottom, 130, 32);
    move(ToleranceLabel, right - 253, bottom + 5, 110, 24); move(ToleranceList, right - 140, bottom, 140, 160);
    move(SelectionHint, 24, bottom + 39, right - 24, 24);
    move(Confirm, 24, bottom + 67, right - 24, 27);
    move(Start, 24, bottom + 105, 175, 32); move(Stop, 209, bottom + 105, 90, 32); move(Rearm, 309, bottom + 105, 105, 32);
    move(Save, 424, bottom + 105, 170, 32); move(Close, right - 140, bottom + 105, 140, 32);
    move(Status, 24, bottom + 149, right - 24, 43);
    const int fixed[] = {48, 130, 0, 155, 90, 115, 180};
    for (int i = 0; i < 7; ++i) ListView_SetColumnWidth(Control(Rows), i, Scale(i == 2 ? std::max(140, width - 770) : fixed[i]));
    InvalidateRect(window, nullptr, TRUE);
}
uint64_t Window::Selected() const {
    const int index = ListView_GetNextItem(Control(Rows), -1, LVNI_SELECTED);
    return index >= 0 && index < static_cast<int>(visible.size()) ? visible[index] : 0;
}
void Window::Render() {
    if (!window) return;
    rendering = true; view = controller.Snapshot();
    if (view.edition > 0 && route >= view.edition) route = 0;
    for (int i = 0; i < 3; ++i) { ShowWindow(Control(Vaio + i), view.edition == 0 || i < view.edition ? SW_SHOW : SW_HIDE); EnableWindow(Control(Vaio + i), view.edition > i); InvalidateRect(Control(Vaio + i), nullptr, TRUE); }
    Set(Control(Subtitle), view.active ? L"PREVIEW RUNNING  /  Individual application volume and mute" : L"PREVIEW STOPPED  /  Strip and bus pickup continues independently");
    std::wstring signature = std::to_wstring(route) + view.selected[route];
    for (const auto& e : view.endpoints) if (e.voiceMeeter) signature += e.id + e.name;
    if (signature != endpointSignature) {
        endpointSignature = signature; endpointKeys.clear(); SendMessageW(Control(EndpointList), CB_RESETCONTENT, 0, 0);
        int selected = -1;
        for (const auto& e : view.endpoints) if (e.voiceMeeter) {
            if (e.id == view.selected[route]) selected = static_cast<int>(endpointKeys.size());
            endpointKeys.push_back(e.id); SendMessageW(Control(EndpointList), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(e.name.c_str()));
        }
        if (selected < 0 && !view.selected[route].empty()) {
            selected = static_cast<int>(endpointKeys.size()); endpointKeys.push_back(view.selected[route]);
            SendMessageW(Control(EndpointList), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Saved playback endpoint is offline"));
        }
        SendMessageW(Control(EndpointList), CB_SETCURSEL, selected, 0);
    }
    std::map<std::wstring, std::wstring> apps;
    for (const auto& s : view.sessions) if (s.endpoint == view.selected[route]) apps[s.appKey] = s.name;
    signature = view.selected[route]; for (const auto& [key, name] : apps) signature += key + name;
    if (signature != appSignature) {
        const int old = static_cast<int>(SendMessageW(Control(AppList), CB_GETCURSEL, 0, 0));
        const auto previous = old >= 0 && old < static_cast<int>(appKeys.size()) ? appKeys[old] : L"";
        appSignature = signature; appKeys.clear(); SendMessageW(Control(AppList), CB_RESETCONTENT, 0, 0); int selected = 0;
        for (const auto& [key, name] : apps) {
            if (key == previous) selected = static_cast<int>(appKeys.size());
            appKeys.push_back(key); SendMessageW(Control(AppList), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        if (appKeys.empty()) SendMessageW(Control(AppList), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No applications here — play audio on this input first"));
        SendMessageW(Control(AppList), CB_SETCURSEL, selected, 0);
    }
    EnableWindow(Control(AddApp), view.edition > 0 && !appKeys.empty());
    const auto oldSelection = Selected();
    std::vector<uint64_t> next;
    for (const auto& r : view.rows) if (r.mapping.route == route) next.push_back(r.mapping.id);
    if (next != visible) {
        uint64_t selection = oldSelection;
        if (std::find(next.begin(), next.end(), oldSelection) == next.end()) selection = next.empty() ? 0 : next.front();
        else if (next.size() > visible.size()) for (const auto id : next) if (std::find(visible.begin(), visible.end(), id) == visible.end()) { selection = id; break; }
        visible = std::move(next); ListView_DeleteAllItems(Control(Rows));
        for (size_t i = 0; i < visible.size(); ++i) {
            LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = const_cast<LPWSTR>(L""); ListView_InsertItem(Control(Rows), &item);
            if (visible[i] == selection) ListView_SetItemState(Control(Rows), static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
    }
    for (size_t i = 0; i < visible.size(); ++i) {
        const auto found = std::find_if(view.rows.begin(), view.rows.end(), [&](const Row& row) { return row.mapping.id == visible[i]; });
        if (found == view.rows.end()) continue;
        const auto& row = *found; const auto& source = row.mapping.source;
        ListView_SetCheckState(Control(Rows), static_cast<int>(i), row.mapping.use);
        const std::wstring values[] = {L"", source.Valid() ? L"Ch " + std::to_wstring(source.channel + 1) + (source.kind == 0 ? L" / CC " : L" / Note ") + std::to_wstring(source.number) : L"Not learned",
            row.mapping.name, ActionName(row.mapping.action), row.pickup.hardware ? std::to_wstring(*row.pickup.hardware) : L"--",
            !row.target.ready ? L"--" : row.target.mixedVolume ? L"Mixed" : Db(row.target.volume) + (row.target.muted ? L" / M" : L""), State(row, view)};
        for (int column = 1; column < 7; ++column) {
            wchar_t old[1024]{}; ListView_GetItemText(Control(Rows), static_cast<int>(i), column, old, 1024);
            if (values[column] != old) ListView_SetItemText(Control(Rows), static_cast<int>(i), column, const_cast<LPWSTR>(values[column].c_str()));
        }
    }
    const auto selected = Selected(); const auto row = std::find_if(view.rows.begin(), view.rows.end(), [&](const Row& r) { return r.mapping.id == selected; });
    for (auto id : {Learn, Mode, Remove}) EnableWindow(Control(id), selected != 0);
    if (row != view.rows.end()) {
        SendMessageW(Control(Mode), CB_SETCURSEL, static_cast<int>(row->mapping.action), 0);
        const auto endpoint = std::find_if(view.endpoints.begin(), view.endpoints.end(), [&](const Endpoint& e) { return e.id == row->mapping.endpoint; });
        Set(Control(SelectionHint), row->mapping.name + L"  /  " + (endpoint == view.endpoints.end() ? L"Saved endpoint offline" : endpoint->name));
    } else Set(Control(SelectionHint), L"Select a row to learn or change its action. All input pages run together.");
    Set(Control(Learn), selected && selected == view.learning ? L"Cancel Learn" : L"Learn selected");
    SendMessageW(Control(Confirm), BM_SETCHECK, view.confirmed ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(Control(ToleranceList), CB_SETCURSEL, view.tolerance, 0);
    EnableWindow(Control(Start), !view.active && view.connected); EnableWindow(Control(Stop), view.active || view.learning);
    Set(Control(Save), view.dirty ? L"Save applications *" : L"Save applications"); Set(Control(Status), view.notice);
    rendering = false;
}
void Window::Command(int id, int code) {
    if (rendering) return;
    apps::Command command{Operation::Stop}; command.row = Selected();
    if (code == CBN_SELCHANGE) {
        const int selected = static_cast<int>(SendMessageW(Control(id), CB_GETCURSEL, 0, 0));
        if (id == EndpointList && selected >= 0 && selected < static_cast<int>(endpointKeys.size())) {
            command.operation = Operation::Endpoint; command.route = route; command.key = endpointKeys[selected];
        } else if (id == Mode) { command.operation = Operation::Mode; command.value = selected; }
        else if (id == ToleranceList) { command.operation = Operation::Tolerance; command.value = selected; }
        else return;
    } else if (code == BN_CLICKED) {
        if (id >= Vaio && id <= Vaio3) { route = id - Vaio; Render(); return; }
        if (id == Close) { ShowWindow(window, SW_HIDE); return; }
        switch (id) {
        case AddApp: {
            const int selected = static_cast<int>(SendMessageW(Control(AppList), CB_GETCURSEL, 0, 0));
            if (selected < 0 || selected >= static_cast<int>(appKeys.size())) return;
            command.operation = Operation::Add; command.route = route; command.key = appKeys[selected]; break;
        }
        case Learn: command.operation = Operation::Learn; break;
        case Remove: command.operation = Operation::Remove; break;
        case Start: command.operation = Operation::Start; break;
        case Stop: command.operation = Operation::Stop; break;
        case Rearm: command.operation = Operation::Rearm; break;
        case Save: command.operation = Operation::Save; break;
        case Confirm: command.operation = Operation::Confirm; command.value = SendMessageW(Control(Confirm), BM_GETCHECK, 0, 0) == BST_CHECKED; break;
        default: return;
        }
    } else return;
    controller.Send(std::move(command));
}
LRESULT Window::Handle(UINT message, WPARAM w, LPARAM l) {
    switch (message) {
    case WM_CREATE: {
        Add(L"STATIC", L"Application MIDI Control", SS_LEFT, Title); Add(L"STATIC", L"PREVIEW", SS_LEFT, Subtitle);
        for (const auto& [id, name] : {std::pair{Vaio, L"VAIO"}, {Aux, L"AUX"}, {Vaio3, L"VAIO 3"}, {AddApp, L"Add application"}, {Learn, L"Learn selected"},
            {Remove, L"Remove row"}, {Start, L"Start applications"}, {Stop, L"Stop"}, {Rearm, L"Re-arm"}, {Save, L"Save applications"}, {Close, L"Hide window"}})
            Add(L"BUTTON", name, BS_OWNERDRAW | WS_TABSTOP, id);
        Add(L"STATIC", L"Playback endpoint", SS_LEFT, EndpointLabel); Add(L"STATIC", L"Connected apps", SS_LEFT, AppLabel);
        Add(L"STATIC", L"Tolerance", SS_LEFT, ToleranceLabel); Add(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, SelectionHint); Add(L"STATIC", L"", SS_LEFT, Status);
        for (auto id : {EndpointList, AppList, Mode, ToleranceList}) Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, id);
        for (int i = 0; i < 4; ++i) {
            SendMessageW(Control(Mode), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ActionName(static_cast<Action>(i))));
            const auto label = std::to_wstring(i) + L" MIDI steps"; SendMessageW(Control(ToleranceList), CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        Add(L"BUTTON", L"These new MIDI controls have no duplicate direct mappings in VoiceMeeter / Bome.", BS_AUTOCHECKBOX | WS_TABSTOP, Confirm);
        Add(WC_LISTVIEWW, L"Application MIDI mappings", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP, Rows);
        ListView_SetExtendedListViewStyle(Control(Rows), LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(Control(Rows), Field); ListView_SetTextBkColor(Control(Rows), Field); ListView_SetTextColor(Control(Rows), Ink);
        SetWindowSubclass(Control(Rows), DarkControl, 3, reinterpret_cast<DWORD_PTR>(this));
        SetWindowTheme(ListView_GetHeader(Control(Rows)), L"", L"");
        int i = 0;
        for (const auto name : {L"Use", L"MIDI source", L"Application", L"Action", L"Hardware", L"Software", L"Pickup state"}) {
            LVCOLUMNW column{}; column.mask = LVCF_TEXT | LVCF_WIDTH; column.cx = 100; column.pszText = const_cast<LPWSTR>(name); ListView_InsertColumn(Control(Rows), i++, &column);
        }
        Fonts(); Layout(); SetTimer(window, 1, 100, nullptr); return 0;
    }
    case WM_COMMAND: Command(LOWORD(w), HIWORD(w)); return 0;
    case WM_TIMER: if (IsWindowVisible(window) && !IsIconic(window)) Render(); return 0;
    case WM_SIZE: Layout(); return 0;
    case WM_DPICHANGED: { dpi = HIWORD(w); Fonts(); const auto rect = reinterpret_cast<RECT*>(l); SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE); Layout(); return 0; }
    case WM_GETMINMAXINFO: { auto info = reinterpret_cast<MINMAXINFO*>(l); info->ptMinTrackSize = {Scale(1040), Scale(660)}; return 0; }
    case WM_NOTIFY: {
        const auto header = reinterpret_cast<NMHDR*>(l);
        if (header->idFrom == Rows && header->code == NM_CUSTOMDRAW) {
            const auto draw = reinterpret_cast<NMLVCUSTOMDRAW*>(l);
            if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                const bool selected = (ListView_GetItemState(Control(Rows), static_cast<int>(draw->nmcd.dwItemSpec), LVIS_SELECTED) & LVIS_SELECTED) != 0;
                draw->clrText = Ink; draw->clrTextBk = selected ? RGB(75, 53, 39) : Field;
                draw->nmcd.uItemState &= ~CDIS_SELECTED; return CDRF_NEWFONT;
            }
        }
        if (header->idFrom == Rows && header->code == LVN_ITEMCHANGED && !rendering) {
            const auto change = reinterpret_cast<NMLISTVIEW*>(l);
            if ((change->uChanged & LVIF_STATE) && ((change->uNewState ^ change->uOldState) & LVIS_STATEIMAGEMASK) && change->iItem >= 0 && change->iItem < static_cast<int>(visible.size())) {
                apps::Command command{Operation::Use}; command.row = visible[change->iItem]; command.value = ListView_GetCheckState(Control(Rows), change->iItem) ? 1 : 0; controller.Send(std::move(command));
            }
        }
        if (header->code == NM_CUSTOMDRAW && header->hwndFrom == ListView_GetHeader(Control(Rows))) {
            const auto draw = reinterpret_cast<NMCUSTOMDRAW*>(l);
            if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                FillRect(draw->hdc, &draw->rc, field); SetTextColor(draw->hdc, Amber); SetBkMode(draw->hdc, TRANSPARENT); SelectObject(draw->hdc, font);
                wchar_t text[128]{}; HDITEMW item{}; item.mask = HDI_TEXT; item.pszText = text; item.cchTextMax = 128;
                Header_GetItem(header->hwndFrom, static_cast<int>(draw->dwItemSpec), &item); RECT rect = draw->rc; rect.left += Scale(5);
                DrawTextW(draw->hdc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE); return CDRF_SKIPDEFAULT;
            }
        }
        break;
    }
    case WM_DRAWITEM: {
        const auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(l); if (draw->CtlType != ODT_BUTTON) break;
        const int id = static_cast<int>(draw->CtlID); const bool selected = (id == Vaio + route || id == Start) && !(draw->itemState & ODS_DISABLED);
        const auto brush = CreateSolidBrush((draw->itemState & ODS_SELECTED) ? RGB(101, 74, 54) : selected ? Amber : Field); FillRect(draw->hDC, &draw->rcItem, brush); DeleteObject(brush);
        SetBkMode(draw->hDC, TRANSPARENT); SetTextColor(draw->hDC, (draw->itemState & ODS_DISABLED) ? Muted : selected ? Back : Ink); SelectObject(draw->hDC, font);
        wchar_t text[128]{}; GetWindowTextW(draw->hwndItem, text, 128); RECT rect = draw->rcItem;
        DrawTextW(draw->hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE); if (draw->itemState & ODS_FOCUS) { InflateRect(&rect, -3, -3); DrawFocusRect(draw->hDC, &rect); } return TRUE;
    }
    case WM_CTLCOLORLISTBOX: case WM_CTLCOLOREDIT: SetTextColor(reinterpret_cast<HDC>(w), Ink); SetBkColor(reinterpret_cast<HDC>(w), Field); return reinterpret_cast<LRESULT>(field);
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: SetTextColor(reinterpret_cast<HDC>(w), GetDlgCtrlID(reinterpret_cast<HWND>(l)) == Subtitle ? Amber : Ink); SetBkColor(reinterpret_cast<HDC>(w), Back); return reinterpret_cast<LRESULT>(background);
    case WM_ERASEBKGND: { RECT rect{}; GetClientRect(window, &rect); FillRect(reinterpret_cast<HDC>(w), &rect, background); return 1; }
    case WM_CLOSE: ShowWindow(window, SW_HIDE); return 0;
    case WM_DESTROY: KillTimer(window, 1); return 0;
    case WM_NCDESTROY: {
        const auto destroyed = window; SetWindowLongPtrW(destroyed, GWLP_USERDATA, 0); window = nullptr;
        return DefWindowProcW(destroyed, message, w, l);
    }
    }
    return DefWindowProcW(window, message, w, l);
}
LRESULT CALLBACK Window::Procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    auto self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) { self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
    return self ? self->Handle(message, w, l) : DefWindowProcW(hwnd, message, w, l);
}
}

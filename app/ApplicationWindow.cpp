#include "ApplicationWindow.h"
#include "resource.h"
#include <dwmapi.h>
#include <uxtheme.h>
#include <sstream>
#include <iomanip>
#include <map>
#include <windowsx.h>

namespace elka::apps {
namespace {
constexpr COLORREF Back = RGB(23, 21, 24), Field = RGB(33, 30, 33), Ink = RGB(242, 235, 229), Muted = RGB(185, 173, 167), Amber = RGB(239, 168, 97);
enum Id { Title = 10, Subtitle, Vaio, Aux, Vaio3, AppLabel, Refresh, RestoreHidden, AddApp, Rows,
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
    if (!RouteAvailable(row.mapping.route, view.edition)) return L"Input unavailable";
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
    if (message == WM_MOUSEWHEEL && id == 1 && !SendMessageW(control, CB_GETDROPPEDSTATE, 0, 0)) return 0;
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
    auto move = [&](int id, int x, int y, int w, int h) { controlLayout.Place(Control(id), Scale(x), Scale(y), Scale(std::max(w, 10)), Scale(h)); };
    move(Title, 24, 15, right - 24, 38); move(Subtitle, 24, 59, right - 24, 24);
    int tabX = 24;
    for (int i = 0; i < InputCount; ++i) {
        move(Vaio + i, tabX, 94, 90, 32);
        if (RouteAvailable(i, view.edition)) tabX += 98;
    }
    move(Refresh, right - 148, 94, 148, 32);
    move(RestoreHidden, right - 350, 94, 190, 32);
    move(AppLabel, 24, 141, right - 24, 38);
    move(Rows, 24, 184, right - 24, height - 396);
    const int bottom = height - 196;
    move(Learn, 24, bottom, 140, 32); move(Mode, 176, bottom, 224, 180);
    move(AddApp, 412, bottom, 130, 32); move(Remove, 554, bottom, 130, 32);
    move(ToleranceLabel, right - 253, bottom + 5, 110, 24); move(ToleranceList, right - 140, bottom, 140, 160);
    move(SelectionHint, 24, bottom + 39, right - 24, 24);
    move(Confirm, 24, bottom + 67, right - 24, 27);
    move(Start, 24, bottom + 105, 175, 32); move(Stop, 209, bottom + 105, 90, 32); move(Rearm, 309, bottom + 105, 105, 32);
    move(Save, 424, bottom + 105, 170, 32); move(Close, right - 140, bottom + 105, 140, 32);
    move(Status, 24, bottom + 149, right - 24, 43);
    const int fixed[] = {48, 130, 0, 155, 90, 115, 180};
    for (int i = 0; i < 7; ++i) ListView_SetColumnWidth(Control(Rows), i, Scale(i == 2 ? std::max(140, width - 770) : fixed[i]));
    controlLayout.Finish(window);
}
uint64_t Window::Selected() const {
    const int index = ListView_GetNextItem(Control(Rows), -1, LVNI_SELECTED);
    return index >= 0 && index < static_cast<int>(visible.size()) ? visible[index] : 0;
}
void Window::Render() {
    if (!window) return;
    rendering = true; const int previousEdition = view.edition; view = controller.Snapshot();
    if (!RouteAvailable(route, view.edition)) route = 0;
    for (int i = 0; i < InputCount; ++i) {
        const bool available = RouteAvailable(i, view.edition);
        const bool detected = std::any_of(view.endpoints.begin(), view.endpoints.end(), [&](const Endpoint& e) { return e.route == i; });
        if ((IsWindowVisible(Control(Vaio + i)) != FALSE) != available) ShowWindow(Control(Vaio + i), available ? SW_SHOW : SW_HIDE);
        if ((IsWindowEnabled(Control(Vaio + i)) != FALSE) != (available && detected)) EnableWindow(Control(Vaio + i), available && detected);
    }
    if (view.edition != previousEdition) Layout();
    Set(Control(Subtitle), view.active ? L"PREVIEW RUNNING  /  Individual application volume and mute" : L"PREVIEW STOPPED  /  Strip and bus pickup continues independently");
    const auto oldSelection = Selected();
    const int topIndex = ListView_GetTopIndex(Control(Rows));
    const auto topId = topIndex >= 0 && topIndex < static_cast<int>(visible.size()) ? visible[topIndex] : 0;
    std::vector<uint64_t> next;
    for (const auto& r : view.rows) if (RouteAvailable(route, view.edition) && r.mapping.route == route && !r.mapping.hidden && !r.target.instances.empty()) next.push_back(r.mapping.id);
    const auto hidden = std::count_if(view.rows.begin(), view.rows.end(), [&](const Row& r) { return r.mapping.route == route && r.mapping.hidden; });
    Set(Control(RestoreHidden), L"Restore removed (" + std::to_wstring(hidden) + L")");
    EnableWindow(Control(RestoreHidden), hidden != 0 && RouteAvailable(route, view.edition));
    Set(Control(AppLabel), view.edition == 0 ? L"Connect VoiceMeeter in the main window to detect its inputs." : next.empty() ?
        std::wstring(RouteName(route)) + (hidden ? L" — No visible application rows. Use Restore removed, or start playback on this input." :
            L" — No applications connected. Start playback on this input; the list updates automatically.") :
        std::wstring(RouteName(route)) + L" — Tick Use for the apps you want to control, then select a row and Learn. All input pages run together.");
    if (next != visible) {
        uint64_t selection = oldSelection;
        if (std::find(next.begin(), next.end(), oldSelection) == next.end()) selection = next.empty() ? 0 : next.front();
        visible = std::move(next); ListView_DeleteAllItems(Control(Rows));
        for (size_t i = 0; i < visible.size(); ++i) {
            LVITEMW item{}; item.mask = LVIF_TEXT; item.iItem = static_cast<int>(i); item.pszText = const_cast<LPWSTR>(L""); ListView_InsertItem(Control(Rows), &item);
            if (visible[i] == selection) ListView_SetItemState(Control(Rows), static_cast<int>(i), LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
        const auto top = std::find(visible.begin(), visible.end(), topId);
        if (top != visible.end()) {
            RECT item{};
            if (ListView_GetItemRect(Control(Rows), 0, &item, LVIR_BOUNDS))
                ListView_Scroll(Control(Rows), 0, static_cast<int>(top - visible.begin()) * (item.bottom - item.top));
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
    for (auto id : {Learn, Mode, Remove, AddApp}) if ((IsWindowEnabled(Control(id)) != FALSE) != (selected != 0)) EnableWindow(Control(id), selected != 0);
    if (row != view.rows.end()) {
        if (!SendMessageW(Control(Mode), CB_GETDROPPEDSTATE, 0, 0)) SendMessageW(Control(Mode), CB_SETCURSEL, static_cast<int>(row->mapping.action), 0);
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
        if (id == Mode) { command.operation = Operation::Mode; command.value = selected; }
        else if (id == ToleranceList) { command.operation = Operation::Tolerance; command.value = selected; }
        else return;
    } else if (code == BN_CLICKED) {
        if (id >= Vaio && id <= Vaio3) { route = id - Vaio; for (int i = 0; i < InputCount; ++i) InvalidateRect(Control(Vaio + i), nullptr, TRUE); Render(); return; }
        if (id == Close) { ShowWindow(window, SW_HIDE); return; }
        switch (id) {
        case AddApp: command.operation = Operation::Add; break;
        case Refresh: command.operation = Operation::Refresh; break;
        case RestoreHidden: command.operation = Operation::RestoreHidden; command.route = route; break;
        case Learn: command.operation = Operation::Learn; break;
        case Remove: command.operation = Operation::Hide; break;
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
        for (int i = 0; i < InputCount; ++i) Add(L"BUTTON", RouteName(i), BS_OWNERDRAW | WS_TABSTOP, Vaio + i);
        for (const auto& [id, name] : {std::pair{Refresh, L"Refresh apps"}, {RestoreHidden, L"Restore removed (0)"}, {AddApp, L"Extra control"}, {Learn, L"Learn selected"},
            {Remove, L"Remove row"}, {Start, L"Start applications"}, {Stop, L"Stop"}, {Rearm, L"Re-arm"}, {Save, L"Save applications"}, {Close, L"Hide window"}})
            Add(L"BUTTON", name, BS_OWNERDRAW | WS_TABSTOP, id);
        Add(L"STATIC", L"Connected apps appear automatically. Tick Use, select a row, then Learn.", SS_LEFT, AppLabel);
        Add(L"STATIC", L"Tolerance", SS_LEFT, ToleranceLabel); Add(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, SelectionHint); Add(L"STATIC", L"", SS_LEFT, Status);
        for (auto id : {Mode, ToleranceList}) Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, id);
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
    case WM_CONTEXTMENU: {
        if (reinterpret_cast<HWND>(w) != Control(Rows)) break;
        POINT point{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (point.x == -1 && point.y == -1) {
            const int index = ListView_GetNextItem(Control(Rows), -1, LVNI_SELECTED);
            RECT rect{};
            if (index < 0 || !ListView_GetItemRect(Control(Rows), index, &rect, LVIR_BOUNDS)) return 0;
            point = {rect.left + Scale(80), rect.bottom}; ClientToScreen(Control(Rows), &point);
        } else {
            LVHITTESTINFO hit{}; hit.pt = point; ScreenToClient(Control(Rows), &hit.pt);
            const int index = ListView_SubItemHitTest(Control(Rows), &hit);
            if (index < 0) return 0;
            ListView_SetItemState(Control(Rows), -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_SetItemState(Control(Rows), index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        }
        const auto id = Selected(); if (!id) return 0;
        const auto menu = CreatePopupMenu(); if (!menu) return 0;
        AppendMenuW(menu, MF_STRING, 1, L"Remove row from Elka");
        AppendMenuW(menu, MF_STRING, 2, L"Clear MIDI assignment");
        // Keep the clicked identity even if discovery refreshes while the menu is open.
        const auto choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, window, nullptr);
        DestroyMenu(menu);
        if (choice) {
            apps::Command command{choice == 1 ? Operation::Hide : Operation::Remove};
            command.row = id; controller.Send(std::move(command));
        }
        return 0;
    }
    case WM_DRAWITEM: {
        const auto draw = reinterpret_cast<DRAWITEMSTRUCT*>(l); if (draw->CtlType != ODT_BUTTON) break;
        const int id = static_cast<int>(draw->CtlID); const bool selected = (id == Vaio + route || id == Start) && !(draw->itemState & ODS_DISABLED);
        FillRect(draw->hDC, &draw->rcItem, background);
        const auto brush = CreateSolidBrush((draw->itemState & ODS_SELECTED) ? RGB(101, 74, 54) : selected ? Amber : Field);
        const auto previousBrush = SelectObject(draw->hDC, brush), previousPen = SelectObject(draw->hDC, GetStockObject(NULL_PEN));
        RoundRect(draw->hDC, draw->rcItem.left, draw->rcItem.top, draw->rcItem.right, draw->rcItem.bottom, Scale(10), Scale(10));
        SelectObject(draw->hDC, previousBrush); SelectObject(draw->hDC, previousPen); DeleteObject(brush);
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

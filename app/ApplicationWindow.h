#pragma once
#include "ApplicationControl.h"
#include <CommCtrl.h>

namespace elka::apps {
class Window {
    Controller& controller;
    HWND window{}, owner{};
    HINSTANCE instance{};
    HFONT font{}, heading{};
    HBRUSH background{}, field{};
    int dpi{96}, route{};
    bool rendering{};
    View view;
    std::vector<uint64_t> visible;
    std::vector<std::wstring> endpointKeys, appKeys;
    std::wstring endpointSignature, appSignature;
    HWND Control(int id) const { return GetDlgItem(window, id); }
    int Scale(int v) const { return MulDiv(v, dpi, 96); }
    HWND Add(const wchar_t* type, const wchar_t* text, DWORD style, int id);
    uint64_t Selected() const;
    void Fonts();
    void Layout();
    void Render();
    void Command(int id, int code);
    LRESULT Handle(UINT message, WPARAM w, LPARAM l);
    static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM w, LPARAM l);
    static LRESULT CALLBACK DarkControl(HWND control, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR context);
public:
    explicit Window(Controller& value) : controller(value) {}
    ~Window();
    HWND Show(HWND parent, HINSTANCE module);
    bool Dialog(MSG& message) { return window && IsWindowVisible(window) && IsDialogMessageW(window, &message); }
};
}

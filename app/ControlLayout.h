#pragma once
#include <Windows.h>
#include <map>

namespace elka {
// A dropdown's requested height includes its popup, but its actual window
// rectangle is collapsed. Keep the requested size instead of comparing the
// collapsed rectangle and resizing it on every wheel message.
class ControlLayout {
    struct Box { int x, y, width, height; bool operator==(const Box&) const = default; };
    std::map<HWND, Box> previous;
    bool changed{};
public:
    void Place(HWND control, int x, int y, int width, int height) {
        const Box next{x, y, width, height};
        const auto old = previous.find(control);
        if (old != previous.end() && old->second == next) return;
        UINT flags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOCOPYBITS;
        if (old != previous.end() && old->second.width == width && old->second.height == height) flags |= SWP_NOSIZE;
        SetWindowPos(control, nullptr, x, y, width, height, flags);
        previous[control] = next; changed = true;
    }
    void Finish(HWND parent) {
        if (changed) RedrawWindow(parent, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
        changed = false;
    }
};
}

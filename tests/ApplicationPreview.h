#pragma once
// Development-only inspection host. Shares the real preview window/controller,
// but provides no MIDI connection and never registers a VoiceMeeter button.
int ApplicationPreview() {
    using namespace elka::apps;
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 2;
    const auto path = std::filesystem::temp_directory_path() / (L"Elka-UI-preview-" + std::to_wstring(GetCurrentProcessId()) + L".applications");
    {
        AudioSessions audio; audio.Discover(); const auto sessions = audio.Read();
        Setup setup;
        for (const auto& endpoint : audio.Endpoints()) if (endpoint.route >= 0) {
            setup.endpoints[endpoint.route] = endpoint.id;
            std::set<std::wstring> seen;
            for (const auto& s : sessions) if (s.endpoint == endpoint.id && seen.insert(s.appKey).second)
                setup.mappings.push_back({setup.mappings.size() + 1, endpoint.route, endpoint.id, s.appKey, s.name, {}, Action::Volume, false});
        }
        SaveApplicationSetup(path, setup);
    }
    {
        elka::VoiceMeeter vm; vm.Connect(nullptr);
        Controller controller; controller.Launch(path); controller.Bridge(false, vm.edition, {});
        Window window(controller);
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&controls);
        const HWND handle = window.Show(nullptr, GetModuleHandleW(nullptr));
        ShowWindow(handle, SW_SHOW); // Keep the test console hidden, show only the inspected GUI.
        MSG message{};
        while (IsWindowVisible(handle) && GetMessageW(&message, nullptr, 0, 0) > 0) {
            controller.Bridge(false, vm.edition, {});
            if (!window.Dialog(message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        controller.Shutdown(); vm.Close();
    }
    std::filesystem::remove(path); CoUninitialize(); return 0;
}

// gui_main.cpp - Win32 GUI 入口（WinMain）
// 编译为 backupgui.exe，零第三方依赖，纯 Win32 API。
#include <windows.h>

#include "gui/MainWindow.h"

using namespace backup::gui;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                     PWSTR /*pCmdLine*/, int /*nCmdShow*/) {
    // 运行时启用 Per-Monitor V2 DPI 感知（避免模糊，无需嵌入清单）
    {
        HMODULE user32 = ::LoadLibraryW(L"user32.dll");
        if (user32) {
            using SetDpiCtxFn = BOOL(WINAPI*)(void*);
            auto setDpi = (SetDpiCtxFn)::GetProcAddress(user32, "SetProcessDpiAwarenessContext");
            if (setDpi) {
                // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4
                setDpi(reinterpret_cast<void*>(-4));
            } else {
                ::SetProcessDPIAware();
            }
            // 不 FreeLibrary：user32 为进程内常用 DLL
        }
    }

    // 初始化 COM（用于 SHBrowseForFolder 等）
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    MainWindow window;
    HWND hwnd = window.create(hInstance);
    if (!hwnd) {
        MessageBoxW(nullptr, L"主窗口创建失败", L"错误", MB_OK | MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // 主消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return static_cast<int>(msg.wParam);
}

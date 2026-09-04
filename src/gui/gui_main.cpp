// gui_main.cpp - Win32 GUI 入口（WinMain）
// 编译为 backupgui.exe，零第三方依赖，纯 Win32 API。
#include <windows.h>

#include "gui/MainWindow.h"

using namespace backup::gui;

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/,
                     PWSTR /*pCmdLine*/, int /*nCmdShow*/) {
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

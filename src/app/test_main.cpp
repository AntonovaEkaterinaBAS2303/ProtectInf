#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tchar.h>

#pragma comment(lib, "comctl32.lib")

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1
#define WM_TASKBAR_RESTART RegisterWindowMessage(_T("TaskbarCreated"))

HINSTANCE g_hInst = NULL;
HWND g_hWnd = NULL;
NOTIFYICONDATA g_nid = {};

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static UINT uTaskbarRestart = 0;

    switch (msg)
    {
    case WM_CREATE:
        uTaskbarRestart = WM_TASKBAR_RESTART;
        // Add tray icon
        ZeroMemory(&g_nid, sizeof(g_nid));
        g_nid.cbSize = sizeof(NOTIFYICONDATA);
        g_nid.hWnd = hWnd;
        g_nid.uID = ID_TRAY_ICON;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        _tcscpy_s(g_nid.szTip, _T("TEST TrayApp"));
        Shell_NotifyIcon(NIM_ADD, &g_nid);

        // MessageBox to prove it's running
        MessageBox(hWnd, _T("TrayApp started!\nCheck system tray for icon.\nClick OK to continue."), _T("TEST"), MB_OK);
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP)
        {
            // Right click - show menu and exit
            HMENU hMenu = CreatePopupMenu();
            InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, 1, _T("Exit"));
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_RIGHTALIGN, pt.x, pt.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == 1)
            DestroyWindow(hWnd);
        break;

    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        break;

    default:
        if (msg == uTaskbarRestart)
        {
            Shell_NotifyIcon(NIM_ADD, &g_nid);
        }
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int)
{
    g_hInst = hInstance;
    InitCommonControls();

    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = _T("TestTrayClass");
    RegisterClassEx(&wc);

    g_hWnd = CreateWindow(_T("TestTrayClass"), _T("Test"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 300, 200, NULL, NULL, hInstance, NULL);

    // Show window for debugging
    ShowWindow(g_hWnd, SW_SHOW);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
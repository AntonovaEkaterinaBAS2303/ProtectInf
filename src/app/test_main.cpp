#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tchar.h>
#include <fstream>

#pragma comment(lib, "comctl32.lib")

void LogToFile(const wchar_t* msg) {
    std::wofstream log;
    log.open(L"C:\\TrayApp_debug.log", std::ios::app);
    if (log.is_open()) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        log << L"[" << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"] " << msg << std::endl;
        log.close();
    }
}

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1

HINSTANCE g_hInstance = NULL;
HWND g_hWnd = NULL;
NOTIFYICONDATA g_nid = {};

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        LogToFile(L"WM_CREATE received");
        break;
    case WM_TRAYICON:
        LogToFile(L"WM_TRAYICON received");
        if (l == WM_RBUTTONUP) {
            PostQuitMessage(0);
        }
        break;
    case WM_DESTROY:
        LogToFile(L"WM_DESTROY received");
        Shell_NotifyIcon(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(h, m, w, l);
    }
    return 0;
}

void AddTrayIcon(HWND h) {
    LogToFile(L"Adding tray icon...");
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = h;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    _tcscpy_s(g_nid.szTip, _T("TrayApp Debug"));

    BOOL result = Shell_NotifyIcon(NIM_ADD, &g_nid);
    LogToFile(result ? L"Tray icon added successfully" : L"Failed to add tray icon");
}

int APIENTRY _tWinMain(HINSTANCE hi, HINSTANCE, LPTSTR, int) {
    LogToFile(L"========== TrayApp Debug Started ==========");

    g_hInstance = hi;
    InitCommonControls();

    // Проверяем аргументы
    LPWSTR* szArglist;
    int nArgs;
    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (szArglist) {
        for (int i = 0; i < nArgs; i++) {
            wchar_t buf[512];
            wsprintf(buf, L"Arg %d: %s", i, szArglist[i]);
            LogToFile(buf);
        }
        LocalFree(szArglist);
    }

    LogToFile(L"Registering window class...");
    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = _T("TrayAppDebugClass");
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
        LogToFile(L"RegisterClassEx FAILED");
        return 1;
    }

    LogToFile(L"Creating window...");
    g_hWnd = CreateWindowEx(
        0,
        _T("TrayAppDebugClass"),
        _T("TrayApp Debug"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 400, 300,
        NULL, NULL, hi, NULL
    );

    if (!g_hWnd) {
        wchar_t buf[256];
        wsprintf(buf, L"CreateWindowEx FAILED. Error: %d", GetLastError());
        LogToFile(buf);
        return 1;
    }

    LogToFile(L"Window created successfully");

    // Показываем окно для отладки
    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);

    AddTrayIcon(g_hWnd);

    LogToFile(L"Entering message loop...");

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    LogToFile(L"========== TrayApp Debug Ended ==========");
    return 0;
}
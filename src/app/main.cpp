#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tchar.h>
#include <tlhelp32.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1
#define IDM_OPEN 1001
#define IDM_EXIT 1002
#define ID_FILE_EXIT 2001
#define IDC_TRAYAPP 101
#define SERVICE_NAME L"TrayAppService"

HINSTANCE g_hInstance = NULL;
HWND g_hWnd = NULL;
NOTIFYICONDATA g_nid = {};
UINT g_uTaskbarRestart = 0;
bool g_bMainWindowVisible = false;
HANDLE g_hMutex = NULL;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void ShowContextMenu(HWND hWnd);
void ShowMainWindow(HWND hWnd);
BOOL CheckParentProcess();
BOOL EnsureServiceRunning();
void StartServiceIfNeeded();
void StopWindowsService();

DWORD GetParentProcessId()
{
    DWORD ppid = 0, pid = GetCurrentProcessId();
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32 pe = { sizeof(PROCESSENTRY32) };
        if (Process32First(hSnapshot, &pe))
            do { if (pe.th32ProcessID == pid) { ppid = pe.th32ParentProcessID; break; } } while (Process32Next(hSnapshot, &pe));
        CloseHandle(hSnapshot);
    }
    return ppid;
}

BOOL CheckParentProcess()
{
    // Also accept "--service" argument as proof of service launch
    LPWSTR* szArglist;
    int nArgs;
    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (szArglist)
    {
        for (int i = 0; i < nArgs; i++)
        {
            if (wcscmp(szArglist[i], L"--service") == 0)
            {
                LocalFree(szArglist);
                return TRUE;
            }
        }
        LocalFree(szArglist);
    }

    // Check if service is running (if not launched with --service)
    return EnsureServiceRunning();
}

BOOL EnsureServiceRunning()
{
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return FALSE;
    SC_HANDLE hSvc = OpenService(hSCM, SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (!hSvc) { CloseServiceHandle(hSCM); return FALSE; }
    SERVICE_STATUS ss; BOOL b = FALSE;
    if (QueryServiceStatus(hSvc, &ss)) b = (ss.dwCurrentState == SERVICE_RUNNING);
    CloseServiceHandle(hSvc); CloseServiceHandle(hSCM);
    return b;
}

void StartServiceIfNeeded()
{
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCM) return;
    SC_HANDLE hSvc = OpenService(hSCM, SERVICE_NAME, SERVICE_START);
    if (hSvc) { StartService(hSvc, 0, NULL); CloseServiceHandle(hSvc); }
    CloseServiceHandle(hSCM);
}

void StopWindowsService()
{
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (hSCM) {
        SC_HANDLE hSvc = OpenService(hSCM, SERVICE_NAME, SERVICE_STOP);
        if (hSvc) { SERVICE_STATUS ss = { 0 }; ControlService(hSvc, SERVICE_CONTROL_STOP, &ss); CloseServiceHandle(hSvc); }
        CloseServiceHandle(hSCM);
    }
}

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int)
{
    g_hInstance = hInstance;
    InitCommonControls();

    // Check if we should run
    if (!CheckParentProcess())
    {
        if (!EnsureServiceRunning())
        {
            StartServiceIfNeeded();
            for (int i = 0; i < 30; i++) { Sleep(1000); if (EnsureServiceRunning()) break; }
        }
        return 0;
    }

    // Create window
    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc; wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszMenuName = MAKEINTRESOURCE(IDC_TRAYAPP);
    wc.lpszClassName = _T("TrayAppClass"); wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassEx(&wc)) return 1;

    g_hWnd = CreateWindow(_T("TrayAppClass"), _T("TrayApp"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 400, 300, NULL, NULL, hInstance, NULL);
    if (!g_hWnd) return 1;

    g_uTaskbarRestart = RegisterWindowMessage(_T("TaskbarCreated"));
    AddTrayIcon(g_hWnd);
    g_bMainWindowVisible = false;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP) ShowMainWindow(hWnd);
        if (lParam == WM_RBUTTONUP) ShowContextMenu(hWnd);
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_OPEN) ShowMainWindow(hWnd);
        if (LOWORD(wParam) == IDM_EXIT || LOWORD(wParam) == ID_FILE_EXIT)
        {
            StopWindowsService(); DestroyWindow(hWnd);
        }
        break;
    case WM_CLOSE: ShowWindow(hWnd, SW_HIDE); g_bMainWindowVisible = false; return 0;
    case WM_DESTROY: RemoveTrayIcon(); PostQuitMessage(0); break;
    default:
        if (msg == g_uTaskbarRestart) AddTrayIcon(hWnd);
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void AddTrayIcon(HWND hWnd)
{
    ZeroMemory(&g_nid, sizeof(g_nid)); g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hWnd; g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    _tcscpy_s(g_nid.szTip, _T("Tray App Service"));
    Shell_NotifyIcon(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() { Shell_NotifyIcon(NIM_DELETE, &g_nid); }

void ShowContextMenu(HWND hWnd)
{
    POINT pt; GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_OPEN, _T("Open"));
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_EXIT, _T("Exit (Stop Service)"));
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    PostMessage(hWnd, WM_NULL, 0, 0); DestroyMenu(hMenu);
}

void ShowMainWindow(HWND hWnd)
{
    ShowWindow(hWnd, IsIconic(hWnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hWnd); g_bMainWindowVisible = true;
}
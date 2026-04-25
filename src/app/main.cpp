#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tchar.h>
#include <tlhelp32.h>
#include <fstream>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1
#define IDM_OPEN 1001
#define IDM_EXIT 1002
#define ID_FILE_EXIT 2001
#define IDC_TRAYAPP 101
#define SERVICE_NAME L"TrayAppService"

void LogApp(const wchar_t* msg)
{
    std::wofstream log;
    log.open(L"C:\\TrayApp.log", std::ios::app);
    if (log.is_open())
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        log << L"[" << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"] PID="
            << GetCurrentProcessId() << L" " << msg << std::endl;
        log.close();
    }
}

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
void StopWindowsService();

DWORD GetParentProcessId()
{
    DWORD ppid = 0;
    DWORD pid = GetCurrentProcessId();
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE)
    {
        PROCESSENTRY32 pe = { sizeof(PROCESSENTRY32) };
        if (Process32First(hSnapshot, &pe))
        {
            do {
                if (pe.th32ProcessID == pid) { ppid = pe.th32ParentProcessID; break; }
            } while (Process32Next(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }
    return ppid;
}

BOOL CheckParentProcess()
{
    DWORD ppid = GetParentProcessId();
    LogApp((L"Parent PID: " + std::to_wstring(ppid)).c_str());
    if (ppid == 0) return FALSE;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ppid);
    if (!hProcess) { LogApp(L"OpenProcess failed"); return FALSE; }

    TCHAR szName[MAX_PATH];
    DWORD dwSize = MAX_PATH;
    BOOL bResult = QueryFullProcessImageName(hProcess, 0, szName, &dwSize);
    CloseHandle(hProcess);

    if (!bResult) { LogApp(L"QueryFullProcessImageName failed"); return FALSE; }

    LogApp((L"Parent name: " + std::wstring(szName)).c_str());
    BOOL isService = (_tcsstr(szName, _T("TrayService.exe")) != NULL);
    LogApp((L"Is service: " + std::to_wstring(isService)).c_str());
    return isService;
}

BOOL EnsureServiceRunning()
{
    SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCManager) return FALSE;
    SC_HANDLE hService = OpenService(hSCManager, SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (!hService) { CloseServiceHandle(hSCManager); return FALSE; }
    SERVICE_STATUS ssStatus;
    BOOL bResult = FALSE;
    if (QueryServiceStatus(hService, &ssStatus))
        bResult = (ssStatus.dwCurrentState == SERVICE_RUNNING);
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    return bResult;
}

void StopWindowsService()
{
    SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (hSCM)
    {
        SC_HANDLE hSvc = OpenService(hSCM, SERVICE_NAME, SERVICE_STOP);
        if (hSvc) { SERVICE_STATUS ss = { 0 }; ControlService(hSvc, SERVICE_CONTROL_STOP, &ss); CloseServiceHandle(hSvc); }
        CloseServiceHandle(hSCM);
    }
}

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int)
{
    // ¬сегда показывать трей-иконку (дл€ теста)
    g_hInstance = hInstance;
    InitCommonControls();

    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCE(IDC_TRAYAPP);
    wc.lpszClassName = _T("TrayAppClass");
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassEx(&wc);

    g_hWnd = CreateWindow(_T("TrayAppClass"), _T("TrayApp"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 400, 300, NULL, NULL, hInstance, NULL);

    if (!g_hWnd) return 1;

    g_uTaskbarRestart = RegisterWindowMessage(_T("TaskbarCreated"));
    AddTrayIcon(g_hWnd);

    g_bMainWindowVisible = false;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

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
            StopWindowsService();
            DestroyWindow(hWnd);
        }
        break;
    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        g_bMainWindowVisible = false;
        return 0;
    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        break;
    default:
        if (msg == g_uTaskbarRestart) AddTrayIcon(hWnd);
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

void AddTrayIcon(HWND hWnd)
{
    LogApp(L"AddTrayIcon called");
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hWnd;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    _tcscpy_s(g_nid.szTip, _T("Tray App Service"));

    BOOL result = Shell_NotifyIcon(NIM_ADD, &g_nid);
    LogApp((L"Shell_NotifyIcon result: " + std::to_wstring(result)).c_str());
    if (!result) LogApp((L"Error: " + std::to_wstring(GetLastError())).c_str());
}

void RemoveTrayIcon()
{
    Shell_NotifyIcon(NIM_DELETE, &g_nid);
}

void ShowContextMenu(HWND hWnd)
{
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_OPEN, _T("Open"));
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_EXIT, _T("Exit (Stop Service)"));
    SetForegroundWindow(hWnd);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN, pt.x, pt.y, 0, hWnd, NULL);
    PostMessage(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void ShowMainWindow(HWND hWnd)
{
    ShowWindow(hWnd, IsIconic(hWnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hWnd);
    g_bMainWindowVisible = true;
}
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tchar.h>
#include <tlhelp32.h>
#include <rpc.h>
#include <rpcdce.h>
#include <rpcndr.h>
#include "resource.h"
#include <string>
#include "../common/service_rpc.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "rpcrt4.lib")

extern "C" {
    void* __RPC_USER MIDL_user_allocate(size_t size) { return malloc(size); }
    void __RPC_USER MIDL_user_free(void* p) { free(p); }
}

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

RPC_WSTR g_StringBinding = NULL;
handle_t g_hRpcBinding = NULL;

bool g_bAuthenticated = false;
bool g_bLicensed = false;
std::wstring g_UserDisplayName;
std::wstring g_LicenseExpiryDate;
long g_LicenseDaysRemaining = 0;

HWND g_hMainStatusText = NULL;
HWND g_hUserInfoText = NULL;
HWND g_hLicenseInfoText = NULL;

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void AddTrayIcon(HWND); void RemoveTrayIcon(); void ShowContextMenu(HWND); void ShowMainWindow(HWND);
BOOL CheckParentProcess(); BOOL EnsureServiceRunning(); void StartServiceIfNeeded(); void StopWindowsService();
bool InitRpcBinding(); void CleanupRpcBinding();
long RpcLogin(const std::wstring&, const std::wstring&);
long RpcLogout();
long RpcActivate(const std::wstring&);
long RpcActivateWithMac(const std::wstring&, const std::wstring&);
long RpcGetUserInfo(std::wstring&);
long RpcGetLicenseInfo(long&, std::wstring&);
void UpdateMainWindow();
void CheckInitialState();
void ShowLoginDialog(HWND); void ShowActivationDialog(HWND);
INT_PTR CALLBACK LoginDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK ActivationDlgProc(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI LicenseMonitorThread(LPVOID);

DWORD GetParentProcessId() {
    DWORD ppid = 0, pid = GetCurrentProcessId();
    HANDLE h = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (h != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32 pe = { sizeof(pe) };
        if (Process32First(h, &pe)) do { if (pe.th32ProcessID == pid) { ppid = pe.th32ParentProcessID; break; } } while (Process32Next(h, &pe));
        CloseHandle(h);
    }
    return ppid;
}

BOOL CheckParentProcess() {
    LPWSTR* a; int n;
    a = CommandLineToArgvW(GetCommandLineW(), &n);
    if (a) { for (int i = 0; i < n; i++) if (wcscmp(a[i], L"--service") == 0) { LocalFree(a); return TRUE; } LocalFree(a); }
    return EnsureServiceRunning();
}

BOOL EnsureServiceRunning() {
    SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!scm) return FALSE;
    SC_HANDLE svc = OpenService(scm, SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (!svc) { CloseServiceHandle(scm); return FALSE; }
    SERVICE_STATUS ss = { 0 };
    BOOL b = FALSE;
    if (QueryServiceStatus(svc, &ss))
        b = (ss.dwCurrentState == SERVICE_RUNNING);  // Явно проверяем состояние Running
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return b;
}

void StartServiceIfNeeded() {
    SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm) { SC_HANDLE svc = OpenService(scm, SERVICE_NAME, SERVICE_START); if (svc) { StartService(svc, 0, NULL); CloseServiceHandle(svc); } CloseServiceHandle(scm); }
}

void StopWindowsService() {
    SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm) { SC_HANDLE svc = OpenService(scm, SERVICE_NAME, SERVICE_STOP); if (svc) { SERVICE_STATUS ss = { 0 }; ControlService(svc, SERVICE_CONTROL_STOP, &ss); CloseServiceHandle(svc); } CloseServiceHandle(scm); }
}

bool InitRpcBinding() {
    RPC_STATUS s = RpcStringBindingComposeW(NULL, (RPC_WSTR)L"ncalrpc", NULL, (RPC_WSTR)L"TrayAppServiceRPC", NULL, &g_StringBinding);
    if (s != RPC_S_OK) return false;
    s = RpcBindingFromStringBindingW(g_StringBinding, &g_hRpcBinding);
    return s == RPC_S_OK;
}

void CleanupRpcBinding() { if (g_hRpcBinding) RpcBindingFree(&g_hRpcBinding); if (g_StringBinding) RpcStringFreeW(&g_StringBinding); }

long RpcLogin(const std::wstring& u, const std::wstring& p) {
    RpcTryExcept
        return Login(g_hRpcBinding, u.c_str(), p.c_str());
    RpcExcept(1)
        return RpcExceptionCode();
    RpcEndExcept
        return -1;
}

long RpcLogout() {
    RpcTryExcept
        return Logout(g_hRpcBinding);
    RpcExcept(1)
        return RpcExceptionCode();
    RpcEndExcept
        return -1;
}

long RpcActivate(const std::wstring& c) {
    RpcTryExcept
        return Activate(g_hRpcBinding, c.c_str());
    RpcExcept(1)
        return RpcExceptionCode();
    RpcEndExcept
        return -1;
}

long RpcActivateWithMac(const std::wstring& c, const std::wstring& m) {
    RpcTryExcept
        return ActivateWithMac(g_hRpcBinding, c.c_str(), m.c_str());
    RpcExcept(1)
        return RpcExceptionCode();
    RpcEndExcept
        return -1;
}

long RpcGetUserInfo(std::wstring& u) {
    wchar_t* w = NULL;
    RpcTryExcept{
        long r = GetUserInfo(g_hRpcBinding, &w);
        if (r == 0 && w) u = w;
        if (w) MIDL_user_free(w);
        return r;
    }
        RpcExcept(1) {
        if (w) MIDL_user_free(w);
        return RpcExceptionCode();
    }
    RpcEndExcept
        return -1;
}

long RpcGetLicenseInfo(long& d, std::wstring& e) {
    wchar_t* w = NULL;
    RpcTryExcept{
        long r = GetLicenseInfo(g_hRpcBinding, &d, &w);
        if (r == 0 && w) e = w;
        if (w) MIDL_user_free(w);
        return r;
    }
        RpcExcept(1) {
        if (w) MIDL_user_free(w);
        return RpcExceptionCode();
    }
    RpcEndExcept
        return -1;
}

INT_PTR CALLBACK LoginDlgProc(HWND h, UINT m, WPARAM w, LPARAM) {
    if (m == WM_COMMAND && LOWORD(w) == IDC_LOGIN_BTN) {
        wchar_t u[256], p[256];
        GetDlgItemText(h, IDC_USERNAME, u, 256); GetDlgItemText(h, IDC_PASSWORD, p, 256);
        if (RpcLogin(u, p) == 0) { g_bAuthenticated = true; g_UserDisplayName = u; EndDialog(h, IDOK); UpdateMainWindow(); }
        else { MessageBox(h, L"Authentication failed", L"Error", MB_ICONERROR); SetWindowText(GetDlgItem(h, IDC_PASSWORD), L""); }
    }
    if (m == WM_COMMAND && LOWORD(w) == IDCANCEL) EndDialog(h, IDCANCEL);
    return FALSE;
}

INT_PTR CALLBACK ActivationDlgProc(HWND h, UINT m, WPARAM w, LPARAM) {
    if (m == WM_COMMAND && LOWORD(w) == IDC_ACTIVATE_BTN) {
        wchar_t code[256];
        GetDlgItemText(h, IDC_ACTIVATION_CODE, code, 256);
        if (RpcActivate(code) == 0) {
            g_bLicensed = true;

            // ЗАПРОСИТЬ ИНФОРМАЦИЮ О ЛИЦЕНЗИИ
            long days;
            std::wstring expiry;
            if (RpcGetLicenseInfo(days, expiry) == 0) {
                g_LicenseDaysRemaining = days;
                g_LicenseExpiryDate = expiry;
            }

            EndDialog(h, IDOK);
            UpdateMainWindow();
        }
        else { MessageBox(h, L"Activation failed", L"Error", MB_ICONERROR); }
    }
    if (m == WM_COMMAND && LOWORD(w) == IDCANCEL) EndDialog(h, IDCANCEL);
    return FALSE;
}

DWORD WINAPI LicenseMonitorThread(LPVOID) {
    while (true) {
        Sleep(60000);
        if (g_bAuthenticated) {
            std::wstring u; if (RpcGetUserInfo(u) != 0) { g_bAuthenticated = false; g_bLicensed = false; PostMessage(g_hWnd, WM_USER + 100, 0, 0); continue; }
            long d; std::wstring e;
            bool was = g_bLicensed;
            g_bLicensed = (RpcGetLicenseInfo(d, e) == 0);
            if (g_bLicensed) { g_LicenseDaysRemaining = d; g_LicenseExpiryDate = e; }
            if (was != g_bLicensed) PostMessage(g_hWnd, WM_USER + 100, 0, 0);
        }
    }
    return 0;
}

void UpdateMainWindow() {
    if (g_hMainStatusText) {
        if (!g_bAuthenticated) { SetWindowText(g_hMainStatusText, L"Not authenticated"); ShowLoginDialog(g_hWnd); }
        else if (!g_bLicensed) { SetWindowText(g_hMainStatusText, L"License required"); ShowActivationDialog(g_hWnd); }
        else SetWindowText(g_hMainStatusText, L"Active");
    }
    if (g_hUserInfoText && g_bAuthenticated) { std::wstring t = L"User: " + g_UserDisplayName; SetWindowText(g_hUserInfoText, t.c_str()); }
    if (g_hLicenseInfoText && g_bLicensed) {
        std::wstring t = L"Expires: " + g_LicenseExpiryDate + L"\r\nDays: " + std::to_wstring(g_LicenseDaysRemaining);
        SetWindowText(g_hLicenseInfoText, t.c_str());
    }
}

void CheckInitialState() {
    std::wstring user;
    long result = RpcGetUserInfo(user);
    g_bAuthenticated = (result == 0 && user != L"Unknown" && !user.empty());

    if (g_bAuthenticated) {
        g_UserDisplayName = user;
        long days;
        std::wstring expiry;

        // Сначала проверяем лицензию (МОГЛА БЫТЬ активирована ранее)
        if (RpcGetLicenseInfo(days, expiry) == 0) {
            g_bLicensed = true;
            g_LicenseDaysRemaining = days;
            g_LicenseExpiryDate = expiry;
        }
        else {
            g_bLicensed = false;
        }
    }

    UpdateMainWindow();
}

void ShowLoginDialog(HWND p) { DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_LOGIN), p, LoginDlgProc); }
void ShowActivationDialog(HWND p) { DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ACTIVATION), p, ActivationDlgProc); }

LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        g_hMainStatusText = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER, 10, 10, 460, 30, h, (HMENU)IDC_STATUS_TEXT, g_hInstance, NULL);
        g_hUserInfoText = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 50, 460, 30, h, (HMENU)IDC_USER_INFO, g_hInstance, NULL);
        g_hLicenseInfoText = CreateWindow(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 10, 90, 460, 60, h, (HMENU)IDC_LICENSE_INFO, g_hInstance, NULL);

        HFONT f = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        SendMessage(g_hMainStatusText, WM_SETFONT, (WPARAM)f, TRUE);

        if (!InitRpcBinding()) { SetTimer(h, 999, 1000, NULL); }
        else { CheckInitialState(); CreateThread(NULL, 0, LicenseMonitorThread, NULL, 0, NULL); }
        break;
    }
    case WM_TIMER:
        if (w == 999) {
            if (InitRpcBinding()) {
                KillTimer(h, 999);
                CheckInitialState();
                CreateThread(NULL, 0, LicenseMonitorThread, NULL, 0, NULL);
            }
        }
        break;
    case WM_USER + 100: UpdateMainWindow(); break;
    case WM_TRAYICON:
        if (l == WM_LBUTTONUP) ShowMainWindow(h);
        if (l == WM_RBUTTONUP) ShowContextMenu(h);
        break;
    case WM_COMMAND:
        if (LOWORD(w) == IDM_OPEN) ShowMainWindow(h);
        if (LOWORD(w) == IDM_EXIT || LOWORD(w) == ID_FILE_EXIT) {
            RpcLogout(); StopWindowsService(); DestroyWindow(h);
        }
        break;
    case WM_CLOSE: ShowWindow(h, SW_HIDE); g_bMainWindowVisible = false; return 0;
    case WM_DESTROY: CleanupRpcBinding(); RemoveTrayIcon(); PostQuitMessage(0); break;
    default:
        if (m == g_uTaskbarRestart) AddTrayIcon(h);
        return DefWindowProc(h, m, w, l);
    }
    return 0;
}

int APIENTRY _tWinMain(HINSTANCE hi, HINSTANCE, LPTSTR, int) {
    g_hInstance = hi;
    InitCommonControls();

    // Проверяем, запущены ли мы от сервиса
    LPWSTR* szArglist;
    int nArgs;
    bool bFromService = false;
    szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (szArglist) {
        for (int i = 0; i < nArgs; i++) {
            if (wcscmp(szArglist[i], L"--service") == 0) {
                bFromService = true;
                break;
            }
        }
        LocalFree(szArglist);
    }

    if (!bFromService) {
        if (!EnsureServiceRunning()) {
            StartServiceIfNeeded();
            // Ждем, пока служба перейдет в состояние SERVICE_RUNNING
            for (int i = 0; i < 30; i++) {
                Sleep(1000);
                if (EnsureServiceRunning())
                    break;
            }
            // Дополнительная проверка: если после ожидания служба не запущена — сообщаем об ошибке
            if (!EnsureServiceRunning()) {
                MessageBox(NULL, L"Failed to start service", L"Error", MB_ICONERROR);
            }
        }
        return 0;
    }

    // После получения bFromService:
    //if (bFromService) {
    //    // Дополнительная проверка: родительским процессом должна быть служба
    //    DWORD parentPid = GetParentProcessId();
    //    HANDLE hParent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentPid);
    //    if (hParent) {
    //        wchar_t parentPath[MAX_PATH] = { 0 };
    //        DWORD size = MAX_PATH;
    //        if (QueryFullProcessImageNameW(hParent, 0, parentPath, &size)) {
    //            std::wstring path(parentPath);
    //            // Проверяем, что родитель — это TrayService.exe
    //            if (path.find(L"TrayService.exe") == std::wstring::npos) {
    //                // Родитель не является службой — завершаем работу
    //                CloseHandle(hParent);
    //                return 0;
    //            }
    //        }
    //        CloseHandle(hParent);
    //    }
    //    else {
    //        // Не удалось открыть родительский процесс — завершаем работу
    //        return 0;
    //    }
    //}

    if (!EnsureServiceRunning()) {
        return 0; // Служба не работает - выходим
    }


    // Запущены от сервиса — показываем UI
    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc; wc.hInstance = hi;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszMenuName = MAKEINTRESOURCE(IDC_TRAYAPP);
    wc.lpszClassName = _T("TrayAppClass"); wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassEx(&wc)) return 1;

    g_hWnd = CreateWindow(_T("TrayAppClass"), _T("TrayApp"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 500, 300, NULL, NULL, hi, NULL);
    if (!g_hWnd) return 1;

    g_uTaskbarRestart = RegisterWindowMessage(_T("TaskbarCreated"));
    AddTrayIcon(g_hWnd);
    g_bMainWindowVisible = false;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}

void AddTrayIcon(HWND h) { ZeroMemory(&g_nid, sizeof(g_nid)); g_nid.cbSize = sizeof(g_nid); g_nid.hWnd = h; g_nid.uID = ID_TRAY_ICON; g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; g_nid.uCallbackMessage = WM_TRAYICON; g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); _tcscpy_s(g_nid.szTip, _T("Tray App")); Shell_NotifyIcon(NIM_ADD, &g_nid); }
void RemoveTrayIcon() { Shell_NotifyIcon(NIM_DELETE, &g_nid); }
void ShowContextMenu(HWND h) { POINT pt; GetCursorPos(&pt); HMENU m = CreatePopupMenu(); InsertMenu(m, -1, MF_BYPOSITION | MF_STRING, IDM_OPEN, _T("Open")); InsertMenu(m, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL); InsertMenu(m, -1, MF_BYPOSITION | MF_STRING, IDM_EXIT, _T("Exit")); SetForegroundWindow(h); TrackPopupMenu(m, TPM_RIGHTALIGN, pt.x, pt.y, 0, h, NULL); PostMessage(h, WM_NULL, 0, 0); DestroyMenu(m); }
void ShowMainWindow(HWND h) { ShowWindow(h, IsIconic(h) ? SW_RESTORE : SW_SHOW); SetForegroundWindow(h); g_bMainWindowVisible = true; }
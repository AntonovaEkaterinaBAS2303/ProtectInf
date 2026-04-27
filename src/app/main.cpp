#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tchar.h>
#include <tlhelp32.h>

// RPC include должен быть после windows.h
#include <rpc.h>
#include <rpcdce.h>
#include <rpcndr.h>
#include "service_rpc.h"

#include <string>
#include <chrono>
#include <thread>
#include "resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "rpcrt4.lib")

extern "C" {
    void* __RPC_USER MIDL_user_allocate(size_t size) {
        return malloc(size);
    }

    void __RPC_USER MIDL_user_free(void* p) {
        free(p);
    }
}

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1
#define IDM_OPEN 1001
#define IDM_EXIT 1002
#define ID_FILE_EXIT 2001
#define IDC_TRAYAPP 101
#define SERVICE_NAME L"TrayAppService"

// Новые ID
#define IDD_LOGIN 300
#define IDD_ACTIVATION 301
#define IDC_USERNAME 1000
#define IDC_PASSWORD 1001
#define IDC_LOGIN_BTN 1002
#define IDC_ACTIVATION_CODE 1003
#define IDC_ACTIVATE_BTN 1004
#define IDC_STATUS_TEXT 1005
#define IDC_LICENSE_INFO 1006
#define IDC_USER_INFO 1007

HINSTANCE g_hInstance = NULL;
HWND g_hWnd = NULL;
NOTIFYICONDATA g_nid = {};
UINT g_uTaskbarRestart = 0;
bool g_bMainWindowVisible = false;
HANDLE g_hMutex = NULL;

// RPC binding
RPC_WSTR g_StringBinding = NULL;
handle_t g_hRpcBinding = NULL;

// State tracking
bool g_bAuthenticated = false;
bool g_bLicensed = false;
std::wstring g_UserDisplayName;
std::wstring g_LicenseExpiryDate;
long g_LicenseDaysRemaining = 0;

// Window handles
HWND g_hMainStatusText = NULL;
HWND g_hUserInfoText = NULL;
HWND g_hLicenseInfoText = NULL;

// Forward declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void ShowContextMenu(HWND hWnd);
void ShowMainWindow(HWND hWnd);
BOOL CheckParentProcess();
BOOL EnsureServiceRunning();
void StartServiceIfNeeded();
void StopWindowsService();
bool InitRpcBinding();
void CleanupRpcBinding();
long RpcLogin(const std::wstring& user, const std::wstring& pass);
long RpcLogout();
long RpcActivate(const std::wstring& code);
long RpcGetUserInfo(std::wstring& user);
long RpcGetLicenseInfo(long& days, std::wstring& expiry);
void UpdateMainWindow();
void ShowLoginDialog(HWND parent);
void ShowActivationDialog(HWND parent);
INT_PTR CALLBACK LoginDlgProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK ActivationDlgProc(HWND, UINT, WPARAM, LPARAM);
DWORD WINAPI LicenseMonitorThread(LPVOID lp);

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

// RPC Helper Functions
bool InitRpcBinding() {
    RPC_STATUS status = RpcStringBindingComposeW(
        NULL,
        (RPC_WSTR)L"ncalrpc",
        NULL,
        (RPC_WSTR)L"TrayAppServiceRPC",
        NULL,
        &g_StringBinding
    );

    if (status != RPC_S_OK) return false;

    status = RpcBindingFromStringBindingW(g_StringBinding, &g_hRpcBinding);
    return status == RPC_S_OK;
}

void CleanupRpcBinding() {
    if (g_hRpcBinding) {
        RpcBindingFree(&g_hRpcBinding);
    }
    if (g_StringBinding) {
        RpcStringFreeW(&g_StringBinding);
    }
}

long RpcLogin(const std::wstring& user, const std::wstring& pass) {
    RpcTryExcept{
        return Login(g_hRpcBinding, user.c_str(), pass.c_str());
    } RpcExcept(1) {
        return RpcExceptionCode();
    } RpcEndExcept
        return -1;
}

long RpcLogout() {
    RpcTryExcept{
        return Logout(g_hRpcBinding);
    } RpcExcept(1) {
        return RpcExceptionCode();
    } RpcEndExcept
        return -1;
}

long RpcActivate(const std::wstring& code) {
    RpcTryExcept{
        return Activate(g_hRpcBinding, code.c_str());
    } RpcExcept(1) {
        return RpcExceptionCode();
    } RpcEndExcept
        return -1;
}

long RpcGetUserInfo(std::wstring& user) {
    wchar_t* wUser = NULL;
    RpcTryExcept{
        long result = GetUserInfo(g_hRpcBinding, &wUser);
        if (result == 0 && wUser) {
            user = wUser;
        }
        if (wUser) MIDL_user_free(wUser);
        return result;
    } RpcExcept(1) {
        if (wUser) MIDL_user_free(wUser);
        return RpcExceptionCode();
    } RpcEndExcept
        return -1;
}

long RpcGetLicenseInfo(long& days, std::wstring& expiry) {
    wchar_t* wExpiry = NULL;
    RpcTryExcept{
        long result = GetLicenseInfo(g_hRpcBinding, &days, &wExpiry);
        if (result == 0 && wExpiry) {
            expiry = wExpiry;
        }
        if (wExpiry) MIDL_user_free(wExpiry);
        return result;
    } RpcExcept(1) {
        if (wExpiry) MIDL_user_free(wExpiry);
        return RpcExceptionCode();
    } RpcEndExcept
        return -1;
}

// Dialog Procedures
INT_PTR CALLBACK LoginDlgProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_LOGIN_BTN: {
            wchar_t user[256], pass[256];
            GetDlgItemText(hwndDlg, IDC_USERNAME, user, 256);
            GetDlgItemText(hwndDlg, IDC_PASSWORD, pass, 256);

            long result = RpcLogin(user, pass);
            if (result == 0) {
                g_bAuthenticated = true;
                g_UserDisplayName = user;
                EndDialog(hwndDlg, IDOK);
                UpdateMainWindow();
            }
            else {
                MessageBox(hwndDlg, L"Authentication failed. Please try again.",
                    L"Error", MB_ICONERROR);
                SetWindowText(GetDlgItem(hwndDlg, IDC_PASSWORD), L"");
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK ActivationDlgProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_INITDIALOG:
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_ACTIVATE_BTN: {
            wchar_t code[256];
            GetDlgItemText(hwndDlg, IDC_ACTIVATION_CODE, code, 256);

            long result = RpcActivate(code);
            if (result == 0) {
                g_bLicensed = true;
                EndDialog(hwndDlg, IDOK);
                UpdateMainWindow();
            }
            else {
                MessageBox(hwndDlg, L"Activation failed. Please check your code and try again.",
                    L"Error", MB_ICONERROR);
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// License monitor thread
DWORD WINAPI LicenseMonitorThread(LPVOID lp) {
    (void)lp;

    while (true) {
        Sleep(60000);

        if (g_bAuthenticated) {
            std::wstring user;
            long result = RpcGetUserInfo(user);
            if (result != 0) {
                g_bAuthenticated = false;
                g_bLicensed = false;
                PostMessage(g_hWnd, WM_USER + 100, 0, 0);
                continue;
            }

            long days;
            std::wstring expiry;
            result = RpcGetLicenseInfo(days, expiry);

            bool wasLicensed = g_bLicensed;
            g_bLicensed = (result == 0);
            g_LicenseDaysRemaining = days;
            g_LicenseExpiryDate = expiry;

            if (wasLicensed != g_bLicensed) {
                PostMessage(g_hWnd, WM_USER + 100, 0, 0);
            }
        }
    }
    return 0;
}

void UpdateMainWindow() {
    if (g_hMainStatusText) {
        if (!g_bAuthenticated) {
            SetWindowText(g_hMainStatusText, L"Not authenticated");
            ShowLoginDialog(g_hWnd);
        }
        else if (!g_bLicensed) {
            SetWindowText(g_hMainStatusText, L"License required");
            ShowActivationDialog(g_hWnd);
        }
        else {
            SetWindowText(g_hMainStatusText, L"Active");
        }
    }

    if (g_hUserInfoText && g_bAuthenticated) {
        std::wstring text = L"User: " + g_UserDisplayName;
        SetWindowText(g_hUserInfoText, text.c_str());
    }

    if (g_hLicenseInfoText && g_bLicensed) {
        std::wstring text = L"License expires: " + g_LicenseExpiryDate +
            L"\r\nDays remaining: " + std::to_wstring(g_LicenseDaysRemaining);
        SetWindowText(g_hLicenseInfoText, text.c_str());
    }
}

void ShowLoginDialog(HWND parent) {
    DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_LOGIN), parent, LoginDlgProc);
}

void ShowActivationDialog(HWND parent) {
    DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ACTIVATION), parent, ActivationDlgProc);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg)
    {
    case WM_CREATE: {
        g_hMainStatusText = CreateWindow(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            10, 10, 360, 30, hWnd, (HMENU)IDC_STATUS_TEXT, g_hInstance, NULL);

        g_hUserInfoText = CreateWindow(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 50, 360, 30, hWnd, (HMENU)IDC_USER_INFO, g_hInstance, NULL);

        g_hLicenseInfoText = CreateWindow(L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            10, 90, 360, 60, hWnd, (HMENU)IDC_LICENSE_INFO, g_hInstance, NULL);

        HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
        SendMessage(g_hMainStatusText, WM_SETFONT, (WPARAM)hFont, TRUE);

        if (!InitRpcBinding()) {
            MessageBox(hWnd, L"Failed to connect to service", L"Error", MB_ICONERROR);
        }

        CreateThread(NULL, 0, LicenseMonitorThread, NULL, 0, NULL);

        std::wstring user;
        long result = RpcGetUserInfo(user);
        g_bAuthenticated = (result == 0 && user != L"Unknown" && !user.empty());

        if (g_bAuthenticated) {
            g_UserDisplayName = user;
            long days;
            std::wstring expiry;
            result = RpcGetLicenseInfo(days, expiry);
            g_bLicensed = (result == 0);
            g_LicenseDaysRemaining = days;
            g_LicenseExpiryDate = expiry;
        }

        UpdateMainWindow();
        break;
    }

    case WM_USER + 100:
        UpdateMainWindow();
        break;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP) ShowMainWindow(hWnd);
        if (lParam == WM_RBUTTONUP) ShowContextMenu(hWnd);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_OPEN) ShowMainWindow(hWnd);
        if (LOWORD(wParam) == IDM_EXIT || LOWORD(wParam) == ID_FILE_EXIT)
        {
            RpcLogout();
            StopWindowsService();
            DestroyWindow(hWnd);
        }
        break;

    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        g_bMainWindowVisible = false;
        return 0;

    case WM_DESTROY:
        CleanupRpcBinding();
        RemoveTrayIcon();
        PostQuitMessage(0);
        break;

    default:
        if (msg == g_uTaskbarRestart) AddTrayIcon(hWnd);
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

int APIENTRY _tWinMain(HINSTANCE hInstance, HINSTANCE, LPTSTR, int)
{
    g_hInstance = hInstance;
    InitCommonControls();

    if (!CheckParentProcess())
    {
        if (!EnsureServiceRunning())
        {
            StartServiceIfNeeded();
            for (int i = 0; i < 30; i++) { Sleep(1000); if (EnsureServiceRunning()) break; }
        }
        return 0;
    }

    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc; wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszMenuName = MAKEINTRESOURCE(IDC_TRAYAPP);
    wc.lpszClassName = _T("TrayAppClass"); wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassEx(&wc)) return 1;

    g_hWnd = CreateWindow(_T("TrayAppClass"), _T("TrayApp"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 500, 300, NULL, NULL, hInstance, NULL);
    if (!g_hWnd) return 1;

    g_uTaskbarRestart = RegisterWindowMessage(_T("TaskbarCreated"));
    AddTrayIcon(g_hWnd);
    g_bMainWindowVisible = false;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
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
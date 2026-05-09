#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <tchar.h>
#include <tlhelp32.h>
#include <string>
#include <rpc.h>
#include "../common/service_rpc.h"

#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")

#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1
#define IDM_OPEN 1001
#define IDM_EXIT 1002
#define ID_FILE_EXIT 2001
#define IDC_TRAYAPP 101
#define SERVICE_NAME L"TrayAppService"

// UI Controls
#define IDC_USERNAME_LABEL 1003
#define IDC_USERNAME_EDIT 1004
#define IDC_PASSWORD_LABEL 1005
#define IDC_PASSWORD_EDIT 1006
#define IDC_LOGIN_BUTTON 1007
#define IDC_LOGOUT_BUTTON 1008
#define IDC_LICENSE_STATUS 1009
#define IDC_ACTIVATION_KEY_EDIT 1010
#define IDC_ACTIVATE_BUTTON 1011
#define IDC_STATUS_TEXT 1012

#define WM_UPDATE_UI (WM_APP + 2)
#define WM_LICENSE_CHANGED (WM_APP + 3)

HINSTANCE g_hInstance = NULL;
HWND g_hWnd = NULL;
HWND g_hUsernameEdit = NULL;
HWND g_hPasswordEdit = NULL;
HWND g_hLoginButton = NULL;
HWND g_hLogoutButton = NULL;
HWND g_hLicenseStatus = NULL;
HWND g_hActivationKeyEdit = NULL;
HWND g_hActivateButton = NULL;
HWND g_hStatusText = NULL;
NOTIFYICONDATA g_nid = {};
UINT g_uTaskbarRestart = 0;
bool g_bMainWindowVisible = false;

RPC_WSTR g_StringBinding = NULL;
handle_t g_hRpcBinding = NULL;

// State
bool g_bAuthenticated = false;
bool g_bLicensed = false;
wchar_t g_Username[256] = { 0 };
long g_DaysRemaining = 0;
wchar_t g_ExpiryDate[64] = { 0 };

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void ShowContextMenu(HWND hWnd);
void ShowMainWindow(HWND hWnd);
BOOL CheckParentProcess();
BOOL EnsureServiceRunning();
void StartServiceIfNeeded();
void StopWindowsService();
void CreateUIControls(HWND hWnd);
void UpdateUIState();
void OnLogin();
void OnLogout();
void OnActivate();

// RPC wrapper functions that don't have C++ objects with destructors
static long RpcLoginSafe(handle_t binding, const wchar_t* username, const wchar_t* password)
{
    long result = 0;
    RpcTryExcept
        result = Login(binding, username, password);
    RpcExcept(1)
        result = 0;
    RpcEndExcept
        return result;
}

static long RpcGetLicenseInfoSafe(handle_t binding, long* daysRemaining, wchar_t** expiryDate)
{
    long result = 0;
    RpcTryExcept
        result = GetLicenseInfo(binding, daysRemaining, expiryDate);
    RpcExcept(1)
        result = 1;
    if (daysRemaining) *daysRemaining = 0;
    if (expiryDate) {
        *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * 16);
        if (*expiryDate) wcscpy_s(*expiryDate, 16, L"Error");
    }
    RpcEndExcept
        return result;
}

static long RpcGetUserInfoSafe(handle_t binding, wchar_t** username)
{
    long result = 0;
    RpcTryExcept
        result = GetUserInfo(binding, username);
    RpcExcept(1)
        result = 0;
    RpcEndExcept
        return result;
}

static void RpcLogoutSafe(handle_t binding)
{
    RpcTryExcept
        Logout(binding);
    RpcExcept(1)
        RpcEndExcept
}

static long RpcActivateProductSafe(handle_t binding, const wchar_t* activationKey)
{
    long result = 0;
    RpcTryExcept
        result = ActivateProduct(binding, activationKey);
    RpcExcept(1)
        result = 0;
    RpcEndExcept
        return result;
}

static void RpcStopServiceSafe(handle_t binding)
{
    RpcTryExcept
        StopService(binding);
    RpcExcept(1)
    {
        SC_HANDLE hSCM = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
        if (hSCM) {
            SC_HANDLE hSvc = OpenService(hSCM, SERVICE_NAME, SERVICE_STOP);
            if (hSvc) {
                SERVICE_STATUS ss = { 0 };
                ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
                CloseServiceHandle(hSvc);
            }
            CloseServiceHandle(hSCM);
        }
    }
    RpcEndExcept
}

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
        b = (ss.dwCurrentState == SERVICE_RUNNING);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return b;
}

void StartServiceIfNeeded() {
    SC_HANDLE scm = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE svc = OpenService(scm, SERVICE_NAME, SERVICE_START);
        if (svc) {
            StartService(svc, 0, NULL);
            CloseServiceHandle(svc);
        }
        CloseServiceHandle(scm);
    }
}

bool InitRpcBinding() {
    RPC_STATUS s = RpcStringBindingComposeW(NULL, (RPC_WSTR)L"ncalrpc", NULL, (RPC_WSTR)L"TrayAppServiceRPC", NULL, &g_StringBinding);
    if (s != RPC_S_OK) return false;
    s = RpcBindingFromStringBindingW(g_StringBinding, &g_hRpcBinding);
    return s == RPC_S_OK;
}

void CleanupRpcBinding() {
    if (g_hRpcBinding) RpcBindingFree(&g_hRpcBinding);
    if (g_StringBinding) RpcStringFreeW(&g_StringBinding);
}

void StopWindowsService()
{
    if (!g_hRpcBinding) InitRpcBinding();
    if (g_hRpcBinding)
    {
        RpcStopServiceSafe(g_hRpcBinding);
    }
}

void CreateUIControls(HWND hWnd) {
    CreateWindowW(L"STATIC", L"Username:", WS_VISIBLE | WS_CHILD,
        10, 10, 70, 25, hWnd, (HMENU)IDC_USERNAME_LABEL, g_hInstance, NULL);

    g_hUsernameEdit = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER,
        90, 10, 150, 25, hWnd, (HMENU)IDC_USERNAME_EDIT, g_hInstance, NULL);

    CreateWindowW(L"STATIC", L"Password:", WS_VISIBLE | WS_CHILD,
        10, 40, 70, 25, hWnd, (HMENU)IDC_PASSWORD_LABEL, g_hInstance, NULL);

    g_hPasswordEdit = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_PASSWORD,
        90, 40, 150, 25, hWnd, (HMENU)IDC_PASSWORD_EDIT, g_hInstance, NULL);

    g_hLoginButton = CreateWindowW(L"BUTTON", L"Login", WS_VISIBLE | WS_CHILD,
        10, 75, 80, 30, hWnd, (HMENU)IDC_LOGIN_BUTTON, g_hInstance, NULL);

    g_hLogoutButton = CreateWindowW(L"BUTTON", L"Logout", WS_VISIBLE | WS_CHILD,
        100, 75, 80, 30, hWnd, (HMENU)IDC_LOGOUT_BUTTON, g_hInstance, NULL);

    g_hLicenseStatus = CreateWindowW(L"STATIC", L"License Status: Not licensed",
        WS_VISIBLE | WS_CHILD, 10, 120, 350, 25, hWnd, (HMENU)IDC_LICENSE_STATUS, g_hInstance, NULL);

    g_hActivationKeyEdit = CreateWindowW(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER,
        10, 155, 150, 25, hWnd, (HMENU)IDC_ACTIVATION_KEY_EDIT, g_hInstance, NULL);

    g_hActivateButton = CreateWindowW(L"BUTTON", L"Activate", WS_VISIBLE | WS_CHILD,
        170, 155, 80, 30, hWnd, (HMENU)IDC_ACTIVATE_BUTTON, g_hInstance, NULL);

    g_hStatusText = CreateWindowW(L"STATIC", L"",
        WS_VISIBLE | WS_CHILD, 10, 200, 350, 50, hWnd, (HMENU)IDC_STATUS_TEXT, g_hInstance, NULL);
}

void UpdateUIState() {
    if (!g_bAuthenticated) {
        EnableWindow(g_hUsernameEdit, TRUE);
        EnableWindow(g_hPasswordEdit, TRUE);
        EnableWindow(g_hLoginButton, TRUE);
        EnableWindow(g_hLogoutButton, FALSE);
        EnableWindow(g_hActivationKeyEdit, FALSE);
        EnableWindow(g_hActivateButton, FALSE);
        SetWindowTextW(g_hLicenseStatus, L"License Status: Login required");
        SetWindowTextW(g_hStatusText, L"");
    }
    else {
        EnableWindow(g_hUsernameEdit, FALSE);
        EnableWindow(g_hPasswordEdit, FALSE);
        EnableWindow(g_hLoginButton, FALSE);
        EnableWindow(g_hLogoutButton, TRUE);

        wchar_t statusText[512];
        wsprintfW(statusText, L"Logged in as: %s", g_Username);
        SetWindowTextW(g_hStatusText, statusText);

        if (!g_bLicensed) {
            EnableWindow(g_hActivationKeyEdit, TRUE);
            EnableWindow(g_hActivateButton, TRUE);
            SetWindowTextW(g_hLicenseStatus, L"License Status: Not licensed");
        }
        else {
            EnableWindow(g_hActivationKeyEdit, FALSE);
            EnableWindow(g_hActivateButton, FALSE);

            // Показываем дату истечения лицензии
            wchar_t licenseText[256];
            wsprintfW(licenseText, L"License Status: Active (Expires: %s, %d days left)",
                g_ExpiryDate, g_DaysRemaining);
            SetWindowTextW(g_hLicenseStatus, licenseText);
        }
    }
}

void OnLogin() {
    wchar_t username[256];
    wchar_t password[256];
    GetWindowTextW(g_hUsernameEdit, username, 256);
    GetWindowTextW(g_hPasswordEdit, password, 256);

    if (!g_hRpcBinding) InitRpcBinding();
    if (!g_hRpcBinding) {
        MessageBoxW(g_hWnd, L"Failed to connect to service", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    long result = RpcLoginSafe(g_hRpcBinding, username, password);

    if (result == 0) {
        g_bAuthenticated = true;
        wcscpy_s(g_Username, username);

        // Проверяем статус лицензии
        long daysRemaining = 0;
        wchar_t* expiryDateStr = NULL;
        long licResult = RpcGetLicenseInfoSafe(g_hRpcBinding, &daysRemaining, &expiryDateStr);

        if (licResult == 0 && expiryDateStr) {
            g_bLicensed = true;
            g_DaysRemaining = daysRemaining;
            wcscpy_s(g_ExpiryDate, expiryDateStr);
            MIDL_user_free(expiryDateStr);
        }
        else {
            g_bLicensed = false;
            g_DaysRemaining = 0;
            g_ExpiryDate[0] = L'\0';
            if (expiryDateStr) MIDL_user_free(expiryDateStr);
        }

        UpdateUIState();
    }
    else {
        MessageBoxW(g_hWnd, L"Login failed. Please check your credentials.",
            L"Authentication Error", MB_OK | MB_ICONERROR);
    }
}

void OnLogout() {
    if (!g_hRpcBinding) InitRpcBinding();
    if (!g_hRpcBinding) return;

    RpcLogoutSafe(g_hRpcBinding);

    g_bAuthenticated = false;
    g_bLicensed = false;
    g_Username[0] = L'\0';
    g_DaysRemaining = 0;
    g_ExpiryDate[0] = L'\0';
    SetWindowTextW(g_hUsernameEdit, L"");
    SetWindowTextW(g_hPasswordEdit, L"");
    UpdateUIState();
}

void OnActivate() {
    wchar_t activationKey[256];
    GetWindowTextW(g_hActivationKeyEdit, activationKey, 256);

    if (!g_hRpcBinding) InitRpcBinding();
    if (!g_hRpcBinding) return;

    long result = RpcActivateProductSafe(g_hRpcBinding, activationKey);

    if (result == 0) {
        // После активации проверяем статус лицензии
        long daysRemaining = 0;
        wchar_t* expiryDateStr = NULL;
        long licResult = RpcGetLicenseInfoSafe(g_hRpcBinding, &daysRemaining, &expiryDateStr);

        if (licResult == 0 && expiryDateStr) {
            g_bLicensed = true;
            g_DaysRemaining = daysRemaining;
            wcscpy_s(g_ExpiryDate, expiryDateStr);
            MIDL_user_free(expiryDateStr);
            UpdateUIState();
            MessageBoxW(g_hWnd, L"Product activated successfully!", L"Success", MB_OK | MB_ICONINFORMATION);
        }
        else {
            g_bLicensed = false;
            if (expiryDateStr) MIDL_user_free(expiryDateStr);
            MessageBoxW(g_hWnd, L"Activation succeeded but license check failed.",
                L"Warning", MB_OK | MB_ICONWARNING);
        }
    }
    else {
        MessageBoxW(g_hWnd, L"Activation failed. Please check your activation key.",
            L"Activation Error", MB_OK | MB_ICONERROR);
    }
}

// Forward declaration of WndProc before _tWinMain
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY _tWinMain(HINSTANCE hi, HINSTANCE, LPTSTR, int)
{
    g_hInstance = hi;
    InitCommonControls();

    HANDLE hMutex = CreateMutexW(NULL, FALSE, L"Global\\TrayApp_Session_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hMutex);
        return 0;
    }

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
            for (int i = 0; i < 30; i++) {
                Sleep(1000);
                if (EnsureServiceRunning()) break;
            }
        }
        return 0;
    }

    bool parentCheckPassed = false;
    DWORD parentPid = GetParentProcessId();
    HANDLE hParent = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, parentPid);
    if (hParent) {
        wchar_t parentPath[MAX_PATH] = { 0 };
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hParent, 0, parentPath, &size)) {
            if (wcsstr(parentPath, L"TrayService.exe") != NULL) {
                parentCheckPassed = true;
            }
        }
        CloseHandle(hParent);
    }

    if (!parentCheckPassed) {
        if (!EnsureServiceRunning()) {
            return 0;
        }
    }

    WNDCLASSEX wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc; wc.hInstance = hi;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.lpszMenuName = MAKEINTRESOURCE(IDC_TRAYAPP);
    wc.lpszClassName = _T("TrayAppClass"); wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    if (!RegisterClassEx(&wc)) return 1;

    g_hWnd = CreateWindow(_T("TrayAppClass"), _T("TrayApp - Antivirus"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, 450, 350, NULL, NULL, hi, NULL);
    if (!g_hWnd) return 1;

    CreateUIControls(g_hWnd);

    // Проверяем начальное состояние аутентификации
    if (InitRpcBinding()) {
        wchar_t* username = NULL;
        long result = RpcGetUserInfoSafe(g_hRpcBinding, &username);

        if (result == 0 && username && wcscmp(username, L"Unknown") != 0) {
            g_bAuthenticated = true;
            wcscpy_s(g_Username, username);

            // Проверяем лицензию
            long daysRemaining = 0;
            wchar_t* expiryDateStr = NULL;
            long licResult = RpcGetLicenseInfoSafe(g_hRpcBinding, &daysRemaining, &expiryDateStr);

            if (licResult == 0 && expiryDateStr) {
                g_bLicensed = true;
                g_DaysRemaining = daysRemaining;
                wcscpy_s(g_ExpiryDate, expiryDateStr);
            }
            if (expiryDateStr) MIDL_user_free(expiryDateStr);
        }
        if (username) MIDL_user_free(username);
    }

    UpdateUIState();

    ShowWindow(g_hWnd, SW_HIDE);
    UpdateWindow(g_hWnd);

    g_uTaskbarRestart = RegisterWindowMessage(_T("TaskbarCreated"));
    AddTrayIcon(g_hWnd);
    g_bMainWindowVisible = false;

    // Запускаем таймер для периодической проверки лицензии
    SetTimer(g_hWnd, 1, 30000, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }

    KillTimer(g_hWnd, 1);
    CleanupRpcBinding();
    return 0;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TIMER:
        if (wParam == 1 && g_bAuthenticated) {
            long daysRemaining = 0;
            wchar_t* expiryDateStr = NULL;
            long result = RpcGetLicenseInfoSafe(g_hRpcBinding, &daysRemaining, &expiryDateStr);

            if (result == 0) {
                if (!g_bLicensed || daysRemaining != g_DaysRemaining) {
                    g_bLicensed = true;
                    g_DaysRemaining = daysRemaining;
                    if (expiryDateStr) {
                        wcscpy_s(g_ExpiryDate, expiryDateStr);
                    }
                    UpdateUIState();
                }
            }
            else {
                if (g_bLicensed) {
                    g_bLicensed = false;
                    g_DaysRemaining = 0;
                    g_ExpiryDate[0] = L'\0';
                    UpdateUIState();
                }
            }
            if (expiryDateStr) MIDL_user_free(expiryDateStr);
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_OPEN) ShowMainWindow(hWnd);
        if (LOWORD(wParam) == IDC_LOGIN_BUTTON) OnLogin();
        if (LOWORD(wParam) == IDC_LOGOUT_BUTTON) OnLogout();
        if (LOWORD(wParam) == IDC_ACTIVATE_BUTTON) OnActivate();

        if (LOWORD(wParam) == IDM_EXIT || LOWORD(wParam) == ID_FILE_EXIT)
        {
            int result = MessageBoxW(
                NULL,
                L"Do you want to stop the TrayApp Service?\n\n"
                L"This will close all TrayApp applications in all sessions.",
                L"TrayApp - Stop Service",
                MB_SERVICE_NOTIFICATION | MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2
            );

            if (result == IDYES) {
                StopWindowsService();
                DestroyWindow(hWnd);
            }
        }
        break;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP) ShowMainWindow(hWnd);
        if (lParam == WM_RBUTTONUP) ShowContextMenu(hWnd);
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
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(NOTIFYICONDATA);
    g_nid.hWnd = hWnd;
    g_nid.uID = ID_TRAY_ICON;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
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
    PostMessage(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void ShowMainWindow(HWND hWnd)
{
    ShowWindow(hWnd, IsIconic(hWnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hWnd);
    g_bMainWindowVisible = true;
}
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <rpc.h>
#include <tchar.h>
#include <string>
#include <tlhelp32.h>

#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "advapi32.lib")

// RPC заглушки (обычно генерируются MIDL, но для простоты объявим вручную)
extern "C" {
    void StopService();
    int CheckServiceStatus();
}

// Resource identifiers
#define IDC_TRAYAPP 101
#define IDI_TRAYAPP 102

// Constants
#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1
#define IDM_OPEN 1001
#define IDM_EXIT 1002
#define ID_FILE_EXIT 2001

#define SERVICE_NAME L"TrayAppService"

// Global variables
HINSTANCE g_hInstance = NULL;
HWND g_hWnd = NULL;
NOTIFYICONDATA g_nid = {};
bool g_bMainWindowVisible = false;
HANDLE g_hMutex = NULL;
UINT g_uTaskbarRestart = 0;
RPC_WSTR g_szStringBinding = NULL;
handle_t g_hRpcBinding = NULL;

// Function declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
BOOL InitInstance(HINSTANCE, int);
void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void ShowContextMenu(HWND hWnd);
void ShowMainWindow(HWND hWnd);
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL CheckParentProcess();
BOOL EnsureServiceRunning();
void StopWindowsService();
BOOL InitRpcClient();

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
            do
            {
                if (pe.th32ProcessID == pid)
                {
                    ppid = pe.th32ParentProcessID;
                    break;
                }
            } while (Process32Next(hSnapshot, &pe));
        }
        CloseHandle(hSnapshot);
    }

    return ppid;
}

BOOL CheckParentProcess()
{
    DWORD ppid = GetParentProcessId();
    if (ppid == 0) return FALSE;

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ppid);
    if (!hProcess) return FALSE;

    WCHAR szName[MAX_PATH];
    DWORD dwSize = MAX_PATH;
    BOOL bResult = QueryFullProcessImageName(hProcess, 0, szName, &dwSize);
    CloseHandle(hProcess);

    if (!bResult) return FALSE;

    // Проверить, что родительский процесс - наша служба
    return (_tcsstr(szName, _T("TrayService.exe")) != NULL);
}

BOOL EnsureServiceRunning()
{
    SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCManager) return FALSE;

    SC_HANDLE hService = OpenService(hSCManager, SERVICE_NAME, SERVICE_QUERY_STATUS | SERVICE_START);
    if (!hService)
    {
        CloseServiceHandle(hSCManager);
        return FALSE;
    }

    SERVICE_STATUS ssStatus;
    if (!QueryServiceStatus(hService, &ssStatus))
    {
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCManager);
        return FALSE;
    }

    if (ssStatus.dwCurrentState == SERVICE_STOPPED)
    {
        // Запустить службу
        StartService(hService, 0, NULL);

        // Ждать запуска
        for (int i = 0; i < 30; i++)
        {
            Sleep(1000);
            if (QueryServiceStatus(hService, &ssStatus) &&
                ssStatus.dwCurrentState == SERVICE_RUNNING)
            {
                CloseServiceHandle(hService);
                CloseServiceHandle(hSCManager);
                return TRUE;
            }
        }
    }
    else if (ssStatus.dwCurrentState == SERVICE_RUNNING)
    {
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCManager);
        return TRUE;
    }

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    return FALSE;
}

void StopWindowsService()
{
    SC_HANDLE hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_CONNECT);
    if (!hSCManager) return;

    SC_HANDLE hService = OpenService(hSCManager, SERVICE_NAME, SERVICE_STOP | SERVICE_QUERY_STATUS);
    if (!hService)
    {
        CloseServiceHandle(hSCManager);
        return;
    }

    SERVICE_STATUS ssStatus;
    ControlService(hService, SERVICE_CONTROL_STOP, &ssStatus);

    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
}

BOOL InitRpcClient()
{
    RPC_STATUS status;

    // Создать строку привязки
    status = RpcStringBindingCompose(
        NULL,
        (RPC_WSTR)L"ncalrpc",
        NULL,
        (RPC_WSTR)L"TrayAppServiceRPC",
        NULL,
        &g_szStringBinding
    );

    if (status != RPC_S_OK) return FALSE;

    // Создать привязку
    status = RpcBindingFromStringBinding(
        g_szStringBinding,
        &g_hRpcBinding
    );

    return (status == RPC_S_OK);
}

int APIENTRY _tWinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPTSTR lpCmdLine,
    int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Проверить, что приложение запущено службой
    if (!CheckParentProcess())
    {
        // Проверить состояние службы
        if (!EnsureServiceRunning())
        {
            MessageBox(NULL, _T("Failed to start service"), _T("Error"), MB_OK | MB_ICONERROR);
        }
        return 0; // Завершить работу
    }

    // Инициализация RPC клиента
    InitRpcClient();

    // Проверка на повторный запуск
    g_hMutex = CreateMutex(NULL, FALSE, _T("Global\\TrayApp_SingleInstance_Mutex"));
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(g_hMutex);
        return 0;
    }

    InitCommonControls();
    g_hInstance = hInstance;
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, SW_HIDE))
    {
        CloseHandle(g_hMutex);
        return FALSE;
    }

    g_uTaskbarRestart = RegisterWindowMessage(_T("TaskbarCreated"));
    AddTrayIcon(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Очистка RPC
    if (g_hRpcBinding)
    {
        RpcBindingFree(&g_hRpcBinding);
    }
    if (g_szStringBinding)
    {
        RpcStringFree(&g_szStringBinding);
    }

    CloseHandle(g_hMutex);
    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCE(IDC_TRAYAPP);
    wcex.lpszClassName = _T("TrayAppClass");
    wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

    return RegisterClassEx(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    g_hWnd = CreateWindow(
        _T("TrayAppClass"),
        _T("Tray Application"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hWnd) return FALSE;

    if (nCmdShow == SW_SHOW)
    {
        ShowWindow(g_hWnd, nCmdShow);
        UpdateWindow(g_hWnd);
        g_bMainWindowVisible = true;
    }

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_TRAYICON:
        switch (lParam)
        {
        case WM_LBUTTONUP:
            ShowMainWindow(hWnd);
            break;
        case WM_RBUTTONUP:
            ShowContextMenu(hWnd);
            break;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_OPEN:
            ShowMainWindow(hWnd);
            break;
        case IDM_EXIT:
        case ID_FILE_EXIT:
            StopWindowsService();
            DestroyWindow(hWnd);
            break;
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
        if (message == g_uTaskbarRestart)
        {
            AddTrayIcon(hWnd);
        }
        return DefWindowProc(hWnd, message, wParam, lParam);
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
    g_nid.hIcon = LoadIcon(g_hInstance, IDI_APPLICATION);
    _tcscpy_s(g_nid.szTip, _T("Tray App Service"));
    Shell_NotifyIcon(NIM_ADD, &g_nid);
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
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hWnd, NULL);
    PostMessage(hWnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void ShowMainWindow(HWND hWnd)
{
    ShowWindow(hWnd, IsIconic(hWnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hWnd);
    g_bMainWindowVisible = true;
}
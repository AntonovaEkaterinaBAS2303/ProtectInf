#include <windows.h>
#include <shellapi.h>
#include <tchar.h>
#include <string>
#include "resource.h"

// Constants
#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_ICON 1
#define IDM_OPEN 1001
#define IDM_EXIT 1002
#define ID_FILE_EXIT 2001

// Global variables
HINSTANCE g_hInstance = NULL;
HWND g_hWnd = NULL;
NOTIFYICONDATA g_nid = {};
bool g_bMainWindowVisible = false;
HANDLE g_hMutex = NULL;
UINT g_uTaskbarRestart = 0;

// Mutex name for single instance check
const TCHAR* MUTEX_NAME = _T("Global\\TrayApp_SingleInstance_Mutex");

// Function declarations
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
BOOL InitInstance(HINSTANCE, int);
void AddTrayIcon(HWND hWnd);
void RemoveTrayIcon();
void ShowContextMenu(HWND hWnd);
void ShowMainWindow(HWND hWnd);
ATOM MyRegisterClass(HINSTANCE hInstance);

int APIENTRY _tWinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPTSTR lpCmdLine,
    int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Check for existing instance using mutex
    g_hMutex = CreateMutex(NULL, FALSE, MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS ||
        GetLastError() == ERROR_ACCESS_DENIED)
    {
        // Another instance is already running
        if (g_hMutex)
        {
            CloseHandle(g_hMutex);
            g_hMutex = NULL;
        }
        return 0; // Exit without creating tray icon
    }

    // Initialize common controls
    INITCOMMONCONTROLSEX icex = { sizeof(INITCOMMONCONTROLSEX), ICC_COOL_CLASSES };
    InitCommonControlsEx(&icex);

    g_hInstance = hInstance;

    // Register window class
    MyRegisterClass(hInstance);

    // Perform application initialization
    if (!InitInstance(hInstance, SW_HIDE)) // Start hidden
    {
        if (g_hMutex)
        {
            ReleaseMutex(g_hMutex);
            CloseHandle(g_hMutex);
            g_hMutex = NULL;
        }
        return FALSE;
    }

    // Register for taskbar recreation notification
    g_uTaskbarRestart = RegisterWindowMessage(_T("TaskbarCreated"));

    // Add tray icon
    AddTrayIcon(g_hWnd);

    // Check command line for "/show" parameter
    LPCTSTR cmdLine = GetCommandLine();
    if (_tcsstr(cmdLine, _T("/show")) != NULL)
    {
        ShowMainWindow(g_hWnd);
    }

    // Main message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup mutex
    if (g_hMutex)
    {
        ReleaseMutex(g_hMutex);
        CloseHandle(g_hMutex);
        g_hMutex = NULL;
    }

    return (int)msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex = { 0 };
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
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
        CW_USEDEFAULT, CW_USEDEFAULT,
        400, 300,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (!g_hWnd)
    {
        return FALSE;
    }

    // Center window on screen
    RECT rc;
    GetWindowRect(g_hWnd, &rc);
    int xPos = (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2;
    int yPos = (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2;
    SetWindowPos(g_hWnd, NULL, xPos, yPos, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

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
    case WM_CREATE:
        // Additional initialization can go here
        break;

    case WM_TRAYICON:
    {
        switch (lParam)
        {
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            ShowMainWindow(hWnd);
            break;

        case WM_RBUTTONUP:
            ShowContextMenu(hWnd);
            break;
        }
        break;
    }

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        case IDM_OPEN:
            ShowMainWindow(hWnd);
            break;

        case IDM_EXIT:
        case ID_FILE_EXIT:
            DestroyWindow(hWnd);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    }

    case WM_CLOSE:
        // Hide window instead of closing
        ShowWindow(hWnd, SW_HIDE);
        g_bMainWindowVisible = false;

        // Optional: Show notification
        NOTIFYICONDATA nid = { sizeof(NOTIFYICONDATA) };
        nid.hWnd = hWnd;
        nid.uID = ID_TRAY_ICON;
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_INFO;
        _tcscpy_s(nid.szInfoTitle, _T("TrayApp"));
        _tcscpy_s(nid.szInfo, _T("Приложение продолжает работу в фоновом режиме"));
        Shell_NotifyIcon(NIM_MODIFY, &nid);

        return 0; // Prevent default close behavior

    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        break;

    default:
        // Handle taskbar recreation
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
    _tcscpy_s(g_nid.szTip, _T("Tray Application - Right click for menu"));

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
    if (hMenu)
    {
        // Add menu items
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_OPEN, _T("Открыть"));
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
        InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, IDM_EXIT, _T("Выход"));

        // Set default menu item
        SetMenuDefaultItem(hMenu, IDM_OPEN, FALSE);

        // Display context menu
        SetForegroundWindow(hWnd);
        TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON,
            pt.x, pt.y, 0, hWnd, NULL);
        PostMessage(hWnd, WM_NULL, 0, 0);

        DestroyMenu(hMenu);
    }
}

void ShowMainWindow(HWND hWnd)
{
    if (IsIconic(hWnd))
    {
        ShowWindow(hWnd, SW_RESTORE);
    }
    else
    {
        ShowWindow(hWnd, SW_SHOW);
    }
    SetForegroundWindow(hWnd);
    SetActiveWindow(hWnd);
    g_bMainWindowVisible = true;
}
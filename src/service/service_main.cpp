#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <rpc.h>
#include <fstream>
#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <set>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "rpcrt4.lib")

#include "../common/service_rpc.h"

#define SERVICE_NAME L"TrayAppService"

void LogToFile(const wchar_t* msg)
{
    std::wofstream log;
    log.open(L"C:\\TrayService.log", std::ios::app);
    if (log.is_open())
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        log << L"[" << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"] " << msg << std::endl;
        log.close();
    }
}

SERVICE_STATUS g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = NULL;
std::map<DWORD, std::vector<HANDLE>> g_SessionProcesses;
std::mutex g_ProcessMutex;

// RPC Interface Implementation
void StopService(handle_t h)
{
    (void)h;
    LogToFile(L"RPC: StopService called by client");
    if (g_ServiceStopEvent) SetEvent(g_ServiceStopEvent);
}

long GetStatus(handle_t h)
{
    (void)h;
    return g_ServiceStatus.dwCurrentState;
}

void Shutdown(handle_t h)
{
    StopService(h);
}

// RPC Server Thread
DWORD WINAPI RpcServerThread(LPVOID lp)
{
    (void)lp;
    LogToFile(L"RPC: Starting ALPC server...");

    RPC_STATUS status = RpcServerUseProtseqEpW(
        (RPC_WSTR)L"ncalrpc",
        RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
        (RPC_WSTR)L"TrayAppServiceRPC",
        NULL
    );

    wchar_t buf[100];
    wsprintf(buf, L"RPC: UseProtseqEp=%d", (int)status);
    LogToFile(buf);

    if (status == RPC_S_OK || status == RPC_S_DUPLICATE_ENDPOINT)
    {
        status = RpcServerRegisterIf(
            ServiceControl_v1_0_s_ifspec,
            NULL,
            NULL
        );
        wsprintf(buf, L"RPC: RegisterIf=%d", (int)status);
        LogToFile(buf);

        if (status == RPC_S_OK)
        {
            LogToFile(L"RPC: ALPC server LISTENING...");
            status = RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, FALSE);
            wsprintf(buf, L"RPC: Listen returned=%d", (int)status);
            LogToFile(buf);
        }
    }

    LogToFile(L"RPC: Server stopped - signaling service");
    if (g_ServiceStopEvent) SetEvent(g_ServiceStopEvent);
    return 0;
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode);
DWORD WINAPI ServiceWorkerThread(LPVOID lp);
void StartAppInSession(DWORD sessionId);
void StopAllApps();

int WINAPI WinMain(HINSTANCE h, HINSTANCE hp, LPSTR c, int n)
{
    (void)h; (void)hp; (void)c; (void)n;
    LogToFile(L"WinMain: Starting dispatcher");

    SERVICE_TABLE_ENTRY st[] = {
        { (LPWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain },
        { NULL, NULL }
    };

    if (!StartServiceCtrlDispatcher(st))
    {
        wchar_t buf[100];
        wsprintf(buf, L"ERROR: Dispatcher=%d", (int)GetLastError());
        LogToFile(buf);
        return GetLastError();
    }
    return 0;
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
    (void)argc; (void)argv;
    LogToFile(L"ServiceMain: START");

    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) { LogToFile(L"ERROR: RegisterServiceCtrlHandler"); return; }

    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwWin32ExitCode = NO_ERROR;
    g_ServiceStatus.dwCheckPoint = 1;
    g_ServiceStatus.dwWaitHint = 10000;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent)
    {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    // Start RPC server
    HANDLE hRpc = CreateThread(NULL, 0, RpcServerThread, NULL, 0, NULL);
    LogToFile(L"ServiceMain: RPC thread created");

    // Start worker thread
    HANDLE hWorker = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
    LogToFile(L"ServiceMain: Worker thread created");

    // Set RUNNING
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    LogToFile(L"ServiceMain: RUNNING");

    // Wait for stop signal
    WaitForSingleObject(g_ServiceStopEvent, INFINITE);
    LogToFile(L"ServiceMain: Stop signal received");

    // Stop
    g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    StopAllApps();

    RpcMgmtStopServerListening(NULL);

    if (hRpc) { WaitForSingleObject(hRpc, 5000); CloseHandle(hRpc); }
    if (hWorker) { WaitForSingleObject(hWorker, 5000); CloseHandle(hWorker); }

    CloseHandle(g_ServiceStopEvent);
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    LogToFile(L"ServiceMain: STOPPED");
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
    wchar_t buf[100];
    wsprintf(buf, L"CtrlHandler: code=%d (IGNORED)", (int)CtrlCode);

    switch (CtrlCode)
    {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        LogToFile(buf);
        break;
    case SERVICE_CONTROL_INTERROGATE:
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        break;
    }
}

DWORD WINAPI ServiceWorkerThread(LPVOID lp)
{
    (void)lp;
    LogToFile(L"Worker: STARTED");

    std::set<DWORD> known;
    WTS_SESSION_INFO* p = NULL;
    DWORD n = 0;

    if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &p, &n))
    {
        wchar_t buf[100];
        wsprintf(buf, L"Worker: Found %d sessions", (int)n);
        LogToFile(buf);

        for (DWORD i = 0; i < n; i++)
        {
            if (p[i].SessionId != 0)
            {
                known.insert(p[i].SessionId);
                if (p[i].State == WTSActive || p[i].State == WTSConnected)
                {
                    StartAppInSession(p[i].SessionId);
                }
            }
        }
        WTSFreeMemory(p);
    }

    while (WaitForSingleObject(g_ServiceStopEvent, 3000) == WAIT_TIMEOUT)
    {
        WTS_SESSION_INFO* ps = NULL;
        DWORD ns = 0;
        if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &ps, &ns))
        {
            for (DWORD i = 0; i < ns; i++)
            {
                if (ps[i].SessionId != 0 && known.find(ps[i].SessionId) == known.end())
                {
                    known.insert(ps[i].SessionId);
                    if (ps[i].State == WTSActive || ps[i].State == WTSConnected)
                    {
                        StartAppInSession(ps[i].SessionId);
                    }
                }
            }
            WTSFreeMemory(ps);
        }
    }

    LogToFile(L"Worker: EXITING");
    return 0;
}

void StartAppInSession(DWORD sessionId)
{
    HANDLE hToken = NULL;
    if (!WTSQueryUserToken(sessionId, &hToken)) return;

    HANDLE hDup = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hDup))
    {
        CloseHandle(hToken);
        return;
    }
    CloseHandle(hToken);

    WCHAR path[MAX_PATH];
    GetModuleFileName(NULL, path, MAX_PATH);
    std::wstring app = path;
    size_t pos = app.rfind(L"\\");
    if (pos != std::wstring::npos) app = app.substr(0, pos + 1) + L"TrayApp.exe";

    STARTUPINFO si = { sizeof(si) };
    si.lpDesktop = NULL;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    LPVOID env = NULL;
    CreateEnvironmentBlock(&env, hDup, FALSE);

    if (CreateProcessAsUser(hDup, NULL, (LPWSTR)app.c_str(), NULL, NULL, FALSE,
        CREATE_UNICODE_ENVIRONMENT, env, NULL, &si, &pi))
    {
        LogToFile((L"StartApp: SUCCESS PID=" + std::to_wstring(pi.dwProcessId)).c_str());
        std::lock_guard<std::mutex> lock(g_ProcessMutex);
        g_SessionProcesses[sessionId].push_back(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    if (env) DestroyEnvironmentBlock(env);
    CloseHandle(hDup);
}

void StopAllApps()
{
    std::lock_guard<std::mutex> lock(g_ProcessMutex);
    for (auto& pair : g_SessionProcesses)
        for (HANDLE h : pair.second)
        {
            TerminateProcess(h, 0);
            CloseHandle(h);
        }
    g_SessionProcesses.clear();
}
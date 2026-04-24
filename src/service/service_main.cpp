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

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode);
DWORD WINAPI ServiceWorkerThread(LPVOID lp);
void StartAppInSession(DWORD sessionId);
void StopAllApps();

int WINAPI WinMain(HINSTANCE h, HINSTANCE hp, LPSTR c, int n)
{
    LogToFile(L"WinMain: Starting dispatcher");
    SERVICE_TABLE_ENTRY st[] = {
        { (LPWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain },
        { NULL, NULL }
    };
    if (!StartServiceCtrlDispatcher(st))
    {
        LogToFile((L"ERROR: Dispatcher=" + std::to_wstring(GetLastError())).c_str());
        return GetLastError();
    }
    return 0;
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
    LogToFile(L"ServiceMain: START");

    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) { LogToFile(L"ERROR: RegisterServiceCtrlHandler"); return; }

    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
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

    // Start worker thread
    HANDLE hWorker = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
    LogToFile(L"ServiceMain: Worker thread created");

    // Set RUNNING
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    LogToFile(L"ServiceMain: RUNNING");

    // Wait for stop
    WaitForSingleObject(g_ServiceStopEvent, INFINITE);
    LogToFile(L"ServiceMain: Stop signal received");

    // Stop
    g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    StopAllApps();

    if (hWorker) { WaitForSingleObject(hWorker, 5000); CloseHandle(hWorker); }

    CloseHandle(g_ServiceStopEvent);
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    LogToFile(L"ServiceMain: STOPPED");
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
    switch (CtrlCode)
    {
    case SERVICE_CONTROL_STOP:
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        if (g_ServiceStopEvent) SetEvent(g_ServiceStopEvent);
        break;
    case SERVICE_CONTROL_SHUTDOWN:
        // IGNORED
        break;
    case SERVICE_CONTROL_INTERROGATE:
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        break;
    }
}

DWORD WINAPI ServiceWorkerThread(LPVOID lp)
{
    LogToFile(L"Worker: STARTED");

    std::set<DWORD> known;
    WTS_SESSION_INFO* p = NULL;
    DWORD n = 0;

    if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &p, &n))
    {
        LogToFile((L"Worker: Found " + std::to_wstring(n) + L" sessions").c_str());
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

    // Monitor for new sessions
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
    LogToFile((L"StartApp: session " + std::to_wstring(sessionId)).c_str());

    HANDLE hToken = NULL;
    if (!WTSQueryUserToken(sessionId, &hToken))
    {
        LogToFile((L"StartApp: Token error=" + std::to_wstring(GetLastError())).c_str());
        return;
    }

    HANDLE hDup = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hDup))
    {
        LogToFile((L"StartApp: DupToken error=" + std::to_wstring(GetLastError())).c_str());
        CloseHandle(hToken);
        return;
    }
    CloseHandle(hToken);

    WCHAR path[MAX_PATH];
    GetModuleFileName(NULL, path, MAX_PATH);
    std::wstring app = path;
    size_t pos = app.rfind(L"\\");
    if (pos != std::wstring::npos) app = app.substr(0, pos + 1) + L"TrayApp.exe";

    LogToFile((L"StartApp: Launching " + app).c_str());

    STARTUPINFO si = { sizeof(si) };
    si.lpDesktop = (LPWSTR)L"winsta0\\default";
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
    else
    {
        LogToFile((L"StartApp: FAILED error=" + std::to_wstring(GetLastError())).c_str());
    }

    if (env) DestroyEnvironmentBlock(env);
    CloseHandle(hDup);
}

void StopAllApps()
{
    LogToFile(L"StopAllApps: Terminating processes");
    std::lock_guard<std::mutex> lock(g_ProcessMutex);
    for (auto& pair : g_SessionProcesses)
    {
        for (HANDLE h : pair.second)
        {
            TerminateProcess(h, 0);
            CloseHandle(h);
        }
    }
    g_SessionProcesses.clear();
}
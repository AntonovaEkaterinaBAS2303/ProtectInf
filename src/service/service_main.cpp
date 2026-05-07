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
#include <filesystem>

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
std::wstring g_ServiceDirectory;

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
DWORD WINAPI RpcServerThread(LPVOID) {
    RPC_STATUS s = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, (RPC_WSTR)L"TrayAppServiceRPC", NULL);
    if (s == RPC_S_OK || s == RPC_S_DUPLICATE_ENDPOINT) { s = RpcServerRegisterIf(ServiceControl_v1_0_s_ifspec, NULL, NULL); if (s == RPC_S_OK) RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, FALSE); }
    if (g_ServiceStopEvent) SetEvent(g_ServiceStopEvent); return 0;
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode);
DWORD WINAPI ServiceWorkerThread(LPVOID lp);
void StartAppInSession(DWORD sessionId);
void StopAllApps();

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    wchar_t mp[MAX_PATH]; GetModuleFileNameW(NULL, mp, MAX_PATH);
    g_ServiceDirectory = std::filesystem::path(mp).parent_path().wstring(); SetCurrentDirectoryW(g_ServiceDirectory.c_str());
    SERVICE_TABLE_ENTRY st[] = { {(LPWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain}, {NULL,NULL} };
    if (!StartServiceCtrlDispatcher(st)) { wchar_t b[100]; wsprintf(b, L"ERROR: %d", GetLastError()); LogToFile(b); return GetLastError(); }
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
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_SESSIONCHANGE |
        SERVICE_ACCEPT_STOP |
        SERVICE_ACCEPT_SHUTDOWN;
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
    HANDLE hWorker = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    LogToFile(L"ServiceMain: RUNNING");

    // Wait for stop signal (from RPC or internal)
    WaitForSingleObject(g_ServiceStopEvent, INFINITE);
    LogToFile(L"ServiceMain: Stop signal received");

    g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    StopAllApps();
    RpcMgmtStopServerListening(NULL);

    if (hRpc) { WaitForSingleObject(hRpc, 5000); CloseHandle(hRpc); }
    if (hWorker) { WaitForSingleObject(hWorker, 5000); CloseHandle(hWorker); }

    CloseHandle(g_ServiceStopEvent);
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    LogToFile(L"ServiceMain: STOPPED");
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
    switch (CtrlCode)
    {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        // Игнорируем — служба останавливается только через RPC
        LogToFile(L"CtrlHandler: STOP/SHUTDOWN signal IGNORED");
        break;
    case SERVICE_CONTROL_SESSIONCHANGE:
    {
        LogToFile(L"CtrlHandler: SESSIONCHANGE event");
        WTS_SESSION_INFO* p = NULL;
        DWORD n = 0;
        if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &p, &n)) {
            for (DWORD i = 0; i < n; i++) {
                if (p[i].SessionId != 0 && p[i].State == WTSActive) {
                    StartAppInSession(p[i].SessionId);
                }
            }
            WTSFreeMemory(p);
        }
    }
    break;
    case SERVICE_CONTROL_INTERROGATE:
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}
DWORD WINAPI ServiceWorkerThread(LPVOID) {
    LogToFile(L"ServiceWorkerThread: Started");

    std::set<DWORD> known;
    WTS_SESSION_INFO* p = NULL;
    DWORD n = 0;

    // Первый проход - запускаем в существующих активных сессиях
    if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &p, &n)) {
        wchar_t buf[256];
        wsprintf(buf, L"ServiceWorkerThread: Found %d sessions", n);
        LogToFile(buf);

        for (DWORD i = 0; i < n; i++) {
            wsprintf(buf, L"ServiceWorkerThread: Session %d - ID=%d, State=%d",
                i, p[i].SessionId, p[i].State);
            LogToFile(buf);

            if (p[i].SessionId == 0) continue;

            known.insert(p[i].SessionId);

            // Запускаем для активных И подключенных сессий
            if (p[i].State == WTSActive || p[i].State == WTSConnected || p[i].State == WTSDisconnected) {
                LogToFile(L"ServiceWorkerThread: Starting app in session");
                StartAppInSession(p[i].SessionId);
            }
        }
        WTSFreeMemory(p);
    }

    LogToFile(L"ServiceWorkerThread: Entering monitoring loop");

    while (WaitForSingleObject(g_ServiceStopEvent, 5000) == WAIT_TIMEOUT) {
        WTS_SESSION_INFO* ps = NULL;
        DWORD ns = 0;

        if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &ps, &ns)) {
            for (DWORD i = 0; i < ns; i++) {
                if (ps[i].SessionId == 0) continue;

                // Новая сессия
                if (!known.count(ps[i].SessionId)) {
                    wchar_t buf[256];
                    wsprintf(buf, L"ServiceWorkerThread: NEW session! ID=%d, State=%d",
                        ps[i].SessionId, ps[i].State);
                    LogToFile(buf);

                    known.insert(ps[i].SessionId);

                    if (ps[i].State == WTSActive || ps[i].State == WTSConnected) {
                        LogToFile(L"ServiceWorkerThread: Launching app in new session");
                        StartAppInSession(ps[i].SessionId);
                    }
                }
                // Существующая сессия, но изменилось состояние
                else if (ps[i].State == WTSActive && !g_SessionProcesses.count(ps[i].SessionId)) {
                    wchar_t buf[256];
                    wsprintf(buf, L"ServiceWorkerThread: Session %d became active, starting app", ps[i].SessionId);
                    LogToFile(buf);
                    StartAppInSession(ps[i].SessionId);
                }
            }
            WTSFreeMemory(ps);
        }
    }

    LogToFile(L"ServiceWorkerThread: Exiting");
    return 0;
}

void StartAppInSession(DWORD sessionId)
{
    {
        std::lock_guard<std::mutex> lock(g_ProcessMutex);
        auto it = g_SessionProcesses.find(sessionId);
        if (it != g_SessionProcesses.end() && !it->second.empty())
        {
            // Проверяем, жив ли ещё процесс
            bool allDead = true;
            for (HANDLE h : it->second) {
                DWORD exitCode = 0;
                if (GetExitCodeProcess(h, &exitCode) && exitCode == STILL_ACTIVE) {
                    allDead = false;
                    break;
                }
            }
            if (!allDead) {
                LogToFile((L"StartApp: App already running in session " + std::to_wstring(sessionId)).c_str());
                return; // Уже запущено
            }
            // Все мёртвые — очищаем
            it->second.clear();
        }
    }

    wchar_t buf[512];

    // Логируем попытку запуска
    wsprintf(buf, L"StartAppInSession: Trying to start app for session %d", sessionId);
    LogToFile(buf);

    HANDLE hToken = NULL;
    if (!WTSQueryUserToken(sessionId, &hToken))
    {
        LogToFile((L"StartApp: Token error=" + std::to_wstring(GetLastError())).c_str());
        return;
    }

    HANDLE hDup = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hDup))
    {
        CloseHandle(hToken);
        return;
    }
    CloseHandle(hToken);

    std::wstring appPath = g_ServiceDirectory + L"\\TrayApp.exe";
    std::wstring cmdLine = L"\"" + appPath + L"\" --service";
    LogToFile((L"StartApp: Launching " + cmdLine).c_str());

    STARTUPINFO si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    LPVOID env = NULL;
    CreateEnvironmentBlock(&env, hDup, FALSE);

    if (CreateProcessAsUser(hDup, NULL, (LPWSTR)cmdLine.c_str(), NULL, NULL, FALSE,
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

    for (auto& pair : g_SessionProcesses) {
        for (HANDLE h : pair.second) {
            DWORD pid = GetProcessId(h);
            // Сначала пробуем мягкое завершение
            EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                DWORD wndPid = 0;
                GetWindowThreadProcessId(hwnd, &wndPid);
                if (wndPid == (DWORD)lParam) {
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                }
                return TRUE;
                }, (LPARAM)pid);

            if (WaitForSingleObject(h, 3000) == WAIT_TIMEOUT) {
                TerminateProcess(h, 0);
            }
            CloseHandle(h);
        }
    }
    g_SessionProcesses.clear();
}
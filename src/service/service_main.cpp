#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <tlhelp32.h>
#include <rpc.h>
#include <vector>
#include <string>
#include <map>
#include <mutex>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "rpcrt4.lib")

#define SERVICE_NAME L"TrayAppService"
#define RPC_ENDPOINT L"TrayAppServiceRPC"

// Глобальные переменные
SERVICE_STATUS g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = INVALID_HANDLE_VALUE;
std::map<DWORD, std::vector<HANDLE>> g_SessionProcesses;
std::mutex g_ProcessMutex;
RPC_BINDING_VECTOR* g_pBindingVector = NULL;

// Прототипы функций
VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode);
DWORD WINAPI ServiceWorkerThread(LPVOID lpParam);
void StartAppInSession(DWORD sessionId);
void StopAllApps();
BOOL GetProcessUserSid(DWORD pid, PSID* ppSid);
DWORD GetParentProcessId(DWORD pid);

// RPC функции
void StopService()
{
    // Остановка службы через RPC
    SERVICE_STATUS ssStatus;
    ssStatus.dwCurrentState = SERVICE_STOP_PENDING;
    ssStatus.dwControlsAccepted = 0;
    SetServiceStatus(g_StatusHandle, &ssStatus);

    SetEvent(g_ServiceStopEvent);
}

int CheckServiceStatus()
{
    return g_ServiceStatus.dwCurrentState;
}

void Shutdown()
{
    StopService();
}

// RPC интерфейс
RPC_STATUS RPC_ENTRY ServiceControl_SecurityCallback(
    RPC_IF_HANDLE /*hInterface*/,
    void* /*pContext*/
)
{
    return RPC_S_OK; // Разрешить все подключения
}

// Точка входа для службы
int wmain(int argc, wchar_t* argv[])
{
    SERVICE_TABLE_ENTRY ServiceTable[] =
    {
        { (LPWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain },
        { NULL, NULL }
    };

    if (StartServiceCtrlDispatcher(ServiceTable) == FALSE)
    {
        return GetLastError();
    }

    return 0;
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
    // Регистрация обработчика управления службой
    g_StatusHandle = RegisterServiceCtrlHandler(
        SERVICE_NAME,
        ServiceCtrlHandler
    );

    if (!g_StatusHandle)
    {
        return;
    }

    // Инициализация статуса службы
    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwWin32ExitCode = 0;
    g_ServiceStatus.dwServiceSpecificExitCode = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;

    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    // Создание события для остановки службы
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_ServiceStopEvent)
    {
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    // Инициализация RPC сервера
    RPC_STATUS rpcStatus;
    rpcStatus = RpcServerUseProtseqEp(
        (RPC_WSTR)L"ncalrpc",
        RPC_C_PROTSEQ_MAX_REQS_DEFAULT,
        (RPC_WSTR)RPC_ENDPOINT,
        NULL
    );

    if (rpcStatus == RPC_S_OK)
    {
        rpcStatus = RpcServerRegisterIf(
            ServiceControl_ServerIfHandle,
            NULL,
            NULL
        );
    }

    if (rpcStatus == RPC_S_OK)
    {
        rpcStatus = RpcServerRegisterAuthInfo(
            NULL,
            RPC_C_AUTHN_WINNT,
            NULL,
            NULL
        );
    }

    // Запуск прослушивания RPC
    if (rpcStatus == RPC_S_OK)
    {
        rpcStatus = RpcServerListen(
            1,
            RPC_C_LISTEN_MAX_CALLS_DEFAULT,
            FALSE
        );
    }

    if (rpcStatus != RPC_S_OK)
    {
        // Ошибка RPC, но продолжаем работу без RPC
    }

    // Запуск рабочего потока
    HANDLE hThread = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

    // Обновление статуса - запущен
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    // Ожидание сигнала остановки или завершения потока
    HANDLE hEvents[2] = { g_ServiceStopEvent, hThread };
    WaitForMultipleObjects(2, hEvents, FALSE, INFINITE);

    // Остановка всех запущенных приложений
    StopAllApps();

    // Остановка RPC сервера
    RpcMgmtStopServerListening(NULL);
    RpcServerUnregisterIf(NULL, NULL, FALSE);

    // Завершение работы
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    if (hThread)
    {
        CloseHandle(hThread);
    }
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
    switch (CtrlCode)
    {
    case SERVICE_CONTROL_STOP:
        g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
        g_ServiceStatus.dwControlsAccepted = 0;
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        SetEvent(g_ServiceStopEvent);
        break;

    case SERVICE_CONTROL_SHUTDOWN:
        // Игнорировать shutdown
        break;

    default:
        break;
    }
}

DWORD WINAPI ServiceWorkerThread(LPVOID lpParam)
{
    // Запуск приложений в существующих сессиях
    WTS_SESSION_INFO* pSessionInfo = NULL;
    DWORD dwCount = 0;

    if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessionInfo, &dwCount))
    {
        for (DWORD i = 0; i < dwCount; i++)
        {
            if (pSessionInfo[i].SessionId != 0 && // Пропустить сессию 0
                pSessionInfo[i].State == WTSActive)
            {
                StartAppInSession(pSessionInfo[i].SessionId);
            }
        }
        WTSFreeMemory(pSessionInfo);
    }

    // Отслеживание новых входов пользователей
    while (WaitForSingleObject(g_ServiceStopEvent, 1000) == WAIT_TIMEOUT)
    {
        // Проверка новых сессий
        WTS_SESSION_INFO* pNewSessionInfo = NULL;
        DWORD dwNewCount = 0;

        if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pNewSessionInfo, &dwNewCount))
        {
            for (DWORD i = 0; i < dwNewCount; i++)
            {
                if (pNewSessionInfo[i].SessionId != 0 &&
                    pNewSessionInfo[i].State == WTSActive)
                {
                    // Проверить, запущено ли уже приложение в этой сессии
                    bool alreadyRunning = false;
                    {
                        std::lock_guard<std::mutex> lock(g_ProcessMutex);
                        auto it = g_SessionProcesses.find(pNewSessionInfo[i].SessionId);
                        if (it != g_SessionProcesses.end() && !it->second.empty())
                        {
                            // Проверить, жив ли процесс
                            DWORD exitCode;
                            if (GetExitCodeProcess(it->second[0], &exitCode) &&
                                exitCode == STILL_ACTIVE)
                            {
                                alreadyRunning = true;
                            }
                        }
                    }

                    if (!alreadyRunning)
                    {
                        StartAppInSession(pNewSessionInfo[i].SessionId);
                    }
                }
            }
            WTSFreeMemory(pNewSessionInfo);
        }
    }

    return ERROR_SUCCESS;
}

void StartAppInSession(DWORD sessionId)
{
    // Получить токен пользователя для сессии
    HANDLE hUserToken = NULL;
    if (!WTSQueryUserToken(sessionId, &hUserToken))
    {
        return;
    }

    // Получить SID пользователя
    PTOKEN_USER pTokenUser = NULL;
    DWORD dwSize = 0;
    GetTokenInformation(hUserToken, TokenUser, NULL, 0, &dwSize);
    pTokenUser = (PTOKEN_USER)LocalAlloc(LPTR, dwSize);
    if (!GetTokenInformation(hUserToken, TokenUser, pTokenUser, dwSize, &dwSize))
    {
        CloseHandle(hUserToken);
        LocalFree(pTokenUser);
        return;
    }

    // Создать процесс от имени пользователя
    WCHAR szAppPath[MAX_PATH];
    GetModuleFileName(NULL, szAppPath, MAX_PATH);

    // Заменить имя службы на имя GUI приложения
    std::wstring appPath = szAppPath;
    size_t pos = appPath.find(L"TrayService.exe");
    if (pos != std::wstring::npos)
    {
        appPath.replace(pos, 17, L"TrayApp.exe");
    }

    STARTUPINFO si = { sizeof(STARTUPINFO) };
    PROCESS_INFORMATION pi = { 0 };
    si.lpDesktop = (LPWSTR)L"winsta0\\default";
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    LPVOID pEnvironment = NULL;
    CreateEnvironmentBlock(&pEnvironment, hUserToken, FALSE);

    DWORD dwCreationFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE;

    if (CreateProcessAsUser(
        hUserToken,
        (LPWSTR)appPath.c_str(),
        NULL,
        NULL,
        NULL,
        FALSE,
        dwCreationFlags,
        pEnvironment,
        NULL,
        &si,
        &pi))
    {
        std::lock_guard<std::mutex> lock(g_ProcessMutex);
        g_SessionProcesses[sessionId].push_back(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    if (pEnvironment)
    {
        DestroyEnvironmentBlock(pEnvironment);
    }

    LocalFree(pTokenUser);
    CloseHandle(hUserToken);
}

void StopAllApps()
{
    std::lock_guard<std::mutex> lock(g_ProcessMutex);

    for (auto& session : g_SessionProcesses)
    {
        for (HANDLE hProcess : session.second)
        {
            TerminateProcess(hProcess, 0);
            CloseHandle(hProcess);
        }
    }

    g_SessionProcesses.clear();
}

DWORD GetParentProcessId(DWORD pid)
{
    DWORD ppid = 0;
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
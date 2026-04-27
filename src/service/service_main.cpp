#include <windows.h>
#include <winhttp.h>
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
#include <chrono>
#include <sstream>
#include <iomanip>
#include <memory>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "winhttp.lib")

#include "service_rpc.h"

extern "C" {
    void* __RPC_USER MIDL_user_allocate(size_t size) {
        return malloc(size);
    }

    void __RPC_USER MIDL_user_free(void* p) {
        free(p);
    }
}

#define SERVICE_NAME L"TrayAppService"

#define API_HOST L"localhost"
#define API_PORT 8443
#define API_USE_HTTPS true  

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

// Токены и лицензии
struct AuthTokens {
    std::wstring accessToken;
    std::wstring refreshToken;
    std::chrono::system_clock::time_point accessExpiry;
    std::chrono::system_clock::time_point refreshExpiry;
};

struct LicenseInfo {
    std::wstring ticket;
    std::chrono::system_clock::time_point expiryDate;
    bool active;
};

std::mutex g_AuthMutex;
std::mutex g_LicenseMutex;
std::unique_ptr<AuthTokens> g_AuthTokens;
std::unique_ptr<LicenseInfo> g_LicenseInfo;
std::wstring g_AuthenticatedUser;
HANDLE g_hRefreshThread = NULL;
HANDLE g_hLicenseRefreshThread = NULL;
bool g_bStopRefreshThreads = false;

// Forward declarations
bool PerformLogin(const std::wstring& username, const std::wstring& password);
bool RefreshTokens();
bool RequestLicenseStatus();
bool ActivateLicense(const std::wstring& activationCode);
DWORD WINAPI TokenRefreshThread(LPVOID lp);
DWORD WINAPI LicenseRefreshThread(LPVOID lp);
void StartRefreshThreads();

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

// Простой парсер JSON значений
std::wstring ExtractJsonValue(const std::wstring& json, const std::wstring& key) {
    std::wstring search = L"\"" + key + L"\":\"";
    size_t start = json.find(search);
    if (start == std::wstring::npos) {
        search = L"\"" + key + L"\":";
        start = json.find(search);
        if (start == std::wstring::npos) return L"";
        start += search.length();
        // Проверяем, не число ли это или boolean
        size_t end = json.find(L",", start);
        size_t endBrace = json.find(L"}", start);
        if (end == std::wstring::npos) end = endBrace;
        if (endBrace != std::wstring::npos && endBrace < end) end = endBrace;
        std::wstring val = json.substr(start, end - start);
        // Убираем пробелы и кавычки
        while (!val.empty() && (val[0] == L' ' || val[0] == L'"')) val = val.substr(1);
        while (!val.empty() && (val.back() == L' ' || val.back() == L'"')) val.pop_back();
        return val;
    }
    start += search.length();
    size_t end = json.find(L"\"", start);
    return json.substr(start, end - start);
}

long long ExtractJsonInt(const std::wstring& json, const std::wstring& key) {
    std::wstring val = ExtractJsonValue(json, key);
    if (val.empty()) return 0;
    try {
        return std::stoll(val);
    }
    catch (...) {
        return 0;
    }
}

// HTTP/HTTPS клиент
class HttpClient {
private:
    HINTERNET hSession;
    HINTERNET hConnect;
    HINTERNET hRequest;
    std::wstring host;
    int port;
    bool useHttps;

public:
    HttpClient(const std::wstring& server, int port = 8080, bool https = false)
        : hSession(NULL), hConnect(NULL), hRequest(NULL), host(server), port(port), useHttps(https) {
        hSession = WinHttpOpen(L"TrayApp/1.0",
            WINHTTP_ACCESS_TYPE_NO_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
    }

    ~HttpClient() {
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
    }

    bool Connect() {
        hConnect = WinHttpConnect(hSession, host.c_str(), (INTERNET_PORT)port, 0);
        return hConnect != NULL;
    }

    bool SendRequest(const std::wstring& method, const std::wstring& path,
        const std::wstring& body = L"",
        const std::wstring& authHeader = L"") {

        DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;

        hRequest = WinHttpOpenRequest(hConnect, method.c_str(), path.c_str(),
            NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags);

        if (!hRequest) return false;

        // Для HTTPS может потребоваться игнорировать ошибки сертификата при тестировании
        if (useHttps) {
            DWORD secFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
                SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, sizeof(secFlags));
        }

        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!authHeader.empty()) {
            headers += L"Authorization: Bearer " + authHeader + L"\r\n";
        }

        LPCWSTR bodyPtr = body.empty() ? WINHTTP_NO_REQUEST_DATA : body.c_str();
        DWORD bodyLength = body.empty() ? 0 : (DWORD)(body.length() * sizeof(wchar_t));

        if (!WinHttpSendRequest(hRequest, headers.c_str(), -1,
            (LPVOID)bodyPtr, bodyLength, bodyLength, 0)) {
            return false;
        }

        return WinHttpReceiveResponse(hRequest, NULL) != FALSE;
    }

    std::wstring GetResponse() {
        std::wstring response;
        DWORD size = 0;
        DWORD downloaded = 0;

        do {
            size = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &size)) break;
            if (size == 0) break;

            std::vector<char> buffer(size + 1);
            if (!WinHttpReadData(hRequest, buffer.data(), size, &downloaded)) break;

            // Конвертируем UTF-8 ответ в wstring
            int wideLen = MultiByteToWideChar(CP_UTF8, 0, buffer.data(), downloaded, NULL, 0);
            std::vector<wchar_t> wideBuf(wideLen + 1);
            MultiByteToWideChar(CP_UTF8, 0, buffer.data(), downloaded, wideBuf.data(), wideLen);
            wideBuf[wideLen] = L'\0';
            response.append(wideBuf.data());
        } while (size > 0);

        return response;
    }

    DWORD GetStatusCode() {
        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize,
            WINHTTP_NO_HEADER_INDEX);
        return statusCode;
    }
};

// Аутентификация
bool PerformLogin(const std::wstring& username, const std::wstring& password) {
    HttpClient client(API_HOST, API_PORT, API_USE_HTTPS);

    if (!client.Connect()) {
        LogToFile(L"HTTP: Connection failed");
        return false;
    }

    std::wstring body = L"{\"username\":\"" + username + L"\",\"password\":\"" + password + L"\",\"deviceId\":\"trayapp-windows\"}";

    if (!client.SendRequest(L"POST", L"/auth/login", body)) {
        LogToFile(L"HTTP: Login request failed");
        return false;
    }

    DWORD statusCode = client.GetStatusCode();
    std::wstring response = client.GetResponse();

    if (statusCode != 200) return false;

    std::lock_guard<std::mutex> lock(g_AuthMutex);
    g_AuthTokens = std::make_unique<AuthTokens>();
    g_AuthTokens->accessToken = ExtractJsonValue(response, L"accessToken");
    g_AuthTokens->refreshToken = ExtractJsonValue(response, L"refreshToken");
    g_AuthenticatedUser = username;

    // Жёстко задаём время жизни токенов
    auto now = std::chrono::system_clock::now();
    g_AuthTokens->accessExpiry = now + std::chrono::hours(1);   // 1 час
    g_AuthTokens->refreshExpiry = now + std::chrono::hours(720); // 30 дней

    LogToFile(L"HTTP: Login successful");
    return true;
}

bool RefreshTokens() {
    std::lock_guard<std::mutex> lock(g_AuthMutex);
    if (!g_AuthTokens) return false;

    HttpClient client(API_HOST, API_PORT, API_USE_HTTPS);
    if (!client.Connect()) return false;

    std::wstring body = L"{\"refreshToken\":\"" + g_AuthTokens->refreshToken + L"\"}";

    if (!client.SendRequest(L"POST", L"/auth/refresh", body)) return false;

    if (client.GetStatusCode() != 200) {
        LogToFile(L"HTTP: Token refresh failed");
        g_AuthTokens.reset();
        g_AuthenticatedUser.clear();
        return false;
    }

    std::wstring response = client.GetResponse();
    g_AuthTokens->accessToken = ExtractJsonValue(response, L"accessToken");
    g_AuthTokens->refreshToken = ExtractJsonValue(response, L"refreshToken");

    long long accessExp = ExtractJsonInt(response, L"accessTokenExpiresIn");
    long long refreshExp = ExtractJsonInt(response, L"refreshTokenExpiresIn");

    auto now = std::chrono::system_clock::now();
    g_AuthTokens->accessExpiry = now + std::chrono::seconds(accessExp > 0 ? accessExp : 3600);
    g_AuthTokens->refreshExpiry = now + std::chrono::seconds(refreshExp > 0 ? refreshExp : 86400);

    LogToFile(L"HTTP: Tokens refreshed");
    return true;
}

// Лицензия
bool RequestLicenseStatus() {
    std::wstring accessToken;
    {
        std::lock_guard<std::mutex> authLock(g_AuthMutex);
        if (!g_AuthTokens) return false;
        accessToken = g_AuthTokens->accessToken;
    }

    HttpClient client(API_HOST, API_PORT, API_USE_HTTPS);
    if (!client.Connect()) return false;

    // LicenseCheckRequest - предполагаем что нужен пустой body или token
    std::wstring body = L"{}";

    if (!client.SendRequest(L"POST", L"/api/license/check", body, accessToken)) {
        LogToFile(L"HTTP: License status request failed");
        return false;
    }

    DWORD statusCode = client.GetStatusCode();
    std::wstring response = client.GetResponse();

    wchar_t buf[256];
    wsprintf(buf, L"HTTP: License check response code=%d", (int)statusCode);
    LogToFile(buf);

    if (statusCode != 200) return false;

    std::lock_guard<std::mutex> licenseLock(g_LicenseMutex);
    g_LicenseInfo = std::make_unique<LicenseInfo>();
    g_LicenseInfo->ticket = ExtractJsonValue(response, L"ticket");
    g_LicenseInfo->active = (ExtractJsonValue(response, L"status") == L"ACTIVE" ||
        ExtractJsonValue(response, L"status") == L"active");

    long long expiry = ExtractJsonInt(response, L"expiresAt");
    if (expiry == 0) expiry = ExtractJsonInt(response, L"expires_at");
    if (expiry > 0) {
        g_LicenseInfo->expiryDate = std::chrono::system_clock::from_time_t((time_t)expiry);
    }
    else {
        g_LicenseInfo->expiryDate = std::chrono::system_clock::now() + std::chrono::hours(8760);
    }

    return g_LicenseInfo->active;
}

bool ActivateLicense(const std::wstring& activationCode) {
    std::wstring accessToken;
    {
        std::lock_guard<std::mutex> authLock(g_AuthMutex);
        if (!g_AuthTokens) return false;
        accessToken = g_AuthTokens->accessToken;
    }

    HttpClient client(API_HOST, API_PORT, API_USE_HTTPS);
    if (!client.Connect()) return false;

    std::wstring body = L"{\"activationCode\":\"" + activationCode + L"\"}";

    wchar_t buf[256];
    wsprintf(buf, L"HTTP: Activating license with code: %s", activationCode.c_str());
    LogToFile(buf);

    if (!client.SendRequest(L"POST", L"/api/license/activate", body, accessToken)) {
        LogToFile(L"HTTP: Activation request failed");
        return false;
    }

    DWORD statusCode = client.GetStatusCode();
    std::wstring response = client.GetResponse();

    wsprintf(buf, L"HTTP: Activation response code=%d", (int)statusCode);
    LogToFile(buf);

    if (statusCode != 200) return false;

    std::wstring ticket = ExtractJsonValue(response, L"ticket");

    if (!ticket.empty()) {
        std::lock_guard<std::mutex> licenseLock(g_LicenseMutex);
        g_LicenseInfo = std::make_unique<LicenseInfo>();
        g_LicenseInfo->ticket = ticket;
        g_LicenseInfo->active = true;
        long long expiry = ExtractJsonInt(response, L"expiresAt");
        if (expiry == 0) expiry = ExtractJsonInt(response, L"expires_at");
        if (expiry > 0) {
            g_LicenseInfo->expiryDate = std::chrono::system_clock::from_time_t((time_t)expiry);
        }
        else {
            g_LicenseInfo->expiryDate = std::chrono::system_clock::now() + std::chrono::hours(8760);
        }
        LogToFile(L"HTTP: License activated");
        return true;
    }

    // Если тикет не вернулся, запрашиваем статус
    LogToFile(L"HTTP: No ticket in response, checking status...");
    return RequestLicenseStatus();
}

// RPC: Login, Logout, GetUserInfo, Activate, GetLicenseInfo
long Login(handle_t h, const wchar_t* username, const wchar_t* password) {
    (void)h;
    return PerformLogin(username, password) ? 0 : 1;
}

long Logout(handle_t h) {
    (void)h;
    std::lock_guard<std::mutex> authLock(g_AuthMutex);
    g_AuthTokens.reset();
    g_AuthenticatedUser.clear();

    std::lock_guard<std::mutex> licLock(g_LicenseMutex);
    g_LicenseInfo.reset();
    return 0;
}

long GetUserInfo(handle_t h, wchar_t** username) {
    (void)h;
    std::lock_guard<std::mutex> lock(g_AuthMutex);

    if (!g_AuthTokens || g_AuthenticatedUser.empty()) {
        *username = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * 8);
        wcscpy_s(*username, 8, L"Unknown");
        return 1;
    }

    *username = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * (g_AuthenticatedUser.length() + 1));
    wcscpy_s(*username, g_AuthenticatedUser.length() + 1, g_AuthenticatedUser.c_str());
    return 0;
}

long Activate(handle_t h, const wchar_t* activationCode) {
    (void)h;
    return ActivateLicense(activationCode) ? 0 : 1;
}

long GetLicenseInfo(handle_t h, long* daysRemaining, wchar_t** expiryDate) {
    (void)h;
    std::lock_guard<std::mutex> lock(g_LicenseMutex);

    if (!g_LicenseInfo || !g_LicenseInfo->active) {
        *daysRemaining = 0;
        *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * 16);
        wcscpy_s(*expiryDate, 16, L"No License");
        return 1;
    }

    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::hours>(
        g_LicenseInfo->expiryDate - now
    );
    *daysRemaining = (long)(duration.count() / 24);

    time_t expiry = std::chrono::system_clock::to_time_t(g_LicenseInfo->expiryDate);
    struct tm stm;
    localtime_s(&stm, &expiry);

    std::wstringstream wss;
    wss << std::put_time(&stm, L"%Y-%m-%d");
    std::wstring dateStr = wss.str();

    *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * (dateStr.length() + 1));
    wcscpy_s(*expiryDate, dateStr.length() + 1, dateStr.c_str());

    return 0;
}

// Потоки обновления токенов
DWORD WINAPI TokenRefreshThread(LPVOID lp) {
    (void)lp;
    while (!g_bStopRefreshThreads) {
        Sleep(30000);

        bool needRefresh = false;
        {
            std::lock_guard<std::mutex> lock(g_AuthMutex);
            if (!g_AuthTokens) continue;

            auto now = std::chrono::system_clock::now();
            if (now >= g_AuthTokens->accessExpiry - std::chrono::seconds(300)) {
                needRefresh = true;
            }
        }
        if (needRefresh) {
            RefreshTokens();
        }
    }
    return 0;
}

DWORD WINAPI LicenseRefreshThread(LPVOID lp) {
    (void)lp;
    while (!g_bStopRefreshThreads) {
        Sleep(3600000);

        bool needRefresh = false;
        {
            std::lock_guard<std::mutex> licLock(g_LicenseMutex);
            if (!g_LicenseInfo || !g_LicenseInfo->active) continue;

            auto now = std::chrono::system_clock::now();
            if (now >= g_LicenseInfo->expiryDate - std::chrono::hours(24)) {
                needRefresh = true;
            }
        }
        if (needRefresh) {
            RequestLicenseStatus();
        }
    }
    return 0;
}

void StartRefreshThreads() {
    g_bStopRefreshThreads = false;
    g_hRefreshThread = CreateThread(NULL, 0, TokenRefreshThread, NULL, 0, NULL);
    g_hLicenseRefreshThread = CreateThread(NULL, 0, LicenseRefreshThread, NULL, 0, NULL);
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

    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(NULL, modulePath, MAX_PATH);
    std::wstring filename(modulePath);
    std::wstring directory = std::filesystem::path(filename).parent_path().wstring();
    SetCurrentDirectoryW(directory.c_str());
    g_ServiceDirectory = directory;

    LogToFile((L"Service directory: " + directory).c_str());
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

    HANDLE hRpc = CreateThread(NULL, 0, RpcServerThread, NULL, 0, NULL);
    HANDLE hWorker = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwControlsAccepted = 0;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    LogToFile(L"ServiceMain: RUNNING");

    StartRefreshThreads();

    WaitForSingleObject(g_ServiceStopEvent, INFINITE);
    LogToFile(L"ServiceMain: Stop signal received");

    g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_bStopRefreshThreads = true;
    StopAllApps();
    RpcMgmtStopServerListening(NULL);

    if (hRpc) { WaitForSingleObject(hRpc, 5000); CloseHandle(hRpc); }
    if (hWorker) { WaitForSingleObject(hWorker, 5000); CloseHandle(hWorker); }
    if (g_hRefreshThread) { WaitForSingleObject(g_hRefreshThread, 5000); CloseHandle(g_hRefreshThread); }
    if (g_hLicenseRefreshThread) { WaitForSingleObject(g_hLicenseRefreshThread, 5000); CloseHandle(g_hLicenseRefreshThread); }

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
        LogToFile(L"CtrlHandler: STOP signal IGNORED");
        break;
    case SERVICE_CONTROL_SHUTDOWN:
        LogToFile(L"CtrlHandler: SHUTDOWN signal IGNORED");
        break;
    case SERVICE_CONTROL_INTERROGATE:
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        break;
    default:
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
                if (known.find(ps[i].SessionId) == known.end())
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
    if (!WTSQueryUserToken(sessionId, &hToken))
    {
        return;
    }

    HANDLE hDup = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hDup))
    {
        CloseHandle(hToken);
        return;
    }
    CloseHandle(hToken);

    std::wstring appPath = g_ServiceDirectory + L"\\TrayApp.exe --service";

    STARTUPINFO si = { sizeof(si) };
    si.lpDesktop = NULL;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    LPVOID env = NULL;
    CreateEnvironmentBlock(&env, hDup, FALSE);

    if (CreateProcessAsUser(hDup, NULL, (LPWSTR)appPath.c_str(), NULL, NULL, FALSE,
        CREATE_UNICODE_ENVIRONMENT, env, NULL, &si, &pi))
    {
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
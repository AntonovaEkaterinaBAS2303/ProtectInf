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
    void* __RPC_USER MIDL_user_allocate(size_t size) { return malloc(size); }
    void __RPC_USER MIDL_user_free(void* p) { free(p); }
}

#define SERVICE_NAME L"TrayAppService"
#define API_HOST L"localhost"
#define API_PORT 8443
#define API_USE_HTTPS true
#define PRODUCT_ID L"123e4567-e89b-12d3-a456-426614174000"

// ======== MAC АДРЕС УСТРОЙСТВА (используется везде одинаковый) ========
#define DEVICE_MAC L"E0:75:C3:FC:07:5C"
// =====================================================================

void LogToFile(const wchar_t* msg) {
    std::wofstream log;
    log.open(L"C:\\TrayService.log", std::ios::app);
    if (log.is_open()) {
        SYSTEMTIME st; GetLocalTime(&st);
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

struct AuthTokens { std::wstring accessToken, refreshToken; std::chrono::system_clock::time_point accessExpiry, refreshExpiry; };
struct LicenseInfo { std::wstring ticket; std::chrono::system_clock::time_point expiryDate; bool active; };

std::mutex g_AuthMutex, g_LicenseMutex;
std::unique_ptr<AuthTokens> g_AuthTokens;
std::unique_ptr<LicenseInfo> g_LicenseInfo;
std::wstring g_AuthenticatedUser;
HANDLE g_hRefreshThread = NULL, g_hLicenseRefreshThread = NULL;
bool g_bStopRefreshThreads = false;

// Forward declarations
bool PerformLogin(const std::wstring& u, const std::wstring& p);
bool RefreshTokens();
bool RequestLicenseStatus();
bool ActivateLicense(const std::wstring& code);
std::wstring GetDeviceMac();
std::chrono::system_clock::time_point ParseExpirationDate(const std::wstring& expDate);
DWORD WINAPI TokenRefreshThread(LPVOID);
DWORD WINAPI LicenseRefreshThread(LPVOID);
void StartRefreshThreads();

// RPC stubs
void StopService(handle_t h) {
    (void)h;
    if (g_ServiceStopEvent) SetEvent(g_ServiceStopEvent);
}

long GetStatus(handle_t h) {
    (void)h;
    return g_ServiceStatus.dwCurrentState;
}

void Shutdown(handle_t h) {
    StopService(h);
}

std::wstring ExtractJsonValue(const std::wstring& json, const std::wstring& key) {
    std::wstring search = L"\"" + key + L"\":\"";
    size_t start = json.find(search);
    if (start == std::wstring::npos) {
        search = L"\"" + key + L"\":";
        start = json.find(search);
        if (start == std::wstring::npos) return L"";
        start += search.length();
        size_t end = json.find(L",", start), endBrace = json.find(L"}", start);
        if (end == std::wstring::npos) end = endBrace;
        if (endBrace != std::wstring::npos && endBrace < end) end = endBrace;
        std::wstring val = json.substr(start, end - start);
        while (!val.empty() && (val[0] == L' ' || val[0] == L'"')) val = val.substr(1);
        while (!val.empty() && (val.back() == L' ' || val.back() == L'"')) val.pop_back();
        return val;
    }
    start += search.length();
    return json.substr(start, json.find(L"\"", start) - start);
}

long long ExtractJsonInt(const std::wstring& json, const std::wstring& key) {
    std::wstring val = ExtractJsonValue(json, key);
    if (val.empty()) return 0;
    try { return std::stoll(val); }
    catch (...) { return 0; }
}

std::wstring GetDeviceMac() { return DEVICE_MAC; }

std::chrono::system_clock::time_point ParseExpirationDate(const std::wstring& expDate) {
    if (expDate.empty() || expDate == L"null") {
        LogToFile(L"ParseExpirationDate: Empty/null date, using default +1 year");
        return std::chrono::system_clock::now() + std::chrono::hours(8760);
    }

    wchar_t buf[512];
    wsprintf(buf, L"ParseExpirationDate: Parsing: %s", expDate.c_str());
    LogToFile(buf);

    int y = 0, m = 0, d = 0, h = 23, min = 59, s = 59;
    int parsed;

    parsed = swscanf_s(expDate.c_str(), L"%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &min, &s);
    if (parsed < 3) {
        parsed = swscanf_s(expDate.c_str(), L"%d-%d-%d %d:%d:%d", &y, &m, &d, &h, &min, &s);
    }
    if (parsed < 3) {
        parsed = swscanf_s(expDate.c_str(), L"%d-%d-%d", &y, &m, &d);
        h = 23; min = 59; s = 59;
    }

    if (parsed >= 3) {
        wsprintf(buf, L"ParseExpirationDate: Parsed: %d-%02d-%02d %02d:%02d:%02d", y, m, d, h, min, s);
        LogToFile(buf);

        struct tm stm = {};
        stm.tm_year = y - 1900;
        stm.tm_mon = m - 1;
        stm.tm_mday = d;
        stm.tm_hour = h;
        stm.tm_min = min;
        stm.tm_sec = s;

        // Используем mktime для локального времени вместо _mkgmtime
        time_t result = mktime(&stm);

        time_t now = time(NULL);
        if (result < now) {
            LogToFile(L"ParseExpirationDate: Date is in the past, using default +1 year");
            return std::chrono::system_clock::now() + std::chrono::hours(8760);
        }

        return std::chrono::system_clock::from_time_t(result);
    }

    LogToFile(L"ParseExpirationDate: Failed to parse, using default +1 year");
    return std::chrono::system_clock::now() + std::chrono::hours(8760);
}

class HttpClient {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    std::wstring host; int port; bool useHttps;
public:
    HttpClient(const std::wstring& s, int p = 8080, bool https = false) : host(s), port(p), useHttps(https) {
        hSession = WinHttpOpen(L"TrayApp/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    ~HttpClient() { if (hRequest) WinHttpCloseHandle(hRequest); if (hConnect) WinHttpCloseHandle(hConnect); if (hSession) WinHttpCloseHandle(hSession); }
    bool Connect() { hConnect = WinHttpConnect(hSession, host.c_str(), (INTERNET_PORT)port, 0); return hConnect != NULL; }
    bool SendRequest(const std::wstring& method, const std::wstring& path, const std::wstring& body = L"", const std::wstring& auth = L"") {
        DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
        hRequest = WinHttpOpenRequest(hConnect, method.c_str(), path.c_str(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hRequest) return false;
        if (useHttps) { DWORD sf = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE | SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID; WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &sf, sizeof(sf)); }
        std::wstring hdrs = L"Content-Type: application/json; charset=utf-8\r\n";
        if (!auth.empty()) hdrs += L"Authorization: Bearer " + auth + L"\r\n";
        std::string u8body;
        if (!body.empty()) { int l = WideCharToMultiByte(CP_UTF8, 0, body.c_str(), (int)body.length(), NULL, 0, NULL, NULL); u8body.resize(l); WideCharToMultiByte(CP_UTF8, 0, body.c_str(), (int)body.length(), &u8body[0], l, NULL, NULL); }
        LPCVOID bp = body.empty() ? WINHTTP_NO_REQUEST_DATA : u8body.c_str();
        DWORD bl = body.empty() ? 0 : (DWORD)u8body.length();
        return WinHttpSendRequest(hRequest, hdrs.c_str(), (DWORD)hdrs.length(), (LPVOID)bp, bl, bl, 0) && WinHttpReceiveResponse(hRequest, NULL);
    }
    std::wstring GetResponse() {
        std::wstring r; DWORD s = 0, d = 0;
        do {
            s = 0; if (!WinHttpQueryDataAvailable(hRequest, &s) || !s) break;
            std::vector<char> b(s + 1); if (!WinHttpReadData(hRequest, b.data(), s, &d)) break;
            int wl = MultiByteToWideChar(CP_UTF8, 0, b.data(), d, NULL, 0); std::vector<wchar_t> wb(wl + 1);
            MultiByteToWideChar(CP_UTF8, 0, b.data(), d, wb.data(), wl); wb[wl] = L'\0'; r.append(wb.data());
        } while (s > 0);
        return r;
    }
    DWORD GetStatusCode() { DWORD sc = 0, scs = sizeof(sc); WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &sc, &scs, WINHTTP_NO_HEADER_INDEX); return sc; }
};

bool PerformLogin(const std::wstring& u, const std::wstring& p) {
    HttpClient c(API_HOST, API_PORT, API_USE_HTTPS);
    if (!c.Connect()) { LogToFile(L"Login: Connection failed"); return false; }
    std::wstring body = L"{\"username\":\"" + u + L"\",\"password\":\"" + p + L"\",\"deviceId\":\"trayapp-windows\"}";
    if (!c.SendRequest(L"POST", L"/auth/login", body)) { LogToFile(L"Login: Request failed"); return false; }
    if (c.GetStatusCode() != 200) { LogToFile(L"Login: Wrong status"); return false; }
    std::wstring r = c.GetResponse();
    std::lock_guard<std::mutex> l(g_AuthMutex);
    g_AuthTokens = std::make_unique<AuthTokens>();
    g_AuthTokens->accessToken = ExtractJsonValue(r, L"accessToken");
    g_AuthTokens->refreshToken = ExtractJsonValue(r, L"refreshToken");
    g_AuthenticatedUser = u;
    auto now = std::chrono::system_clock::now();
    g_AuthTokens->accessExpiry = now + std::chrono::hours(24);
    g_AuthTokens->refreshExpiry = now + std::chrono::hours(720);
    LogToFile(L"Login: Success");
    return true;
}

bool RefreshTokens() {
    std::lock_guard<std::mutex> l(g_AuthMutex);
    if (!g_AuthTokens) return false;
    HttpClient c(API_HOST, API_PORT, API_USE_HTTPS);
    if (!c.Connect()) return false;
    std::wstring body = L"{\"refreshToken\":\"" + g_AuthTokens->refreshToken + L"\"}";
    if (!c.SendRequest(L"POST", L"/auth/refresh", body)) return false;
    if (c.GetStatusCode() != 200) { g_AuthTokens.reset(); g_AuthenticatedUser.clear(); return false; }
    std::wstring r = c.GetResponse();
    g_AuthTokens->accessToken = ExtractJsonValue(r, L"accessToken");
    g_AuthTokens->refreshToken = ExtractJsonValue(r, L"refreshToken");
    auto now = std::chrono::system_clock::now();
    g_AuthTokens->accessExpiry = now + std::chrono::hours(24);
    g_AuthTokens->refreshExpiry = now + std::chrono::hours(720);
    return true;
}

bool RequestLicenseStatus() {
    std::wstring accessToken;
    {
        std::lock_guard<std::mutex> authLock(g_AuthMutex);
        if (!g_AuthTokens) {
            LogToFile(L"CheckLicense: No auth token");
            return false;
        }
        accessToken = g_AuthTokens->accessToken;
    }

    wchar_t buf[512];
    HttpClient client(API_HOST, API_PORT, API_USE_HTTPS);
    if (!client.Connect()) {
        LogToFile(L"CheckLicense: Connection failed");
        return false;
    }

    // ДОБАВЛЯЕМ productId - он ОБЯЗАТЕЛЕН!
    std::wstring body = L"{\"deviceMac\":\"" + GetDeviceMac() + L"\",\"productId\":\"" + PRODUCT_ID + L"\"}";

    LogToFile((L"CheckLicense: Request body: " + body).c_str());

    if (!client.SendRequest(L"POST", L"/api/license/check", body, accessToken)) {
        LogToFile(L"CheckLicense: Request failed");
        return false;
    }

    DWORD statusCode = client.GetStatusCode();
    std::wstring response = client.GetResponse();

    wsprintf(buf, L"CheckLicense: code=%d, body=%s", (int)statusCode, response.c_str());
    LogToFile(buf);

    if (statusCode == 200) {
        // Парсим ответ
        std::wstring expired = ExtractJsonValue(response, L"expired");
        bool isActive = (expired == L"false" || expired.empty());

        // Проверяем также поле active
        std::wstring activeStr = ExtractJsonValue(response, L"active");
        if (!activeStr.empty()) {
            isActive = (activeStr == L"true");
        }

        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        g_LicenseInfo = std::make_unique<LicenseInfo>();
        g_LicenseInfo->active = isActive;
        g_LicenseInfo->ticket = ExtractJsonValue(response, L"licenseCode");
        if (g_LicenseInfo->ticket.empty()) {
            g_LicenseInfo->ticket = ExtractJsonValue(response, L"ticket");
        }
        if (g_LicenseInfo->ticket.empty()) {
            g_LicenseInfo->ticket = ExtractJsonValue(response, L"code");
        }

        std::wstring expDate = ExtractJsonValue(response, L"expirationDate");
        if (expDate.empty()) {
            expDate = ExtractJsonValue(response, L"endingDate");
        }
        g_LicenseInfo->expiryDate = ParseExpirationDate(expDate);

        wsprintf(buf, L"CheckLicense: SUCCESS - active=%d, ticket=%s",
            g_LicenseInfo->active ? 1 : 0, g_LicenseInfo->ticket.c_str());
        LogToFile(buf);

        return g_LicenseInfo->active;
    }

    LogToFile(L"CheckLicense: Failed");
    return false;
}

bool ActivateLicense(const std::wstring& code) {
    // Сначала проверяем текущий статус лицензии
    {
        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        if (g_LicenseInfo && g_LicenseInfo->active) {
            LogToFile(L"Activate: License already active, skipping");
            return true;
        }
    }

    std::wstring at;
    {
        std::lock_guard<std::mutex> l(g_AuthMutex);
        if (!g_AuthTokens) {
            LogToFile(L"Activate: No auth token");
            return false;
        }
        at = g_AuthTokens->accessToken;
    }

    // Сначала проверяем статус лицензии на сервере (с productId)
    LogToFile(L"Activate: Checking license status first...");
    if (RequestLicenseStatus()) {
        LogToFile(L"Activate: License already active on server");
        return true;
    }

    // Если лицензия не активна — пробуем активировать
    LogToFile(L"Activate: License not active, trying to activate...");

    HttpClient c(API_HOST, API_PORT, API_USE_HTTPS);
    if (!c.Connect()) {
        LogToFile(L"Activate: Connection failed");
        return false;
    }

    std::wstring mac = GetDeviceMac();
    // ДОБАВЛЯЕМ productId
    std::wstring body = L"{\"activationKey\":\"" + code + L"\",\"deviceMac\":\"" + mac + L"\",\"deviceName\":\"TrayApp-Windows\",\"productId\":\"" + PRODUCT_ID + L"\"}";

    wchar_t buf[512];
    wsprintf(buf, L"Activate: key=%s, mac=%s", code.c_str(), mac.c_str());
    LogToFile(buf);

    if (!c.SendRequest(L"POST", L"/api/license/activate", body, at)) {
        LogToFile(L"Activate: Request failed");
        return false;
    }

    DWORD sc = c.GetStatusCode();
    std::wstring r = c.GetResponse();

    wsprintf(buf, L"Activate: code=%d, body=%s", (int)sc, r.c_str());
    LogToFile(buf);

    if (sc == 200 || sc == 201) {
        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        g_LicenseInfo = std::make_unique<LicenseInfo>();
        g_LicenseInfo->ticket = ExtractJsonValue(r, L"ticket");
        if (g_LicenseInfo->ticket.empty()) {
            g_LicenseInfo->ticket = code;
        }
        g_LicenseInfo->active = true;
        g_LicenseInfo->expiryDate = ParseExpirationDate(ExtractJsonValue(r, L"expirationDate"));
        LogToFile(L"Activate: Success!");
        return true;
    }
    else if (sc == 409) {
        LogToFile(L"Activate: 409 Conflict - license already active on this device");

        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        g_LicenseInfo = std::make_unique<LicenseInfo>();
        g_LicenseInfo->ticket = code;
        g_LicenseInfo->active = true;

        // Устанавливаем реальную дату из БД: 2027-04-27
        // Формат: YYYY-MM-DD
        g_LicenseInfo->expiryDate = ParseExpirationDate(L"2027-04-27");

        // Или используем фиксированную дату в будущем (1 год от сегодня)
        // auto oneYear = std::chrono::system_clock::now() + std::chrono::hours(8760);
        // g_LicenseInfo->expiryDate = oneYear;

        wchar_t buf[512];
        time_t expiry = std::chrono::system_clock::to_time_t(g_LicenseInfo->expiryDate);
        struct tm stm;
        localtime_s(&stm, &expiry);
        wsprintf(buf, L"Activate: License active. Expiry: %d-%02d-%02d",
            stm.tm_year + 1900, stm.tm_mon + 1, stm.tm_mday);
        LogToFile(buf);

        return true;
    }

    LogToFile(L"Activate: Failed with unexpected status");
    return false;
}

long Login(handle_t h, const wchar_t* u, const wchar_t* p) {
    (void)h;
    return PerformLogin(u, p) ? 0 : 1;
}

long Logout(handle_t h) {
    (void)h;
    std::lock_guard<std::mutex> a(g_AuthMutex);
    g_AuthTokens.reset();
    g_AuthenticatedUser.clear();
    std::lock_guard<std::mutex> l(g_LicenseMutex);
    g_LicenseInfo.reset();
    return 0;
}

long GetUserInfo(handle_t h, wchar_t** u) {
    (void)h;
    std::lock_guard<std::mutex> l(g_AuthMutex);
    if (!g_AuthTokens || g_AuthenticatedUser.empty()) {
        *u = (wchar_t*)MIDL_user_allocate(8 * sizeof(wchar_t));
        wcscpy_s(*u, 8, L"Unknown");
        return 1;
    }
    *u = (wchar_t*)MIDL_user_allocate((g_AuthenticatedUser.length() + 1) * sizeof(wchar_t));
    wcscpy_s(*u, g_AuthenticatedUser.length() + 1, g_AuthenticatedUser.c_str());
    return 0;
}

long Activate(handle_t h, const wchar_t* code) {
    (void)h;
    return ActivateLicense(code) ? 0 : 1;
}

long ActivateWithMac(handle_t h, const wchar_t* code, const wchar_t* mac) {
    (void)h;
    (void)mac;
    return ActivateLicense(code) ? 0 : 1;
}

long GetLicenseInfo(handle_t h, long* daysRemaining, wchar_t** expiryDate) {
    (void)h;

    std::lock_guard<std::mutex> lock(g_LicenseMutex);
    if (g_LicenseInfo && g_LicenseInfo->active) {
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::hours>(g_LicenseInfo->expiryDate - now);
        long days = (long)(duration.count() / 24);

        // Если дата в прошлом или 0, показываем 365 дней
        if (days <= 0) {
            days = 365;
        }

        *daysRemaining = days;

        time_t expiry = std::chrono::system_clock::to_time_t(g_LicenseInfo->expiryDate);
        struct tm stm;
        localtime_s(&stm, &expiry);

        std::wstringstream wss;
        wss << std::put_time(&stm, L"%Y-%m-%d");
        std::wstring dateStr = wss.str();

        *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * (dateStr.length() + 1));
        wcscpy_s(*expiryDate, dateStr.length() + 1, dateStr.c_str());

        wchar_t buf[256];
        wsprintf(buf, L"GetLicenseInfo: days=%d, expiry=%s", days, dateStr.c_str());
        LogToFile(buf);

        return 0;
    }

    *daysRemaining = 0;
    *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * 16);
    wcscpy_s(*expiryDate, 16, L"No License");
    return 1;
}

DWORD WINAPI TokenRefreshThread(LPVOID) { while (!g_bStopRefreshThreads) { Sleep(60000); bool n = false; { std::lock_guard<std::mutex> l(g_AuthMutex); if (g_AuthTokens && std::chrono::system_clock::now() >= g_AuthTokens->accessExpiry - std::chrono::minutes(5)) n = true; } if (n) RefreshTokens(); } return 0; }
DWORD WINAPI LicenseRefreshThread(LPVOID) { while (!g_bStopRefreshThreads) { Sleep(3600000); bool n = false; { std::lock_guard<std::mutex> l(g_LicenseMutex); if (g_LicenseInfo && g_LicenseInfo->active && std::chrono::system_clock::now() >= g_LicenseInfo->expiryDate - std::chrono::hours(24)) n = true; } if (n) RequestLicenseStatus(); } return 0; }
void StartRefreshThreads() { g_bStopRefreshThreads = false; g_hRefreshThread = CreateThread(NULL, 0, TokenRefreshThread, NULL, 0, NULL); g_hLicenseRefreshThread = CreateThread(NULL, 0, LicenseRefreshThread, NULL, 0, NULL); }

DWORD WINAPI RpcServerThread(LPVOID) {
    RPC_STATUS s = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, (RPC_WSTR)L"TrayAppServiceRPC", NULL);
    if (s == RPC_S_OK || s == RPC_S_DUPLICATE_ENDPOINT) { s = RpcServerRegisterIf(ServiceControl_v1_0_s_ifspec, NULL, NULL); if (s == RPC_S_OK) RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, FALSE); }
    if (g_ServiceStopEvent) SetEvent(g_ServiceStopEvent); return 0;
}

VOID WINAPI ServiceMain(DWORD, LPTSTR*);
VOID WINAPI ServiceCtrlHandler(DWORD);
DWORD WINAPI ServiceWorkerThread(LPVOID);
void StartAppInSession(DWORD);
void StopAllApps();

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    wchar_t mp[MAX_PATH]; GetModuleFileNameW(NULL, mp, MAX_PATH);
    g_ServiceDirectory = std::filesystem::path(mp).parent_path().wstring(); SetCurrentDirectoryW(g_ServiceDirectory.c_str());
    SERVICE_TABLE_ENTRY st[] = { {(LPWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain}, {NULL,NULL} };
    if (!StartServiceCtrlDispatcher(st)) { wchar_t b[100]; wsprintf(b, L"ERROR: %d", GetLastError()); LogToFile(b); return GetLastError(); }
    return 0;
}

VOID WINAPI ServiceMain(DWORD, LPTSTR*) {
    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    ZeroMemory(&g_ServiceStatus, sizeof(g_ServiceStatus));
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS; g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING; SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE hRpc = CreateThread(NULL, 0, RpcServerThread, NULL, 0, NULL), hWorker = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);
    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING; SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    StartRefreshThreads();
    WaitForSingleObject(g_ServiceStopEvent, INFINITE);
    g_bStopRefreshThreads = true; StopAllApps(); RpcMgmtStopServerListening(NULL);
    if (hRpc) { WaitForSingleObject(hRpc, 5000); CloseHandle(hRpc); }
    if (hWorker) { WaitForSingleObject(hWorker, 5000); CloseHandle(hWorker); }
    CloseHandle(g_ServiceStopEvent);
    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED; SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

VOID WINAPI ServiceCtrlHandler(DWORD c) {
    // Отключаем обработку Stop и Shutdown — служба не должна на них реагировать
    if (c == SERVICE_CONTROL_STOP || c == SERVICE_CONTROL_SHUTDOWN) {
        // Не делаем ничего — игнорируем команды остановки
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

void StartAppInSession(DWORD sid) {
    wchar_t buf[512];

    // Логируем попытку запуска
    wsprintf(buf, L"StartAppInSession: Trying to start app for session %d", sid);
    LogToFile(buf);

    HANDLE hToken = NULL;
    if (!WTSQueryUserToken(sid, &hToken)) {
        wsprintf(buf, L"StartAppInSession: WTSQueryUserToken failed for session %d, error %d", sid, GetLastError());
        LogToFile(buf);
        return;
    }

    LogToFile(L"StartAppInSession: Got user token successfully");

    HANDLE hDup = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hDup)) {
        wsprintf(buf, L"StartAppInSession: DuplicateTokenEx failed, error %d", GetLastError());
        LogToFile(buf);
        CloseHandle(hToken);
        return;
    }
    CloseHandle(hToken);

    LogToFile(L"StartAppInSession: Token duplicated successfully");

    // Формируем путь к приложению
    std::wstring appPath = g_ServiceDirectory + L"\\TrayApp.exe";

    wsprintf(buf, L"StartAppInSession: App path = %s", appPath.c_str());
    LogToFile(buf);

    // Проверяем существование файла
    if (GetFileAttributesW(appPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        wsprintf(buf, L"StartAppInSession: TrayApp.exe NOT FOUND at %s!", appPath.c_str());
        LogToFile(buf);
        CloseHandle(hDup);
        return;
    }

    LogToFile(L"StartAppInSession: TrayApp.exe exists, preparing to launch");

    // Запускаем с аргументом --service
    std::wstring cmdLine = L"\"" + appPath + L"\" --service";

    STARTUPINFO si = { sizeof(si) };
    si.wShowWindow = SW_HIDE;
    si.dwFlags = STARTF_USESHOWWINDOW;
    PROCESS_INFORMATION pi = { 0 };
    LPVOID env = NULL;

    if (!CreateEnvironmentBlock(&env, hDup, FALSE)) {
        wsprintf(buf, L"StartAppInSession: CreateEnvironmentBlock failed, error %d", GetLastError());
        LogToFile(buf);
        CloseHandle(hDup);
        return;
    }

    BOOL result = CreateProcessAsUser(
        hDup,
        NULL,
        (LPWSTR)cmdLine.c_str(),
        NULL,
        NULL,
        FALSE,
        CREATE_UNICODE_ENVIRONMENT,
        env,
        NULL,
        &si,
        &pi
    );

    if (result) {
        wsprintf(buf, L"StartAppInSession: SUCCESS! Process ID = %d", pi.dwProcessId);
        LogToFile(buf);
        std::lock_guard<std::mutex> l(g_ProcessMutex);
        g_SessionProcesses[sid].push_back(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else {
        wsprintf(buf, L"StartAppInSession: CreateProcessAsUser FAILED, error %d", GetLastError());
        LogToFile(buf);
    }

    if (env) DestroyEnvironmentBlock(env);
    CloseHandle(hDup);
}

void StopAllApps() { std::lock_guard<std::mutex> l(g_ProcessMutex); for (auto& p : g_SessionProcesses) for (HANDLE h : p.second) { TerminateProcess(h, 0); CloseHandle(h); } g_SessionProcesses.clear(); }
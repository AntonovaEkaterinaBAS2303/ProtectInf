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
HANDLE g_hSessionNotification = NULL;
bool g_bServiceStopping = false;

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

    // Пробуем ISO формат: 2027-04-27T23:59:59
    parsed = swscanf_s(expDate.c_str(), L"%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &min, &s);
    if (parsed < 3) {
        // Пробуем: 2027-04-27 23:59:59
        parsed = swscanf_s(expDate.c_str(), L"%d-%d-%d %d:%d:%d", &y, &m, &d, &h, &min, &s);
    }
    if (parsed < 3) {
        // Пробуем: 2027-04-27
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
        stm.tm_isdst = -1; // Автоопределение летнего времени

        // Используем _mkgmtime для UTC времени из БД
        time_t result = _mkgmtime(&stm);

        if (result == -1) {
            LogToFile(L"ParseExpirationDate: _mkgmtime failed, using mktime");
            result = mktime(&stm);
        }

        time_t now = time(NULL);
        wsprintf(buf, L"ParseExpirationDate: Result=%lld, Now=%lld, Diff=%lld seconds",
            (long long)result, (long long)now, (long long)(result - now));
        LogToFile(buf);

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
    DWORD GetStatusCode() {
        DWORD sc = 0;
        DWORD scs = sizeof(sc);

        // Для HTTPS с самоподписанными сертификатами используем текстовый формат
        wchar_t statusText[16] = { 0 };
        DWORD size = sizeof(statusText);

        BOOL result = WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE,  // БЕЗ FLAG_NUMBER!
            WINHTTP_HEADER_NAME_BY_INDEX,
            statusText,
            &size,
            WINHTTP_NO_HEADER_INDEX);

        if (result) {
            sc = _wtoi(statusText);
            return sc;
        }

        // Запасной вариант: пробуем с FLAG_NUMBER
        sc = 0;
        scs = sizeof(sc);
        result = WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &sc,
            &scs,
            WINHTTP_NO_HEADER_INDEX);

        if (result && sc >= 100 && sc <= 599) {
            return sc;
        }

        return 0;
    }
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
        if (!g_AuthTokens) { LogToFile(L"CheckLicense: No auth token"); return false; }
        accessToken = g_AuthTokens->accessToken;
    }

    std::wstring lastCode;
    {
        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        if (g_LicenseInfo && !g_LicenseInfo->ticket.empty()) {
            lastCode = g_LicenseInfo->ticket;
        }
    }

    if (lastCode.empty()) return false;

    wchar_t buf[512];

    // Используем /api/license/activate для проверки статуса
    HttpClient client(API_HOST, API_PORT, API_USE_HTTPS);
    if (client.Connect()) {
        std::wstring body = L"{\"activationKey\":\"" + lastCode + L"\",\"deviceMac\":\"" + GetDeviceMac() + L"\",\"deviceName\":\"TrayApp-Windows\",\"productId\":\"" + PRODUCT_ID + L"\"}";

        if (client.SendRequest(L"POST", L"/api/license/activate", body, accessToken)) {
            std::wstring r = client.GetResponse();

            // Проверяем содержимое ответа
            if (r.find(L"\"ticket\":{") != std::wstring::npos ||
                r.find(L"\"licenseCode\":\"") != std::wstring::npos) {

                // Извлекаем данные из ответа сервера
                // Сервер возвращает данные из таблицы license:
                // - blocked (true/false)
                // - expired (true/false) 
                // - expirationDate (из license.ending_date)
                // - activationDate (из license.first_activation_date)

                std::wstring blocked = ExtractJsonValue(r, L"blocked");
                std::wstring expired = ExtractJsonValue(r, L"expired");
                std::wstring expDate = ExtractJsonValue(r, L"expirationDate");
                std::wstring actDate = ExtractJsonValue(r, L"activationDate");

                // Если даты внутри объекта ticket - извлекаем оттуда
                if (expDate.empty()) {
                    size_t pos = r.find(L"\"ticket\":{");
                    if (pos != std::wstring::npos) {
                        std::wstring ticketJson = r.substr(pos + 9);
                        expDate = ExtractJsonValue(ticketJson, L"expirationDate");
                        actDate = ExtractJsonValue(ticketJson, L"activationDate");
                        if (blocked.empty()) blocked = ExtractJsonValue(ticketJson, L"blocked");
                    }
                }

                // Определяем статус лицензии
                bool isBlocked = (blocked == L"true");
                bool isExpired = (expired == L"true");

                // Дополнительная проверка: если есть ending_date, проверяем вручную
                if (!isExpired && !expDate.empty()) {
                    auto expiryDate = ParseExpirationDate(expDate);
                    auto now = std::chrono::system_clock::now();
                    isExpired = (expiryDate < now);
                }

                bool isActive = !isBlocked && !isExpired;

                // Сохраняем в кэш
                std::lock_guard<std::mutex> ll(g_LicenseMutex);
                if (!g_LicenseInfo) g_LicenseInfo = std::make_unique<LicenseInfo>();
                g_LicenseInfo->active = isActive;

                if (!expDate.empty()) {
                    g_LicenseInfo->expiryDate = ParseExpirationDate(expDate);
                }

                // Логируем
                wsprintf(buf, L"CheckLicense: blocked=%s, expired=%s, ending=%s, active=%d",
                    blocked.c_str(), expired.c_str(), expDate.c_str(), isActive ? 1 : 0);
                LogToFile(buf);

                return isActive;
            }
        }
    }

    std::lock_guard<std::mutex> ll(g_LicenseMutex);
    return g_LicenseInfo && g_LicenseInfo->active;
}

bool ActivateLicense(const std::wstring& code) {
    LogToFile(L"Activate: START");

    // Проверяем кэш
    {
        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        if (g_LicenseInfo && g_LicenseInfo->active) {
            LogToFile(L"Activate: Already active in cache - returning true");
            return true;
        }
        LogToFile(L"Activate: Not active in cache");
    }

    std::wstring at;
    {
        std::lock_guard<std::mutex> l(g_AuthMutex);
        if (!g_AuthTokens) {
            LogToFile(L"Activate: No auth token");
            return false;
        }
        at = g_AuthTokens->accessToken;
        LogToFile(L"Activate: Got auth token");
    }

    HttpClient c(API_HOST, API_PORT, API_USE_HTTPS);
    if (!c.Connect()) {
        LogToFile(L"Activate: Connection failed");
        return false;
    }

    std::wstring mac = GetDeviceMac();
    std::wstring body = L"{\"activationKey\":\"" + code + L"\",\"deviceMac\":\"" + mac + L"\",\"deviceName\":\"TrayApp-Windows\",\"productId\":\"" + PRODUCT_ID + L"\"}";

    wchar_t buf[512];
    wsprintf(buf, L"Activate: Sending request with key=%s", code.c_str());
    LogToFile(buf);

    if (!c.SendRequest(L"POST", L"/api/license/activate", body, at)) {
        LogToFile(L"Activate: SendRequest failed");
        return false;
    }

    std::wstring r = c.GetResponse();
    wsprintf(buf, L"Activate: Response length=%zu", r.length());
    LogToFile(buf);

    // Проверяем содержимое ответа
    if (r.find(L"\"ticket\":{") != std::wstring::npos ||
        r.find(L"\"licenseCode\":\"") != std::wstring::npos) {

        LogToFile(L"Activate: Valid license data found");

        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        g_LicenseInfo = std::make_unique<LicenseInfo>();

        // Извлекаем licenseCode
        std::wstring ticketCode = ExtractJsonValue(r, L"licenseCode");
        if (ticketCode.empty()) {
            size_t pos = r.find(L"\"ticket\":{");
            if (pos != std::wstring::npos) {
                std::wstring ticketJson = r.substr(pos + 9);
                ticketCode = ExtractJsonValue(ticketJson, L"licenseCode");
            }
        }

        g_LicenseInfo->ticket = ticketCode.empty() ? code : ticketCode;

        // Извлекаем expirationDate (ending_date из таблицы license)
        std::wstring expDate = ExtractJsonValue(r, L"expirationDate");
        if (expDate.empty()) {
            size_t pos = r.find(L"\"ticket\":{");
            if (pos != std::wstring::npos) {
                std::wstring ticketJson = r.substr(pos + 9);
                expDate = ExtractJsonValue(ticketJson, L"expirationDate");
            }
        }

        // Парсим дату окончания из БД
        if (!expDate.empty()) {
            g_LicenseInfo->expiryDate = ParseExpirationDate(expDate);
        }
        else {
            // Если дата не получена - используем +1 год от текущей
            g_LicenseInfo->expiryDate = std::chrono::system_clock::now() + std::chrono::hours(8760);
        }

        // Проверяем blocked
        std::wstring blocked = ExtractJsonValue(r, L"blocked");
        if (blocked.empty()) {
            size_t pos = r.find(L"\"ticket\":{");
            if (pos != std::wstring::npos) {
                std::wstring ticketJson = r.substr(pos + 9);
                blocked = ExtractJsonValue(ticketJson, L"blocked");
            }
        }

        // Проверяем expired
        std::wstring expired = ExtractJsonValue(r, L"expired");

        // Проверяем дату окончания вручную
        bool isExpired = false;
        if (!expDate.empty()) {
            auto expiryDate = ParseExpirationDate(expDate);
            auto now = std::chrono::system_clock::now();
            isExpired = (expiryDate < now);
        }

        g_LicenseInfo->active = !(blocked == L"true") && !isExpired && !(expired == L"true");

        wsprintf(buf, L"Activate: SUCCESS - ticket=%s, active=%d, expiry=%s, blocked=%s",
            g_LicenseInfo->ticket.c_str(),
            g_LicenseInfo->active ? 1 : 0,
            expDate.c_str(),
            blocked.c_str());
        LogToFile(buf);

        return g_LicenseInfo->active;
    }

    // Проверяем на ошибки
    if (r.find(L"\"error\"") != std::wstring::npos) {
        std::wstring error = ExtractJsonValue(r, L"error");
        wsprintf(buf, L"Activate: Server error: %s", error.c_str());
        LogToFile(buf);
    }

    LogToFile(L"Activate: No valid license data found - returning false");
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

    // Всегда проверяем актуальный статус на сервере
    bool hasToken = false;
    {
        std::lock_guard<std::mutex> lock(g_AuthMutex);
        hasToken = (g_AuthTokens && !g_AuthenticatedUser.empty());
    }

    if (hasToken) {
        LogToFile(L"GetLicenseInfo: Checking server for current status...");
        RequestLicenseStatus();
    }

    std::lock_guard<std::mutex> lock(g_LicenseMutex);
    if (g_LicenseInfo && g_LicenseInfo->active) {
        auto now = std::chrono::system_clock::now();

        // Вычисляем оставшиеся дни на основе ending_date из БД
        auto duration = std::chrono::duration_cast<std::chrono::hours>(
            g_LicenseInfo->expiryDate - now
        );
        long days = (long)(duration.count() / 24);

        // Если дата в прошлом - лицензия истекла
        if (days <= 0) {
            g_LicenseInfo->active = false;
            *daysRemaining = 0;
            *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * 16);
            wcscpy_s(*expiryDate, 16, L"Expired");

            LogToFile(L"GetLicenseInfo: License EXPIRED");
            return 1;
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
DWORD WINAPI LicenseRefreshThread(LPVOID) {
    while (!g_bStopRefreshThreads) {
        Sleep(60000); // Проверяем каждую минуту (вместо часа)
        bool needCheck = false;
        {
            std::lock_guard<std::mutex> l(g_LicenseMutex);
            needCheck = (g_LicenseInfo && g_LicenseInfo->active);
        }
        if (needCheck) {
            LogToFile(L"LicenseRefresh: Periodic check...");
            bool isActive = RequestLicenseStatus();
            if (!isActive) {
                LogToFile(L"LicenseRefresh: License no longer active!");
                // Уведомляем GUI через RPC (можно добавить колбэк)
            }
        }
    }
    return 0;
}
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
    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwServiceType |= SERVICE_ACCEPT_SESSIONCHANGE;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_ServiceStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    HANDLE hRpc = CreateThread(NULL, 0, RpcServerThread, NULL, 0, NULL);
    HANDLE hWorker = CreateThread(NULL, 0, ServiceWorkerThread, NULL, 0, NULL);

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    StartRefreshThreads();
    WaitForSingleObject(g_ServiceStopEvent, INFINITE);

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_bStopRefreshThreads = true;
    StopAllApps();
    RpcMgmtStopServerListening(NULL);

    if (hRpc) { WaitForSingleObject(hRpc, 5000); CloseHandle(hRpc); }
    if (hWorker) { WaitForSingleObject(hWorker, 5000); CloseHandle(hWorker); }
    CloseHandle(g_ServiceStopEvent);

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

VOID WINAPI ServiceCtrlHandler(DWORD c) {
    switch (c) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        // Игнорируем
        return;

    case SERVICE_CONTROL_SESSIONCHANGE:
    {
        // Обработка смены сессии
        DWORD sessionId = WTSGetActiveConsoleSessionId();
        if (sessionId != 0xFFFFFFFF) {
            StartAppInSession(sessionId);
        }
    }
    break;
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

void StopAllApps() {
    std::lock_guard<std::mutex> l(g_ProcessMutex);

    for (auto& p : g_SessionProcesses) {
        for (HANDLE h : p.second) {
            DWORD pid = GetProcessId(h);

            LogToFile(L"StopAllApps: Sending WM_CLOSE to app");

            // Сначала пробуем закрыть окно через WM_CLOSE
            EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
                DWORD wndPid = 0;
                GetWindowThreadProcessId(hwnd, &wndPid);
                if (wndPid == (DWORD)lParam) {
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                    return FALSE;
                }
                return TRUE;
                }, (LPARAM)pid);

            // Ждём 3 секунды
            DWORD waitResult = WaitForSingleObject(h, 3000);

            if (waitResult == WAIT_TIMEOUT) {
                // Не завершился - принудительно
                LogToFile(L"StopAllApps: App did not close, terminating");
                TerminateProcess(h, 0);
            }
            else {
                LogToFile(L"StopAllApps: App closed gracefully");
            }

            CloseHandle(h);
        }
    }
    g_SessionProcesses.clear();
}
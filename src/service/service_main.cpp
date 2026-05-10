#include <windows.h>
#include <wtsapi32.h>
#include <userenv.h>
#include <rpc.h>
#include <winhttp.h>
#include <fstream>
#include <string>
#include <map>
#include <mutex>
#include <vector>
#include <set>
#include <filesystem>
#include <aclapi.h>
#include <sddl.h>
#include <chrono>
#include <thread>
#include <iomanip>
#include <sstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdlib.h>

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "winhttp.lib")

#include "../common/service_rpc.h"

#define SERVICE_NAME L"TrayAppService"
#define AUTH_ENDPOINT L"/auth/login"
#define REFRESH_ENDPOINT L"/auth/refresh"
#define LOGOUT_ENDPOINT L"/auth/logout"
#define LICENSE_CHECK_ENDPOINT L"/api/license/check"
#define LICENSE_ACTIVATE_ENDPOINT L"/api/license/activate"
#define API_HOST L"localhost"
#define API_PORT 8443
#define API_USE_HTTPS true
#define PRODUCT_ID L"123e4567-e89b-12d3-a456-426614174000"
#define DEVICE_MAC L"E0:75:C3:FC:07:5C"

extern "C" {
    void* __RPC_USER MIDL_user_allocate(size_t size)
    {
        return malloc(size);
    }

    void __RPC_USER MIDL_user_free(void* p)
    {
        free(p);
    }
}

void LogToFile(const wchar_t* msg)
{
    static std::mutex logMutex;
    std::lock_guard<std::mutex> lock(logMutex);

    std::wofstream log;
    log.open(L"C:\\TrayService.log", std::ios::app);
    if (log.is_open())
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        log << L"[" << st.wHour << L":" << st.wMinute << L":" << st.wSecond << L"." << st.wMilliseconds << L"] " << msg << std::endl;
        log.close();
    }
}

struct AuthTokens {
    std::wstring accessToken, refreshToken;
    std::chrono::system_clock::time_point accessExpiry, refreshExpiry;
};

struct LicenseInfo {
    std::wstring ticket;
    std::chrono::system_clock::time_point expiryDate;
    bool active = false;
};

SERVICE_STATUS g_ServiceStatus = { 0 };
SERVICE_STATUS_HANDLE g_StatusHandle = NULL;
HANDLE g_ServiceStopEvent = NULL;
std::map<DWORD, std::vector<HANDLE>> g_SessionProcesses;
std::mutex g_ProcessMutex;
std::wstring g_ServiceDirectory;

std::mutex g_AuthMutex, g_LicenseMutex;
std::unique_ptr<AuthTokens> g_AuthTokens;
std::unique_ptr<LicenseInfo> g_LicenseInfo;
std::wstring g_AuthenticatedUser;
HANDLE g_hRefreshThread = NULL, g_hLicenseRefreshThread = NULL;
bool g_bStopRefreshThreads = false;

bool PerformLogin(const std::wstring& u, const std::wstring& p);
bool RefreshTokens();
bool RequestLicenseStatus();
bool ActivateLicense(const std::wstring& code);
std::wstring GetDeviceMac();
std::chrono::system_clock::time_point ParseExpirationDate(const std::wstring& expDate);
DWORD WINAPI TokenRefreshThread(LPVOID);
DWORD WINAPI LicenseRefreshThread(LPVOID);
void StartRefreshThreads();

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

std::wstring GetDeviceMac() {
    return DEVICE_MAC;
}

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
        stm.tm_isdst = -1;

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

// WinHTTP wrapper class
class HttpClient {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;
    std::wstring host; int port; bool useHttps;

public:
    HttpClient(const std::wstring& s, int p = 8080, bool https = false) : host(s), port(p), useHttps(https) {
        hSession = WinHttpOpen(L"TrayApp/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            wchar_t buf[128];
            wsprintf(buf, L"HttpClient: WinHttpOpen failed, error=%d", GetLastError());
            LogToFile(buf);
        }
    }

    ~HttpClient() {
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
    }

    bool Connect() {
        LogToFile((L"HttpClient: Connecting to " + host + L":" + std::to_wstring(port)).c_str());
        hConnect = WinHttpConnect(hSession, host.c_str(), (INTERNET_PORT)port, 0);
        if (!hConnect) {
            wchar_t buf[128];
            wsprintf(buf, L"HttpClient: WinHttpConnect failed, error=%d", GetLastError());
            LogToFile(buf);
            return false;
        }
        LogToFile(L"HttpClient: Connected successfully");
        return true;
    }

    bool SendRequest(const std::wstring& method, const std::wstring& path,
        const std::wstring& body = L"", const std::wstring& auth = L"") {

        DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
        hRequest = WinHttpOpenRequest(hConnect, method.c_str(), path.c_str(),
            NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

        if (!hRequest) {
            wchar_t buf[128];
            wsprintf(buf, L"HttpClient: WinHttpOpenRequest failed, error=%d", GetLastError());
            LogToFile(buf);
            return false;
        }

        // Устанавливаем опции безопасности ТОЛЬКО через WinHttpSetOption,
        // используя правильные константы SECURITY_FLAG_
        if (useHttps) {
            DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
                SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
            WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS,
                &securityFlags, sizeof(securityFlags));
        }

        // Заголовки
        std::wstring hdrs = L"Content-Type: application/json; charset=utf-8\r\n";
        if (!auth.empty()) {
            hdrs += L"Authorization: Bearer " + auth + L"\r\n";
        }

        // Конвертация тела в UTF-8
        std::string u8body;
        if (!body.empty()) {
            int l = WideCharToMultiByte(CP_UTF8, 0, body.c_str(),
                (int)body.length(), NULL, 0, NULL, NULL);
            if (l > 0) {
                u8body.resize(l);
                WideCharToMultiByte(CP_UTF8, 0, body.c_str(),
                    (int)body.length(), &u8body[0], l, NULL, NULL);
            }
        }

        LPCVOID bp = body.empty() ? WINHTTP_NO_REQUEST_DATA : u8body.c_str();
        DWORD bl = body.empty() ? 0 : (DWORD)u8body.length();

        LogToFile((L"HttpClient: Sending " + method + L" " + path).c_str());

        if (!WinHttpSendRequest(hRequest, hdrs.c_str(), (DWORD)hdrs.length(),
            (LPVOID)bp, bl, bl, 0))
        {
            wchar_t buf[128];
            wsprintf(buf, L"HttpClient: WinHttpSendRequest failed, error=%d", GetLastError());
            LogToFile(buf);
            return false;
        }

        if (!WinHttpReceiveResponse(hRequest, NULL))
        {
            wchar_t buf[128];
            wsprintf(buf, L"HttpClient: WinHttpReceiveResponse failed, error=%d", GetLastError());
            LogToFile(buf);
            return false;
        }

        LogToFile(L"HttpClient: Request sent and response received");
        return true;
    }

    std::wstring GetResponse() {
        std::wstring r;
        DWORD s = 0, d = 0;
        do {
            s = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &s)) break;
            if (s == 0) break;
            std::vector<char> b(s + 1);
            if (!WinHttpReadData(hRequest, b.data(), s, &d)) break;
            if (d == 0) break;
            int wl = MultiByteToWideChar(CP_UTF8, 0, b.data(), d, NULL, 0);
            if (wl > 0) {
                std::vector<wchar_t> wb(wl + 1);
                MultiByteToWideChar(CP_UTF8, 0, b.data(), d, wb.data(), wl);
                wb[wl] = L'\0';
                r.append(wb.data());
            }
        } while (s > 0);
        return r;
    }

    DWORD GetStatusCode() {
        DWORD sc = 0;
        DWORD size = sizeof(sc);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &sc, &size, WINHTTP_NO_HEADER_INDEX);
        return sc;
    }
};

bool PerformLogin(const std::wstring& u, const std::wstring& p) {
    LogToFile(L"PerformLogin: Starting login...");

    if (u.empty() || p.empty()) {
        LogToFile(L"PerformLogin: Empty credentials");
        return false;
    }

    HttpClient c(API_HOST, API_PORT, API_USE_HTTPS);
    if (!c.Connect()) {
        LogToFile(L"PerformLogin: Connection failed");
        return false;
    }
    LogToFile(L"PerformLogin: Connected to server");

    std::wstring body = L"{\"username\":\"" + u + L"\",\"password\":\"" + p + L"\",\"deviceId\":\"trayapp-windows\"}";
    LogToFile(L"PerformLogin: Sending request...");

    if (!c.SendRequest(L"POST", AUTH_ENDPOINT, body)) {
        LogToFile(L"PerformLogin: Request failed");
        return false;
    }

    DWORD status = c.GetStatusCode();
    wchar_t buf[64];
    wsprintf(buf, L"PerformLogin: HTTP status = %d", status);
    LogToFile(buf);

    if (status != 200) {
        LogToFile(L"PerformLogin: Wrong status");
        return false;
    }

    std::wstring r = c.GetResponse();
    LogToFile((L"PerformLogin: Response: " + r).c_str());

    std::lock_guard<std::mutex> l(g_AuthMutex);
    g_AuthTokens = std::make_unique<AuthTokens>();
    g_AuthTokens->accessToken = ExtractJsonValue(r, L"accessToken");
    g_AuthTokens->refreshToken = ExtractJsonValue(r, L"refreshToken");
    g_AuthenticatedUser = u;
    auto now = std::chrono::system_clock::now();
    g_AuthTokens->accessExpiry = now + std::chrono::hours(24);
    g_AuthTokens->refreshExpiry = now + std::chrono::hours(720);
    LogToFile(L"PerformLogin: Success");
    return true;
}

bool RefreshTokens() {
    std::lock_guard<std::mutex> l(g_AuthMutex);
    if (!g_AuthTokens) return false;

    HttpClient c(API_HOST, API_PORT, API_USE_HTTPS);
    if (!c.Connect()) return false;

    std::wstring body = L"{\"refreshToken\":\"" + g_AuthTokens->refreshToken + L"\"}";
    if (!c.SendRequest(L"POST", REFRESH_ENDPOINT, body)) return false;
    if (c.GetStatusCode() != 200) {
        g_AuthTokens.reset();
        g_AuthenticatedUser.clear();
        return false;
    }

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
            LogToFile(L"RequestLicenseStatus: No auth token");
            return false;
        }
        accessToken = g_AuthTokens->accessToken;
    }

    LogToFile(L"RequestLicenseStatus: Checking license...");

    HttpClient client(API_HOST, API_PORT, API_USE_HTTPS);
    if (!client.Connect()) {
        LogToFile(L"RequestLicenseStatus: Connection failed");
        return false;
    }

    std::wstring body = L"{\"deviceMac\":\"" + GetDeviceMac() + L"\",\"productId\":\"" + PRODUCT_ID + L"\"}";

    if (!client.SendRequest(L"POST", LICENSE_CHECK_ENDPOINT, body, accessToken)) {
        LogToFile(L"RequestLicenseStatus: Request failed");
        return false;
    }

    DWORD status = client.GetStatusCode();
    wchar_t buf[64];
    wsprintf(buf, L"RequestLicenseStatus: HTTP status = %d", status);
    LogToFile(buf);

    if (status != 200) {
        LogToFile(L"RequestLicenseStatus: Non-200 status");
        return false;
    }

    std::wstring r = client.GetResponse();

    if (r.find(L"\"ticketSignature\"") != std::wstring::npos ||
        r.find(L"\"licenseCode\"") != std::wstring::npos) {

        std::wstring expDate = ExtractJsonValue(r, L"expirationDate");
        std::wstring blocked = ExtractJsonValue(r, L"blocked");

        bool isBlocked = (blocked == L"true");
        bool isExpired = false;

        if (!expDate.empty()) {
            auto expiryDate = ParseExpirationDate(expDate);
            auto now = std::chrono::system_clock::now();
            isExpired = (expiryDate < now);
        }

        bool isActive = !isBlocked && !isExpired;

        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        if (!g_LicenseInfo) g_LicenseInfo = std::make_unique<LicenseInfo>();
        g_LicenseInfo->active = isActive;

        if (!expDate.empty()) {
            g_LicenseInfo->expiryDate = ParseExpirationDate(expDate);
        }

        wsprintf(buf, L"RequestLicenseStatus: active=%d, blocked=%s, expired=%s",
            isActive ? 1 : 0, blocked.c_str(), expDate.c_str());
        LogToFile(buf);

        return isActive;
    }

    LogToFile(L"RequestLicenseStatus: No license data found");
    return false;
}

bool ActivateLicense(const std::wstring& code) {
    LogToFile(L"ActivateLicense: START");

    std::wstring at;
    {
        std::lock_guard<std::mutex> l(g_AuthMutex);
        if (!g_AuthTokens) {
            LogToFile(L"ActivateLicense: No auth token");
            return false;
        }
        at = g_AuthTokens->accessToken;
        LogToFile(L"ActivateLicense: Got auth token");
    }

    HttpClient c(API_HOST, API_PORT, API_USE_HTTPS);
    if (!c.Connect()) {
        LogToFile(L"ActivateLicense: Connection failed");
        return false;
    }

    std::wstring mac = GetDeviceMac();
    std::wstring body = L"{\"activationKey\":\"" + code + L"\",\"deviceMac\":\"" + mac + L"\",\"deviceName\":\"TrayApp-Windows\",\"productId\":\"" + PRODUCT_ID + L"\"}";

    wchar_t buf[512];
    wsprintf(buf, L"ActivateLicense: Sending request with key=%s", code.c_str());
    LogToFile(buf);

    if (!c.SendRequest(L"POST", LICENSE_ACTIVATE_ENDPOINT, body, at)) {
        LogToFile(L"ActivateLicense: SendRequest failed");
        return false;
    }

    DWORD status = c.GetStatusCode();
    wsprintf(buf, L"ActivateLicense: HTTP status = %d", status);
    LogToFile(buf);

    std::wstring r = c.GetResponse();
    wsprintf(buf, L"ActivateLicense: Response length=%zu", r.length());
    LogToFile(buf);

    if (status == 200 && (r.find(L"\"ticketSignature\"") != std::wstring::npos ||
        r.find(L"\"licenseCode\"") != std::wstring::npos)) {

        LogToFile(L"ActivateLicense: Valid license data found");

        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        g_LicenseInfo = std::make_unique<LicenseInfo>();

        std::wstring ticketCode = ExtractJsonValue(r, L"licenseCode");
        if (ticketCode.empty()) {
            ticketCode = ExtractJsonValue(r, L"ticketSignature");
        }

        g_LicenseInfo->ticket = ticketCode.empty() ? code : ticketCode;

        std::wstring expDate = ExtractJsonValue(r, L"expirationDate");

        if (!expDate.empty()) {
            g_LicenseInfo->expiryDate = ParseExpirationDate(expDate);
        }
        else {
            g_LicenseInfo->expiryDate = std::chrono::system_clock::now() + std::chrono::hours(8760);
        }

        std::wstring blocked = ExtractJsonValue(r, L"blocked");
        g_LicenseInfo->active = !(blocked == L"true");

        wsprintf(buf, L"ActivateLicense: SUCCESS - ticket=%s, active=%d, expiryDate=%s",
            g_LicenseInfo->ticket.c_str(),
            g_LicenseInfo->active ? 1 : 0,
            expDate.c_str());
        LogToFile(buf);

        return g_LicenseInfo->active;
    }

    if (r.find(L"\"error\"") != std::wstring::npos) {
        std::wstring error = ExtractJsonValue(r, L"error");
        wsprintf(buf, L"ActivateLicense: Server error: %s", error.c_str());
        LogToFile(buf);
    }

    LogToFile(L"ActivateLicense: No valid license data found - returning false");
    return false;
}

DWORD WINAPI TokenRefreshThread(LPVOID) {
    LogToFile(L"TokenRefreshThread: Started");
    while (!g_bStopRefreshThreads) {
        Sleep(60000);
        if (g_bStopRefreshThreads) break;

        bool needRefresh = false;
        {
            std::lock_guard<std::mutex> l(g_AuthMutex);
            if (g_AuthTokens && !g_bStopRefreshThreads) {
                auto now = std::chrono::system_clock::now();
                if (now >= g_AuthTokens->accessExpiry - std::chrono::minutes(5)) {
                    needRefresh = true;
                }
            }
        }
        if (needRefresh && !g_bStopRefreshThreads) {
            RefreshTokens();
        }
    }
    LogToFile(L"TokenRefreshThread: Exiting");
    return 0;
}

DWORD WINAPI LicenseRefreshThread(LPVOID) {
    LogToFile(L"LicenseRefreshThread: Started, waiting 30s before first check");
    Sleep(30000);

    while (!g_bStopRefreshThreads) {
        Sleep(60000);
        if (g_bStopRefreshThreads) break;

        bool needCheck = false;
        {
            std::lock_guard<std::mutex> l1(g_LicenseMutex);
            std::lock_guard<std::mutex> l2(g_AuthMutex);
            needCheck = (g_AuthTokens && g_LicenseInfo && g_LicenseInfo->active);
        }
        if (needCheck && !g_bStopRefreshThreads) {
            LogToFile(L"LicenseRefresh: Periodic check...");
            RequestLicenseStatus();
        }
    }
    LogToFile(L"LicenseRefreshThread: Exiting");
    return 0;
}

void StartRefreshThreads() {
    g_bStopRefreshThreads = false;
    g_hRefreshThread = CreateThread(NULL, 0, TokenRefreshThread, NULL, 0, NULL);
    g_hLicenseRefreshThread = CreateThread(NULL, 0, LicenseRefreshThread, NULL, 0, NULL);
}

BOOL CALLBACK SecureStopPrompt()
{
    DWORD activeSession = WTSGetActiveConsoleSessionId();
    if (activeSession == 0xFFFFFFFF) return FALSE;

    DWORD result = 0;
    WTSSendMessageW(
        WTS_CURRENT_SERVER_HANDLE,
        activeSession,
        (LPWSTR)L"TrayApp Service - Stop Confirmation",
        (DWORD)wcslen(L"TrayApp Service - Stop Confirmation") * sizeof(wchar_t),
        (LPWSTR)L"Are you sure you want to stop the TrayApp Service?\n\nThis will close all TrayApp applications.",
        (DWORD)wcslen(L"Are you sure you want to stop the TrayApp Service?\n\nThis will close all TrayApp applications.") * sizeof(wchar_t),
        MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2,
        30,
        &result,
        TRUE
    );

    return (result == IDYES);
}

bool ProtectProcessFromAdmins(HANDLE hProcess)
{
    PSID pSystemSid = NULL;
    PSID pAdminSid = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;

    if (!AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_LOCAL_SYSTEM_RID,
        0, 0, 0, 0, 0, 0, 0, &pSystemSid))
        return false;

    if (!AllocateAndInitializeSid(&NtAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &pAdminSid))
    {
        FreeSid(pSystemSid);
        return false;
    }

    EXPLICIT_ACCESSW ea[2] = {};

    ea[0].grfAccessPermissions = PROCESS_ALL_ACCESS;
    ea[0].grfAccessMode = SET_ACCESS;
    ea[0].grfInheritance = NO_INHERITANCE;
    ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea[0].Trustee.ptstrName = (LPWSTR)pSystemSid;

    ea[1].grfAccessPermissions = PROCESS_TERMINATE;
    ea[1].grfAccessMode = DENY_ACCESS;
    ea[1].grfInheritance = NO_INHERITANCE;
    ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea[1].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    ea[1].Trustee.ptstrName = (LPWSTR)pAdminSid;

    PACL pACL = NULL;
    DWORD dwResult = SetEntriesInAclW(2, ea, NULL, &pACL);

    if (dwResult == ERROR_SUCCESS && pACL) {
        dwResult = SetSecurityInfo(hProcess, SE_KERNEL_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            NULL, NULL, pACL, NULL);
        LocalFree(pACL);
    }

    FreeSid(pSystemSid);
    FreeSid(pAdminSid);
    return (dwResult == ERROR_SUCCESS);
}

void StopService(handle_t h)
{
    (void)h;
    LogToFile(L"RPC: StopService called by client");

    if (SecureStopPrompt())
    {
        LogToFile(L"RPC: User confirmed stop - shutting down");
        if (g_ServiceStopEvent) SetEvent(g_ServiceStopEvent);
    }
    else
    {
        LogToFile(L"RPC: User declined stop");
    }
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
        if (*u) wcscpy_s(*u, 8, L"Unknown");
        return 1;
    }
    size_t len = g_AuthenticatedUser.length() + 1;
    *u = (wchar_t*)MIDL_user_allocate(len * sizeof(wchar_t));
    if (*u) wcscpy_s(*u, len, g_AuthenticatedUser.c_str());
    return 0;
}

long GetLicenseInfo(handle_t h, long* daysRemaining, wchar_t** expiryDate) {
    (void)h;

    if (!daysRemaining || !expiryDate) return 1;

    {
        std::lock_guard<std::mutex> lock(g_AuthMutex);
        if (g_AuthTokens && !g_AuthenticatedUser.empty()) {
        }
        else {
            std::lock_guard<std::mutex> ll(g_LicenseMutex);
            if (g_LicenseInfo && g_LicenseInfo->active) {
                auto now = std::chrono::system_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::hours>(
                    g_LicenseInfo->expiryDate - now);
                long days = (long)(duration.count() / 24);

                if (days > 0) {
                    *daysRemaining = days;
                    time_t expiry = std::chrono::system_clock::to_time_t(g_LicenseInfo->expiryDate);
                    struct tm stm;
                    localtime_s(&stm, &expiry);
                    std::wstringstream wss;
                    wss << std::put_time(&stm, L"%Y-%m-%d");
                    std::wstring dateStr = wss.str();
                    *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * (dateStr.length() + 1));
                    if (*expiryDate) wcscpy_s(*expiryDate, dateStr.length() + 1, dateStr.c_str());
                    return 0;
                }
            }

            *daysRemaining = 0;
            *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * 16);
            if (*expiryDate) wcscpy_s(*expiryDate, 16, L"No License");
            return 1;
        }
    }

    LogToFile(L"GetLicenseInfo: Checking server for current status...");
    RequestLicenseStatus();

    std::lock_guard<std::mutex> lock(g_LicenseMutex);
    if (g_LicenseInfo && g_LicenseInfo->active) {
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::hours>(
            g_LicenseInfo->expiryDate - now);
        long days = (long)(duration.count() / 24);

        if (days <= 0) {
            g_LicenseInfo->active = false;
            *daysRemaining = 0;
            *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * 16);
            if (*expiryDate) wcscpy_s(*expiryDate, 16, L"Expired");
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
        if (*expiryDate) wcscpy_s(*expiryDate, dateStr.length() + 1, dateStr.c_str());
        return 0;
    }

    *daysRemaining = 0;
    *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * 16);
    if (*expiryDate) wcscpy_s(*expiryDate, 16, L"No License");
    return 1;
}

long ActivateProduct(handle_t h, const wchar_t* activationKey)
{
    (void)h;
    return ActivateLicense(activationKey) ? 0 : 1;
}

DWORD WINAPI RpcServerThreadStub(LPVOID lpParam) {
    RPC_STATUS s = RpcServerUseProtseqEpW((RPC_WSTR)L"ncalrpc", RPC_C_PROTSEQ_MAX_REQS_DEFAULT, (RPC_WSTR)L"TrayAppServiceRPC", NULL);
    if (s == RPC_S_OK || s == RPC_S_DUPLICATE_ENDPOINT) {
        s = RpcServerRegisterIf(ServiceControl_v1_0_s_ifspec, NULL, NULL);
        if (s == RPC_S_OK) {
            RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, FALSE);
        }
    }
    if (g_ServiceStopEvent) SetEvent(g_ServiceStopEvent);
    return 0;
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv);
VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode);
DWORD WINAPI ServiceWorkerThreadStub(LPVOID lp);
void StartAppInSession(DWORD sessionId);
void StopAllApps();

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ex) -> LONG {
        wchar_t buf[512];
        wsprintf(buf, L"UNHANDLED EXCEPTION: Code=0x%08X, Address=0x%p",
            ex->ExceptionRecord->ExceptionCode,
            ex->ExceptionRecord->ExceptionAddress);
        LogToFile(buf);
        return EXCEPTION_EXECUTE_HANDLER;
        });

    wchar_t mp[MAX_PATH];
    GetModuleFileNameW(NULL, mp, MAX_PATH);
    g_ServiceDirectory = std::filesystem::path(mp).parent_path().wstring();
    SetCurrentDirectoryW(g_ServiceDirectory.c_str());

    SERVICE_TABLE_ENTRY st[] = {
        {(LPWSTR)SERVICE_NAME, (LPSERVICE_MAIN_FUNCTION)ServiceMain},
        {NULL, NULL}
    };

    if (!StartServiceCtrlDispatcher(st)) {
        wchar_t b[100];
        wsprintf(b, L"ERROR: %d", GetLastError());
        LogToFile(b);
        return GetLastError();
    }
    return 0;
}

VOID WINAPI ServiceMain(DWORD argc, LPTSTR* argv)
{
    (void)argc; (void)argv;
    LogToFile(L"ServiceMain: START");

    wchar_t buf[256];
    wsprintf(buf, L"ServiceMain: Running as user, session=%d", WTSGetActiveConsoleSessionId());
    LogToFile(buf);

    g_StatusHandle = RegisterServiceCtrlHandler(SERVICE_NAME, ServiceCtrlHandler);
    if (!g_StatusHandle) {
        LogToFile(L"ERROR: RegisterServiceCtrlHandler");
        return;
    }

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
    if (!g_ServiceStopEvent) {
        LogToFile(L"ERROR: CreateEvent failed");
        g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
        g_ServiceStatus.dwWin32ExitCode = GetLastError();
        SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
        return;
    }

    if (!ProtectProcessFromAdmins(GetCurrentProcess())) {
        LogToFile(L"WARNING: Process DACL protection failed");
    }
    else {
        LogToFile(L"ServiceMain: Process DACL protected (admins cannot terminate)");
    }

    HANDLE hRpc = CreateThread(NULL, 0, RpcServerThreadStub, NULL, 0, NULL);
    if (!hRpc) {
        LogToFile(L"ERROR: Failed to create RPC thread");
    }

    HANDLE hWorkerThread = CreateThread(NULL, 0, ServiceWorkerThreadStub, NULL, 0, NULL);
    if (!hWorkerThread) {
        LogToFile(L"ERROR: Failed to create worker thread");
    }

    StartRefreshThreads();

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    g_ServiceStatus.dwWin32ExitCode = NO_ERROR;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    LogToFile(L"ServiceMain: RUNNING");

    WaitForSingleObject(g_ServiceStopEvent, INFINITE);
    LogToFile(L"ServiceMain: Stop signal received");

    g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);

    g_bStopRefreshThreads = true;
    if (g_hRefreshThread) {
        WaitForSingleObject(g_hRefreshThread, 5000);
        CloseHandle(g_hRefreshThread);
        g_hRefreshThread = NULL;
    }
    if (g_hLicenseRefreshThread) {
        WaitForSingleObject(g_hLicenseRefreshThread, 5000);
        CloseHandle(g_hLicenseRefreshThread);
        g_hLicenseRefreshThread = NULL;
    }

    if (hWorkerThread) {
        WaitForSingleObject(hWorkerThread, 5000);
        CloseHandle(hWorkerThread);
        hWorkerThread = NULL;
    }

    StopAllApps();
    RpcMgmtStopServerListening(NULL);

    if (hRpc) {
        WaitForSingleObject(hRpc, 5000);
        CloseHandle(hRpc);
    }

    CloseHandle(g_ServiceStopEvent);
    g_ServiceStopEvent = NULL;

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    g_ServiceStatus.dwWin32ExitCode = NO_ERROR;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    LogToFile(L"ServiceMain: STOPPED");
}

VOID WINAPI ServiceCtrlHandler(DWORD CtrlCode)
{
    switch (CtrlCode)
    {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
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

DWORD WINAPI ServiceWorkerThreadStub(LPVOID lpParam) {
    LogToFile(L"ServiceWorkerThread: Started");

    Sleep(3000);

    std::set<DWORD> known;
    WTS_SESSION_INFO* p = NULL;
    DWORD n = 0;

    if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &p, &n)) {
        for (DWORD i = 0; i < n; i++) {
            if (p[i].SessionId == 0) continue;
            known.insert(p[i].SessionId);
            if (p[i].State == WTSActive || p[i].State == WTSConnected) {
                StartAppInSession(p[i].SessionId);
            }
        }
        WTSFreeMemory(p);
    }

    while (WaitForSingleObject(g_ServiceStopEvent, 5000) == WAIT_TIMEOUT) {
        WTS_SESSION_INFO* ps = NULL;
        DWORD ns = 0;

        if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &ps, &ns)) {
            for (DWORD i = 0; i < ns; i++) {
                if (ps[i].SessionId == 0) continue;
                if (!known.count(ps[i].SessionId)) {
                    known.insert(ps[i].SessionId);
                    if (ps[i].State == WTSActive || ps[i].State == WTSConnected) {
                        StartAppInSession(ps[i].SessionId);
                    }
                }
                else if (ps[i].State == WTSActive && !g_SessionProcesses.count(ps[i].SessionId)) {
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
    wchar_t buf[256];
    wsprintf(buf, L"StartAppInSession: Attempting session %d", sessionId);
    LogToFile(buf);

    {
        std::lock_guard<std::mutex> lock(g_ProcessMutex);
        auto it = g_SessionProcesses.find(sessionId);
        if (it != g_SessionProcesses.end() && !it->second.empty())
        {
            bool allDead = true;
            for (HANDLE h : it->second) {
                DWORD exitCode = 0;
                if (GetExitCodeProcess(h, &exitCode) && exitCode == STILL_ACTIVE) {
                    allDead = false;
                    break;
                }
            }
            if (!allDead) {
                wsprintf(buf, L"StartAppInSession: Session %d already has running process", sessionId);
                LogToFile(buf);
                return;
            }
            it->second.clear();
        }
    }

    HANDLE hToken = NULL;
    if (!WTSQueryUserToken(sessionId, &hToken)) {
        wsprintf(buf, L"StartAppInSession: WTSQueryUserToken failed for session %d, error=%d", sessionId, GetLastError());
        LogToFile(buf);
        return;
    }

    HANDLE hDup = NULL;
    if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hDup))
    {
        wsprintf(buf, L"StartAppInSession: DuplicateTokenEx failed, error=%d", GetLastError());
        LogToFile(buf);
        CloseHandle(hToken);
        return;
    }
    CloseHandle(hToken);

    std::wstring appPath = g_ServiceDirectory + L"\\TrayApp.exe";

    if (!std::filesystem::exists(appPath)) {
        wsprintf(buf, L"StartAppInSession: File not found: %s", appPath.c_str());
        LogToFile(buf);
        CloseHandle(hDup);
        return;
    }

    std::wstring cmdLine = L"\"" + appPath + L"\" --service";

    STARTUPINFO si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi = { 0 };
    LPVOID env = NULL;
    CreateEnvironmentBlock(&env, hDup, FALSE);

    if (CreateProcessAsUser(hDup, NULL, (LPWSTR)cmdLine.c_str(), NULL, NULL, FALSE,
        CREATE_UNICODE_ENVIRONMENT, env, NULL, &si, &pi))
    {
        wsprintf(buf, L"StartAppInSession: Process created in session %d, PID=%d", sessionId, pi.dwProcessId);
        LogToFile(buf);

        ProtectProcessFromAdmins(pi.hProcess);

        std::lock_guard<std::mutex> lock(g_ProcessMutex);
        g_SessionProcesses[sessionId].push_back(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else {
        wsprintf(buf, L"StartAppInSession: CreateProcessAsUser failed, error=%d", GetLastError());
        LogToFile(buf);
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
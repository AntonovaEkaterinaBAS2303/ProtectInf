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
bool ActivateLicense(const std::wstring& code, const std::wstring& mac);
std::wstring GetDeviceMac();
std::chrono::system_clock::time_point ParseExpirationDate(const std::wstring& expDate);
DWORD WINAPI TokenRefreshThread(LPVOID);
DWORD WINAPI LicenseRefreshThread(LPVOID);
void StartRefreshThreads();

// RPC stubs
void StopService(handle_t h) { (void)h; if (g_ServiceStopEvent) SetEvent(g_ServiceStopEvent); }
long GetStatus(handle_t h) { (void)h; return g_ServiceStatus.dwCurrentState; }
void Shutdown(handle_t h) { StopService(h); }

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
        while (!val.empty() && val[0] == L' ') val = val.substr(1);
        while (!val.empty() && val.back() == L' ') val.pop_back();
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

std::wstring GetDeviceMac() { return L"AA:BB:CC:DD:EE:FF"; }

// Парсинг даты из ISO формата: "2027-04-27T23:59:59" или "2027-04-27"
std::chrono::system_clock::time_point ParseExpirationDate(const std::wstring& expDate) {
    if (expDate.empty() || expDate == L"null")
        return std::chrono::system_clock::now() + std::chrono::hours(8760);

    int y = 0, m = 0, d = 0, h = 23, min = 59, s = 59;
    int parsed = swscanf_s(expDate.c_str(), L"%d-%d-%dT%d:%d:%d", &y, &m, &d, &h, &min, &s);
    if (parsed < 3)
        parsed = swscanf_s(expDate.c_str(), L"%d-%d-%d", &y, &m, &d);
    if (parsed >= 3) {
        struct tm stm = {};
        stm.tm_year = y - 1900; stm.tm_mon = m - 1; stm.tm_mday = d;
        stm.tm_hour = (parsed >= 4) ? h : 23;
        stm.tm_min = (parsed >= 5) ? min : 59;
        stm.tm_sec = (parsed >= 6) ? s : 59;
        return std::chrono::system_clock::from_time_t(_mkgmtime(&stm));
    }
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
    if (!c.Connect()) return false;
    std::wstring body = L"{\"username\":\"" + u + L"\",\"password\":\"" + p + L"\",\"deviceId\":\"trayapp-windows\"}";
    if (!c.SendRequest(L"POST", L"/auth/login", body)) return false;
    if (c.GetStatusCode() != 200) return false;
    std::wstring r = c.GetResponse();
    std::lock_guard<std::mutex> l(g_AuthMutex);
    g_AuthTokens = std::make_unique<AuthTokens>();
    g_AuthTokens->accessToken = ExtractJsonValue(r, L"accessToken");
    g_AuthTokens->refreshToken = ExtractJsonValue(r, L"refreshToken");
    g_AuthenticatedUser = u;
    auto now = std::chrono::system_clock::now();
    g_AuthTokens->accessExpiry = now + std::chrono::hours(24);
    g_AuthTokens->refreshExpiry = now + std::chrono::hours(720);
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
    std::wstring at;
    { std::lock_guard<std::mutex> l(g_AuthMutex); if (!g_AuthTokens) return false; at = g_AuthTokens->accessToken; }
    HttpClient c(API_HOST, API_PORT, API_USE_HTTPS);
    if (!c.Connect()) return false;
    std::wstring body = L"{\"deviceMac\":\"" + GetDeviceMac() + L"\",\"productId\":\"" PRODUCT_ID L"\"}";
    if (!c.SendRequest(L"POST", L"/api/license/check", body, at)) return false;
    if (c.GetStatusCode() != 200) return false;
    std::wstring r = c.GetResponse();
    // Ищем ticket внутри ответа
    std::wstring tj = r;
    size_t ts = r.find(L"\"ticket\":{");
    if (ts != std::wstring::npos) tj = r.substr(ts + 9);

    std::lock_guard<std::mutex> ll(g_LicenseMutex);
    g_LicenseInfo = std::make_unique<LicenseInfo>();
    g_LicenseInfo->ticket = ExtractJsonValue(tj, L"licenseCode");
    std::wstring status = ExtractJsonValue(tj, L"status");
    std::wstring blocked = ExtractJsonValue(tj, L"blocked");
    g_LicenseInfo->active = (status == L"ACTIVE" && blocked != L"true");
    g_LicenseInfo->expiryDate = ParseExpirationDate(ExtractJsonValue(tj, L"expirationDate"));

    wchar_t buf[100]; wsprintf(buf, L"License check: active=%d, days=%lld", g_LicenseInfo->active ? 1 : 0,
        std::chrono::duration_cast<std::chrono::hours>(g_LicenseInfo->expiryDate - std::chrono::system_clock::now()).count() / 24);
    LogToFile(buf);
    return g_LicenseInfo->active;
}

bool ActivateLicense(const std::wstring& code, const std::wstring& mac) {
    std::wstring at;
    { std::lock_guard<std::mutex> l(g_AuthMutex); if (!g_AuthTokens) return false; at = g_AuthTokens->accessToken; }
    HttpClient c(API_HOST, API_PORT, API_USE_HTTPS);
    if (!c.Connect()) return false;
    std::wstring body = L"{\"activationKey\":\"" + code + L"\",\"deviceMac\":\"" + mac + L"\",\"deviceName\":\"TrayApp-Windows\"}";
    if (!c.SendRequest(L"POST", L"/api/license/activate", body, at)) return false;
    DWORD sc = c.GetStatusCode();
    std::wstring r = c.GetResponse();
    if (sc != 200 && sc != 201) return false;

    std::wstring tj = r;
    size_t ts = r.find(L"\"ticket\":{");
    if (ts != std::wstring::npos) tj = r.substr(ts + 9);

    std::lock_guard<std::mutex> ll(g_LicenseMutex);
    g_LicenseInfo = std::make_unique<LicenseInfo>();
    g_LicenseInfo->ticket = ExtractJsonValue(tj, L"licenseCode");
    g_LicenseInfo->active = true;
    g_LicenseInfo->expiryDate = ParseExpirationDate(ExtractJsonValue(tj, L"expirationDate"));
    LogToFile(L"License activated!");
    return true;
}

long Login(handle_t h, const wchar_t* u, const wchar_t* p) { (void)h; return PerformLogin(u, p) ? 0 : 1; }
long Logout(handle_t h) { (void)h; std::lock_guard<std::mutex> a(g_AuthMutex); g_AuthTokens.reset(); g_AuthenticatedUser.clear(); std::lock_guard<std::mutex> l(g_LicenseMutex); g_LicenseInfo.reset(); return 0; }
long GetUserInfo(handle_t h, wchar_t** u) {
    (void)h; std::lock_guard<std::mutex> l(g_AuthMutex);
    if (!g_AuthTokens || g_AuthenticatedUser.empty()) { *u = (wchar_t*)MIDL_user_allocate(8 * sizeof(wchar_t)); wcscpy_s(*u, 8, L"Unknown"); return 1; }
    *u = (wchar_t*)MIDL_user_allocate((g_AuthenticatedUser.length() + 1) * sizeof(wchar_t)); wcscpy_s(*u, g_AuthenticatedUser.length() + 1, g_AuthenticatedUser.c_str()); return 0;
}
long Activate(handle_t h, const wchar_t* code) { (void)h; return ActivateLicense(code, GetDeviceMac()) ? 0 : 1; }
long ActivateWithMac(handle_t h, const wchar_t* code, const wchar_t* mac) { (void)h; return ActivateLicense(code, mac) ? 0 : 1; }
long GetLicenseInfo(handle_t h, long* days, wchar_t** expiry) {
    (void)h; std::lock_guard<std::mutex> l(g_LicenseMutex);
    if (!g_LicenseInfo || !g_LicenseInfo->active) { *days = 0; *expiry = (wchar_t*)MIDL_user_allocate(16 * sizeof(wchar_t)); wcscpy_s(*expiry, 16, L"No License"); return 1; }
    *days = (long)(std::chrono::duration_cast<std::chrono::hours>(g_LicenseInfo->expiryDate - std::chrono::system_clock::now()).count() / 24);
    time_t exp = std::chrono::system_clock::to_time_t(g_LicenseInfo->expiryDate);
    struct tm stm; localtime_s(&stm, &exp);
    std::wstringstream wss; wss << std::put_time(&stm, L"%Y-%m-%d"); std::wstring ds = wss.str();
    *expiry = (wchar_t*)MIDL_user_allocate((ds.length() + 1) * sizeof(wchar_t)); wcscpy_s(*expiry, ds.length() + 1, ds.c_str());
    return 0;
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
    if (c == SERVICE_CONTROL_STOP || c == SERVICE_CONTROL_SHUTDOWN) { if (g_ServiceStopEvent) SetEvent(g_ServiceStopEvent); }
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
}

DWORD WINAPI ServiceWorkerThread(LPVOID) {
    std::set<DWORD> known; WTS_SESSION_INFO* p = NULL; DWORD n = 0;
    if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &p, &n)) { for (DWORD i = 0;i < n;i++) if (p[i].SessionId) { known.insert(p[i].SessionId); if (p[i].State == WTSActive) StartAppInSession(p[i].SessionId); } WTSFreeMemory(p); }
    while (WaitForSingleObject(g_ServiceStopEvent, 3000) == WAIT_TIMEOUT) { WTS_SESSION_INFO* ps = NULL; DWORD ns = 0; if (WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &ps, &ns)) { for (DWORD i = 0;i < ns;i++) if (!known.count(ps[i].SessionId)) { known.insert(ps[i].SessionId); if (ps[i].State == WTSActive) StartAppInSession(ps[i].SessionId); } WTSFreeMemory(ps); } }
    return 0;
}

void StartAppInSession(DWORD sid) {
    HANDLE hToken = NULL; if (!WTSQueryUserToken(sid, &hToken)) return;
    HANDLE hDup = NULL; if (!DuplicateTokenEx(hToken, MAXIMUM_ALLOWED, NULL, SecurityImpersonation, TokenPrimary, &hDup)) { CloseHandle(hToken); return; }
    CloseHandle(hToken);
    std::wstring app = g_ServiceDirectory + L"\\TrayApp.exe --service";
    STARTUPINFO si = { sizeof(si) }; si.wShowWindow = SW_HIDE; si.dwFlags = STARTF_USESHOWWINDOW;
    PROCESS_INFORMATION pi = { 0 }; LPVOID env = NULL; CreateEnvironmentBlock(&env, hDup, FALSE);
    if (CreateProcessAsUser(hDup, NULL, (LPWSTR)app.c_str(), NULL, NULL, FALSE, CREATE_UNICODE_ENVIRONMENT, env, NULL, &si, &pi)) { std::lock_guard<std::mutex> l(g_ProcessMutex); g_SessionProcesses[sid].push_back(pi.hProcess); CloseHandle(pi.hThread); }
    if (env) DestroyEnvironmentBlock(env); CloseHandle(hDup);
}

void StopAllApps() { std::lock_guard<std::mutex> l(g_ProcessMutex); for (auto& p : g_SessionProcesses) for (HANDLE h : p.second) { TerminateProcess(h, 0); CloseHandle(h); } g_SessionProcesses.clear(); }
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
#include <wininet.h>
#include <algorithm>  // для std::transform, std::min
#include <ctime>      // для time_t, gmtime_s
#include <cstring>    // для memcmp, memcpy
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "userenv.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "wininet.lib")

#include "../common/service_rpc.h"

#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")

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
#define AVDB_HASH_ALG CALG_SHA_256
#define AVDB_SIGN_ALG CALG_RSA_SIGN
#define AVDB_SIGN_ALG_MAGIC "RSA1"
#define UPDATE_ENDPOINT L"/api/av/update"
#define RECORD_ENDPOINT L"/api/av/record"
#define UPDATE_CHECK_INTERVAL 3600000  // 1 час в миллисекундах
#define AVDB_VERSION_URL L"/api/av/version"
#define TEMP_KEY_CONTAINER L"TrayAppTempContainer_{12345678-1234-1234-1234-123456789012}"


const char* AVDB_PUBLIC_KEY_BLOB =
"-----BEGIN PUBLIC KEY-----\nMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAsXG6UrLzGfhkNPDyB5u6\nWetKgFMcb3Sb3pCW4TuaM5jX0bNIBC3tty2/TljHhUVsBNSi2Il/zkTo14GLS3Kz\nCq3dxRohGlHs7ix08DtK3TO0rAZW+1Hpi9kLZKIohdwxxilln8No1LJxiuVPjfaB\nrlpYEd9jTsmlXj15wEpX9Cl656InvrLN24ZvVrguWuivurPMHE4855duUhWLuiZb\nifT1FdY+5JsN19oqEgJjQFIZA4d25RAOWpKxDmAlgkuENGUpCT3eJmDsQdIn1ms4\nOoQ6Beir1OXc2r9BIUsMbDCkNXugustv7PpUnjlpNUkGhbU3fmE5zmpCsQaq/BE4\n8wIDAQAB\n-----END PUBLIC KEY-----\n";


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

HCRYPTKEY ImportPublicKey(const std::string& pemKey) {
    HCRYPTPROV hProv = 0;
    HCRYPTKEY hKey = 0;
    wchar_t logBuf[512];

    std::string base64Key;
    bool isX509Format = false;

    if (pemKey.find("-----BEGIN RSA PUBLIC KEY-----") != std::string::npos) {
        LogToFile(L"[CRYPTO] Detected RSA PUBLIC KEY format");
    }
    else if (pemKey.find("-----BEGIN PUBLIC KEY-----") != std::string::npos) {
        isX509Format = true;
        LogToFile(L"[CRYPTO] Detected X.509 PUBLIC KEY format");
    }
    else {
        LogToFile(L"[CRYPTO] Unknown format");
        return 0;
    }

    const char* beginMarker = isX509Format ? "-----BEGIN PUBLIC KEY-----" : "-----BEGIN RSA PUBLIC KEY-----";
    const char* endMarker = isX509Format ? "-----END PUBLIC KEY-----" : "-----END RSA PUBLIC KEY-----";

    size_t start = pemKey.find(beginMarker) + strlen(beginMarker);
    size_t end = pemKey.find(endMarker);
    base64Key = pemKey.substr(start, end - start);

    base64Key.erase(std::remove_if(base64Key.begin(), base64Key.end(),
        [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }),
        base64Key.end());

    DWORD derLen = 0;
    CryptStringToBinaryA(base64Key.c_str(), (DWORD)base64Key.length(),
        CRYPT_STRING_BASE64, NULL, &derLen, NULL, NULL);

    std::vector<BYTE> derKey(derLen);
    CryptStringToBinaryA(base64Key.c_str(), (DWORD)base64Key.length(),
        CRYPT_STRING_BASE64, derKey.data(), &derLen, NULL, NULL);

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
            LogToFile(L"[CRYPTO] No crypto provider available");
            return 0;
        }
    }

    if (isX509Format) {
        CERT_PUBLIC_KEY_INFO* pPubKeyInfo = NULL;
        DWORD cbPubKeyInfo = 0;

        if (CryptDecodeObjectEx(
            X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
            X509_PUBLIC_KEY_INFO,
            derKey.data(), (DWORD)derKey.size(),
            CRYPT_DECODE_ALLOC_FLAG, NULL,
            &pPubKeyInfo, &cbPubKeyInfo))
        {
            wsprintf(logBuf, L"[CRYPTO] X.509 decoded OK, algorithm OID: %S",
                pPubKeyInfo->Algorithm.pszObjId ? pPubKeyInfo->Algorithm.pszObjId : "NULL");
            LogToFile(logBuf);

            if (!CryptImportPublicKeyInfo(hProv, X509_ASN_ENCODING, pPubKeyInfo, &hKey)) {
                DWORD err = GetLastError();
                wsprintf(logBuf, L"[CRYPTO] CryptImportPublicKeyInfo failed: 0x%08X", err);
                LogToFile(logBuf);
            }
            else {
                LogToFile(L"[CRYPTO] X.509 key imported successfully");
            }
            LocalFree(pPubKeyInfo);
        }
    }
    else {
        if (!CryptImportKey(hProv, derKey.data(), (DWORD)derKey.size(), 0, 0, &hKey)) {
            DWORD err = GetLastError();
            wsprintf(logBuf, L"[CRYPTO] CryptImportKey (RSA) failed: 0x%08X", err);
            LogToFile(logBuf);
        }
        else {
            LogToFile(L"[CRYPTO] RSA key imported directly");
        }
    }

    return hKey;
}

// Структура манифеста базы
struct AVDB_MANIFEST {
    char magic[4] = { 'A', 'V', 'D', 'B' }; // Магическое число
    uint32_t version = 1;                 // Версия формата
    uint64_t releaseTimestamp;            // Время сборки базы (Unix time)
    uint32_t recordCount;                 // Количество записей
    uint32_t reserved;                    // Выравнивание
    // Далее следует цифровая подпись манифеста
    // Длина подписи будет фиксированной или записана здесь же.
    uint32_t signatureSize;              // Размер подписи в байтах
    std::vector<uint8_t> signature;      // Сама подпись (сериализуется отдельно)

    // Сериализация в вектор для подписи (данные, которые подписываются)
    std::vector<uint8_t> GetDataToSign() const {
        std::vector<uint8_t> data;
        data.insert(data.end(), magic, magic + 4);
        data.insert(data.end(), (uint8_t*)&version, (uint8_t*)&version + 4);
        data.insert(data.end(), (uint8_t*)&releaseTimestamp, (uint8_t*)&releaseTimestamp + 8);
        data.insert(data.end(), (uint8_t*)&recordCount, (uint8_t*)&recordCount + 4);
        return data;
    }
};

// Типы сканируемых объектов
enum class ObjectType : uint32_t {
    PE_FILE = 1,        // PE исполняемые файлы
    DOTNET_ASSEMBLY = 2,// .NET сборки
    JAVA_CLASS = 3,     // Java классы
    PYTHON_SCRIPT = 4,  // Python скрипты
    JAVASCRIPT = 5,     // JavaScript
    POWERSHELL = 6      // PowerShell скрипты
};

// Запись антивирусной базы
struct AvRecord {
    uint64_t objectSignaturePrefix;  // Первые 8 байт сигнатуры
    uint32_t objectSignatureLength;  // Длина сигнатуры (включая префикс)
    std::vector<uint8_t> objectSignature; // Хеш сигнатуры
    uint64_t offsetBegin;            // Начало интервала поиска
    uint64_t offsetEnd;              // Конец интервала поиска
    ObjectType objectType;           // Тип объекта
    std::vector<uint8_t> avRecordSignature; // ЭЦП записи
};

struct MonitoredDirectory {
    std::wstring path;
    HANDLE hDir;
    bool recursive;
    std::vector<uint8_t> buffer;
    OVERLAPPED overlapped;
};

std::vector<MonitoredDirectory> g_MonitoredDirs;
std::mutex g_MonitorMutex;
HANDLE g_hMonitorThread = NULL;
HANDLE g_hIOCP = NULL;  // IO Completion Port
bool g_bMonitorActive = false;

// Callback для уведомления клиентов об обнаружении
typedef void (*MalwareFoundCallback)(const wchar_t* filePath, const wchar_t* signatureName);

// Антивирусная база (красно-чёрное дерево)
std::map<uint64_t, std::vector<std::shared_ptr<AvRecord>>> g_AvDatabase;
std::mutex g_AvDbMutex;
std::wstring g_AvDbReleaseDate;
size_t g_AvDbRecordCount = 0;

HANDLE g_hUpdateThread = NULL;
HANDLE g_hUpdateEvent = NULL;
bool g_bForceUpdate = false;
std::mutex g_UpdateMutex;


struct AuthTokens {
    std::wstring accessToken, refreshToken;
    std::chrono::system_clock::time_point accessExpiry, refreshExpiry;
};

struct LicenseInfo {
    std::wstring ticket;
    std::wstring licenseId;
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
long AddMonitoredDirectory(const std::wstring& path, bool recursive);
long RemoveMonitoredDirectory(const std::wstring& path);
void StartMonitoring();
void StopMonitoring();
bool UpdateAvDatabase(const std::wstring& newDbPath);
bool CheckForUpdates();
DWORD WINAPI UpdateThread(LPVOID lpParam);
std::wstring ExtractJsonValue(
    const std::wstring& json,
    const std::wstring& key
);

long ScanFile(handle_t h, const wchar_t* filePath, ScanResultData* result);

std::vector<uint8_t> CalculateHash(const std::vector<uint8_t>& data);

DWORD WINAPI DirectoryMonitorThread(LPVOID lpParam);
void ProcessFileNotification(const std::wstring& filePath);
bool IsScanTarget(const std::wstring& filePath);

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

// Функция проверки целостности блока данных
bool VerifySignature(BYTE* data, size_t dataSize, BYTE* signature, size_t sigSize, const std::string& publicKeyBlob) {
    wchar_t buf[512];
    wsprintf(buf, L"[VERIFY] Input params: dataSize=%Iu, sigSize=%Iu", dataSize, sigSize);
    LogToFile(buf);

    if (sigSize == 0 || signature == NULL) {
        LogToFile(L"[AVDB] Empty signature - verification skipped");
        return true;
    }

    // Дамп данных
    std::wstring hexDump;
    for (size_t i = 0; i < min(dataSize, (size_t)20); i++) {
        wchar_t hex[4];
        wsprintf(hex, L"%02X ", data[i]);
        hexDump += hex;
    }
    wsprintf(buf, L"[VERIFY] Data: %s", hexDump.c_str());
    LogToFile(buf);

    // Парсим PEM ключ
    std::string base64Key;
    const char* beginMarker = "-----BEGIN PUBLIC KEY-----";
    const char* endMarker = "-----END PUBLIC KEY-----";

    size_t start = publicKeyBlob.find(beginMarker);
    size_t end = publicKeyBlob.find(endMarker);

    if (start == std::string::npos) {
        beginMarker = "-----BEGIN RSA PUBLIC KEY-----";
        endMarker = "-----END RSA PUBLIC KEY-----";
        start = publicKeyBlob.find(beginMarker);
        end = publicKeyBlob.find(endMarker);
    }

    if (start == std::string::npos) {
        LogToFile(L"[VERIFY] No PEM markers found");
        return false;
    }

    start += strlen(beginMarker);
    base64Key = publicKeyBlob.substr(start, end - start);
    base64Key.erase(std::remove_if(base64Key.begin(), base64Key.end(),
        [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }),
        base64Key.end());

    // Декодируем base64
    DWORD derLen = 0;
    CryptStringToBinaryA(base64Key.c_str(), (DWORD)base64Key.length(),
        CRYPT_STRING_BASE64, NULL, &derLen, NULL, NULL);

    std::vector<BYTE> derKey(derLen);
    CryptStringToBinaryA(base64Key.c_str(), (DWORD)base64Key.length(),
        CRYPT_STRING_BASE64, derKey.data(), &derLen, NULL, NULL);

    // Получаем криптопровайдер
    HCRYPTPROV hProv = 0;
    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
            LogToFile(L"[VERIFY] No crypto provider");
            return false;
        }
    }

    // Декодируем X.509 и импортируем ключ
    CERT_PUBLIC_KEY_INFO* pPubKeyInfo = NULL;
    DWORD cbPubKeyInfo = 0;

    if (!CryptDecodeObjectEx(
        X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
        X509_PUBLIC_KEY_INFO,
        derKey.data(), (DWORD)derKey.size(),
        CRYPT_DECODE_ALLOC_FLAG, NULL,
        &pPubKeyInfo, &cbPubKeyInfo))
    {
        wsprintf(buf, L"[VERIFY] X.509 decode failed: 0x%08X", GetLastError());
        LogToFile(buf);
        CryptReleaseContext(hProv, 0);
        return false;
    }

    HCRYPTKEY hPublicKey = 0;
    if (!CryptImportPublicKeyInfo(hProv, X509_ASN_ENCODING, pPubKeyInfo, &hPublicKey)) {
        wsprintf(buf, L"[VERIFY] Import key failed: 0x%08X", GetLastError());
        LogToFile(buf);
        LocalFree(pPubKeyInfo);
        CryptReleaseContext(hProv, 0);
        return false;
    }
    LocalFree(pPubKeyInfo);

    LogToFile(L"[VERIFY] Key imported successfully");

    // === РУЧНАЯ ВЕРИФИКАЦИЯ ПОДПИСИ ===
    // 1. Вычисляем хеш данных
    HCRYPTHASH hHash = 0;
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        LogToFile(L"[VERIFY] CryptCreateHash failed");
        CryptDestroyKey(hPublicKey);
        CryptReleaseContext(hProv, 0);
        return false;
    }

    if (!CryptHashData(hHash, data, (DWORD)dataSize, 0)) {
        LogToFile(L"[VERIFY] CryptHashData failed");
        CryptDestroyHash(hHash);
        CryptDestroyKey(hPublicKey);
        CryptReleaseContext(hProv, 0);
        return false;
    }

    // 2. Получаем хеш
    BYTE hashValue[32] = { 0 };
    DWORD hashLen = 32;
    CryptGetHashParam(hHash, HP_HASHVAL, hashValue, &hashLen, 0);

    wsprintf(buf, L"[VERIFY] Hash: %02X%02X%02X%02X%02X%02X%02X%02X...",
        hashValue[0], hashValue[1], hashValue[2], hashValue[3],
        hashValue[4], hashValue[5], hashValue[6], hashValue[7]);
    LogToFile(buf);

    // 3. Копируем подпись и "расшифровываем" её
    std::vector<BYTE> sigCopy(signature, signature + sigSize);
    DWORD decryptedLen = (DWORD)sigCopy.size();

    // Пробуем CryptDecrypt
    BOOL decryptResult = CryptDecrypt(hPublicKey, 0, TRUE, 0, sigCopy.data(), &decryptedLen);

    if (decryptResult && decryptedLen >= 32) {
        // PKCS#1 v1.5 структура: 00 01 FF...FF 00 DER(SHA-256) HASH
        // Ищем 0x00 разделитель с конца
        BYTE* hashInSig = NULL;
        for (int i = decryptedLen - 34; i >= 0; i--) {
            if (sigCopy[i] == 0x00) {
                hashInSig = sigCopy.data() + i + 1;
                break;
            }
        }

        if (hashInSig) {
            // SHA-256 OID: 30 31 30 0D 06 09 60 86 48 01 65 03 04 02 01 05 00 04 20
            // Хеш начинается после этого DER-заголовка
            BYTE* hashStart = hashInSig;
            // Пропускаем DER-заголовок (обычно 19 байт для SHA-256)
            if (decryptedLen - (hashStart - sigCopy.data()) >= 32 + 19) {
                hashStart += 19;
            }
            else if (decryptedLen - (hashStart - sigCopy.data()) >= 32) {
                // Если DER-заголовок короче
                hashStart = sigCopy.data() + decryptedLen - 32;
            }

            wsprintf(buf, L"[VERIFY] Sig hash: %02X%02X%02X%02X...",
                hashStart[0], hashStart[1], hashStart[2], hashStart[3]);
            LogToFile(buf);

            if (memcmp(hashStart, hashValue, 32) == 0) {
                LogToFile(L"[VERIFY] Manual verification SUCCESS!");
                CryptDestroyHash(hHash);
                CryptDestroyKey(hPublicKey);
                CryptReleaseContext(hProv, 0);
                return true;
            }
            LogToFile(L"[VERIFY] Hash mismatch");
        }
    }
    else {
        wsprintf(buf, L"[VERIFY] CryptDecrypt failed: 0x%08X, len=%lu",
            GetLastError(), decryptedLen);
        LogToFile(buf);
    }

    // Fallback: пробуем стандартную верификацию
    LogToFile(L"[VERIFY] Trying standard CryptVerifySignature...");
    BOOL result = CryptVerifySignature(hHash, signature, (DWORD)sigSize, hPublicKey, NULL, 0);

    if (!result) {
        // Пробуем reversed
        std::vector<BYTE> reversedSig(sigSize);
        for (size_t i = 0; i < sigSize; i++) {
            reversedSig[i] = signature[sigSize - 1 - i];
        }
        result = CryptVerifySignature(hHash, reversedSig.data(), (DWORD)sigSize, hPublicKey, NULL, 0);
        if (result) LogToFile(L"[VERIFY] Standard SUCCESS with reversed!");
    }
    else {
        LogToFile(L"[VERIFY] Standard verification SUCCESS");
    }

    CryptDestroyHash(hHash);
    CryptDestroyKey(hPublicKey);
    CryptReleaseContext(hProv, 0);
    return result == TRUE;
}

bool CheckNetworkAvailability() {
    DWORD flags = 0;
    BOOL result = InternetGetConnectedState(&flags, 0);
    return result == TRUE;
}

bool DownloadFile(const std::wstring& url, const std::wstring& localPath,
    const std::wstring& authToken = L"") {
    HINTERNET hSession = NULL, hConnect = NULL, hRequest = NULL;

    hSession = WinHttpOpen(L"TrayApp/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    // Парсим URL
    URL_COMPONENTS urlComp = { 0 };
    urlComp.dwStructSize = sizeof(urlComp);
    wchar_t hostName[256] = { 0 }, urlPath[1024] = { 0 };
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = 256;
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = 1024;

    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.length(), 0, &urlComp)) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    bool useHttps = (urlComp.nScheme == INTERNET_SCHEME_HTTPS);

    hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
    hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // Настройка безопасности для HTTPS
    if (useHttps) {
        DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
            SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
            SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
            SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
        WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS,
            &securityFlags, sizeof(securityFlags));
    }

    // Добавляем заголовок авторизации если есть
    std::wstring headers = L"";
    if (!authToken.empty()) {
        headers = L"Authorization: Bearer " + authToken;
    }

    if (!WinHttpSendRequest(hRequest,
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        headers.empty() ? 0 : (DWORD)headers.length(),
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        goto cleanup;
    }

    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        goto cleanup;
    }

    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);

    if (statusCode != 200) goto cleanup;

    {
        std::ofstream file(localPath, std::ios::binary);
        if (!file) goto cleanup;

        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;

            std::vector<BYTE> buffer(dwSize);
            if (!WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) break;
            if (dwDownloaded == 0) break;

            file.write((char*)buffer.data(), dwDownloaded);
        } while (true);

        file.close();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return true;
    }

cleanup:
    if (hRequest) WinHttpCloseHandle(hRequest);
    if (hConnect) WinHttpCloseHandle(hConnect);
    if (hSession) WinHttpCloseHandle(hSession);
    return false;
}

std::wstring GetServerVersion(const std::wstring& authToken) {
    HttpClient c(API_HOST, API_PORT, API_USE_HTTPS);
    if (!c.Connect()) return L"";
    if (!c.SendRequest(L"GET", AVDB_VERSION_URL, L"", authToken)) return L"";

    DWORD status = c.GetStatusCode();
    if (status != 200) return L"";

    std::wstring response = c.GetResponse();
    return ExtractJsonValue(response, L"version");
}

long GetServerRecord(handle_t h, const wchar_t* filePath, AvRecord* record) {
    (void)h;
    if (!record) return 1;

    std::wstring authToken;
    {
        std::lock_guard<std::mutex> lock(g_AuthMutex);
        if (!g_AuthTokens) return 1;
        authToken = g_AuthTokens->accessToken;
    }

    if (!CheckNetworkAvailability()) return 1;

    HttpClient c(API_HOST, API_PORT, API_USE_HTTPS);
    if (!c.Connect()) return 1;

    // Формируем запрос
    std::wstring body = L"{\"filePath\":\"" + std::wstring(filePath) + L"\"}";
    if (!c.SendRequest(L"POST", RECORD_ENDPOINT, body, authToken)) return 1;

    if (c.GetStatusCode() != 200) return 1;

    std::wstring response = c.GetResponse();

    return 0;
}

void LoadDefaultAvDatabase() {
    std::lock_guard<std::mutex> lock(g_AvDbMutex);
    g_AvDatabase.clear();
    g_AvDbRecordCount = 0;

    // EICAR тестовый файл
    {
        auto eicar = std::make_shared<AvRecord>();
        eicar->objectSignaturePrefix = 0x41402550214F3558ULL;
        eicar->objectSignatureLength = 8;
        eicar->objectSignature = CalculateHash(std::vector<uint8_t>{'X', '5', 'O', '!', 'P', '%', '@', 'A'});
        eicar->offsetBegin = 0;
        eicar->offsetEnd = 68;
        eicar->objectType = ObjectType::PE_FILE;
        eicar->avRecordSignature.clear();
        g_AvDatabase[eicar->objectSignaturePrefix].push_back(eicar);
    }

    // PowerShell тестовая строка
    {
        auto ps = std::make_shared<AvRecord>();
        ps->objectSignaturePrefix = 0x53207265776F5000ULL;
        ps->objectSignatureLength = 8;
        ps->objectSignature = CalculateHash(std::vector<uint8_t>{'P', 'o', 'w', 'e', 'r', 'S', 'h', 'e'});
        ps->offsetBegin = 0;
        ps->offsetEnd = 23;
        ps->objectType = ObjectType::POWERSHELL;
        ps->avRecordSignature.clear();
        g_AvDatabase[ps->objectSignaturePrefix].push_back(ps);
    }

    // BAT тестовый файл
    {
        auto bat = std::make_shared<AvRecord>();
        bat->objectSignaturePrefix = 0x206F6863654045ULL;
        bat->objectSignatureLength = 8;
        bat->objectSignature = CalculateHash(std::vector<uint8_t>{'@', 'e', 'c', 'h', 'o', ' ', 'V', 'i'});
        bat->offsetBegin = 0;
        bat->offsetEnd = 16;
        bat->objectType = ObjectType::PE_FILE;
        bat->avRecordSignature.clear();
        g_AvDatabase[bat->objectSignaturePrefix].push_back(bat);
    }

    g_AvDbRecordCount = 3;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t dateBuf[64];
    wsprintf(dateBuf, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    g_AvDbReleaseDate = dateBuf;

    LogToFile(L"[AVDB] Default database loaded with 3 records");
}

// Вспомогательная функция для логирования hex-дампа
void LogHexDump(const wchar_t* prefix, const uint8_t* data, size_t size) {
    std::wstring dump = prefix;
    wchar_t hex[4];
    for (size_t i = 0; i < min(size, (size_t)64); i++) {
        wsprintf(hex, L"%02X ", data[i]);
        dump += hex;
    }
    if (size > 64) {
        dump += L"...";
    }
    LogToFile((LPWSTR)dump.c_str());
}

// Загрузка антивирусных записей с проверкой ЭЦП каждой записи
bool LoadRecords(std::ifstream& file, const AVDB_MANIFEST& manifest, const std::string& publicKey) {
    std::lock_guard<std::mutex> lock(g_AvDbMutex);
    g_AvDatabase.clear();
    g_AvDbRecordCount = 0;

    wchar_t buf[512];
    unsigned int loadedCount = 0;
    unsigned int discardedCount = 0;
    unsigned int networkRequested = 0;

    wsprintf(buf, L"[AVDB] Loading %u records...", manifest.recordCount);
    LogToFile(buf);

    for (uint32_t i = 0; i < manifest.recordCount; ++i) {
        AvRecord record;

        // 1. Читаем префикс (8 байт)
        file.read((char*)&record.objectSignaturePrefix, 8);
        if (file.fail()) {
            wsprintf(buf, L"[AVDB] Failed to read prefix for record %u", i);
            LogToFile(buf);
            break;
        }

        // 2. Читаем длину сигнатуры (4 байта)
        file.read((char*)&record.objectSignatureLength, 4);
        if (file.fail()) {
            wsprintf(buf, L"[AVDB] Failed to read signature length for record %u", i);
            LogToFile(buf);
            break;
        }

        // 3. Читаем саму сигнатуру (переменная длина)
        if (record.objectSignatureLength > 0 && record.objectSignatureLength < 1024 * 1024) {
            record.objectSignature.resize(record.objectSignatureLength);
            file.read((char*)record.objectSignature.data(), record.objectSignatureLength);
            if (file.fail()) {
                wsprintf(buf, L"[AVDB] Failed to read signature data for record %u (length=%u)",
                    i, record.objectSignatureLength);
                LogToFile(buf);
                break;
            }
        }
        else {
            wsprintf(buf, L"[AVDB] Invalid signature length for record %u: %u",
                i, record.objectSignatureLength);
            LogToFile(buf);
            discardedCount++;
            continue;
        }

        // 4. Читаем offsetBegin (8 байт)
        file.read((char*)&record.offsetBegin, 8);
        if (file.fail()) {
            wsprintf(buf, L"[AVDB] Failed to read offsetBegin for record %u", i);
            LogToFile(buf);
            break;
        }

        // 5. Читаем offsetEnd (8 байт)
        file.read((char*)&record.offsetEnd, 8);
        if (file.fail()) {
            wsprintf(buf, L"[AVDB] Failed to read offsetEnd for record %u", i);
            LogToFile(buf);
            break;
        }

        // 6. Читаем objectType (4 байта)
        file.read((char*)&record.objectType, sizeof(ObjectType));
        if (file.fail()) {
            wsprintf(buf, L"[AVDB] Failed to read objectType for record %u", i);
            LogToFile(buf);
            break;
        }

        // 7. Читаем длину подписи записи (4 байта)
        uint32_t sigLen = 0;
        file.read((char*)&sigLen, 4);
        if (file.fail()) {
            wsprintf(buf, L"[AVDB] Failed to read record signature length for record %u", i);
            LogToFile(buf);
            break;
        }

        // 8. Читаем подпись записи
        if (sigLen > 0 && sigLen < 1024) {
            record.avRecordSignature.resize(sigLen);
            file.read((char*)record.avRecordSignature.data(), sigLen);
            if (file.fail()) {
                wsprintf(buf, L"[AVDB] Failed to read record signature for record %u", i);
                LogToFile(buf);
                break;
            }
        }
        else {
            wsprintf(buf, L"[AVDB] Invalid record signature length for record %u: %u",
                i, sigLen);
            LogToFile(buf);
            discardedCount++;
            continue;
        }

        // Логируем информацию о записи
        wsprintf(buf, L"[AVDB] Record %u: prefix=0x%016llX, sigLen=%u, offsets=[%llu,%llu], type=%u",
            i, record.objectSignaturePrefix, record.objectSignatureLength,
            record.offsetBegin, record.offsetEnd, (unsigned int)record.objectType);
        LogToFile(buf);

        // Подготовка данных для проверки подписи
        // ВАЖНО: порядок и размер полей должны точно соответствовать Python!
        std::vector<uint8_t> recordData;

        // prefix (8 байт)
        recordData.insert(recordData.end(),
            (uint8_t*)&record.objectSignaturePrefix,
            (uint8_t*)&record.objectSignaturePrefix + 8);

        // signatureLength (4 байта)
        recordData.insert(recordData.end(),
            (uint8_t*)&record.objectSignatureLength,
            (uint8_t*)&record.objectSignatureLength + 4);

        // signature data (переменная длина)
        recordData.insert(recordData.end(),
            record.objectSignature.begin(),
            record.objectSignature.end());

        // offsetBegin (8 байт)
        recordData.insert(recordData.end(),
            (uint8_t*)&record.offsetBegin,
            (uint8_t*)&record.offsetBegin + 8);

        // offsetEnd (8 байт)
        recordData.insert(recordData.end(),
            (uint8_t*)&record.offsetEnd,
            (uint8_t*)&record.offsetEnd + 8);

        // objectType (4 байта)
        recordData.insert(recordData.end(),
            (uint8_t*)&record.objectType,
            (uint8_t*)&record.objectType + sizeof(ObjectType));

        // Логируем размер данных и их начало
        wsprintf(buf, L"[AVDB] Record %u data size: %Iu bytes", i, recordData.size());
        LogToFile(buf);
        LogHexDump(L"[AVDB] Record data hex: ", recordData.data(), recordData.size());

        // Проверка ЭЦП записи
        if (!VerifySignature(recordData.data(), recordData.size(),
            record.avRecordSignature.data(), sigLen, publicKey)) {
            wsprintf(buf, L"[AVDB] Record %u signature INVALID.", i);
            LogToFile(buf);

            // Запрос записи с сервера при наличии сети
            if (CheckNetworkAvailability()) {
                wsprintf(buf, L"[AVDB] Attempting to fetch record %u from server...", i);
                LogToFile(buf);
                // TODO: Реализовать запрос к серверу
                networkRequested++;
            }

            discardedCount++;
            continue;
        }

        // Логируем сигнатуру для сверки
        wsprintf(buf, L"[AVDB] Record %u signature VERIFIED successfully", i);
        LogToFile(buf);
        LogHexDump(L"[AVDB] Signature first bytes: ",
            record.avRecordSignature.data(), min(record.avRecordSignature.size(), (size_t)32));

        // Добавление записи в базу
        g_AvDatabase[record.objectSignaturePrefix].push_back(std::make_shared<AvRecord>(std::move(record)));
        loadedCount++;
    }

    g_AvDbRecordCount = loadedCount;
    wsprintf(buf, L"[AVDB] Records loaded: %u valid, %u discarded, %u requested from network",
        loadedCount, discardedCount, networkRequested);
    LogToFile(buf);

    return loadedCount > 0;
}

// Основная функция загрузки базы с обработкой всех ошибок
bool LoadAndVerifyAvDatabase(const std::wstring& dbPath, const std::wstring& publicKeyPath) {
    LogToFile(L"[AVDB] Starting database loading process...");

    std::wstring primaryPath = dbPath;
    std::wstring backupPath = dbPath + L".bak";

    // Загружаем публичный ключ из файла
    std::string publicKeyBlob = AVDB_PUBLIC_KEY_BLOB;

    // Пытаемся прочитать ключ из файла
    std::ifstream keyFile(publicKeyPath);
    if (keyFile) {
        std::stringstream buffer;
        buffer << keyFile.rdbuf();
        publicKeyBlob = buffer.str();
        keyFile.close();
        LogToFile(L"[AVDB] Public key loaded from file");
    }
    else {
        LogToFile(L"[AVDB] Using built-in public key");
    }

    // Попытка загрузить основную базу
    std::ifstream file(primaryPath, std::ios::binary);
    if (!file) {
        LogToFile(L"[AVDB] Primary database not found. Check backup.");
        file.open(backupPath, std::ios::binary);
        if (file) {
            LogToFile(L"[AVDB] Loading backup database...");
            primaryPath = backupPath;
        }
        else {
            // Требование 6: Загружаем базу по умолчанию
            LogToFile(L"[AVDB] Backup not found. Loading default database.");
            LoadDefaultAvDatabase();

            // Если есть сеть, пытаемся обновить базы
            if (CheckNetworkAvailability()) {
                LogToFile(L"[AVDB] Network available, attempting forced update...");
                // Запускаем принудительное обновление в фоне
                SetEvent(g_hUpdateEvent);
            }

            return true;
        }
    }

    // Чтение и проверка манифеста
    AVDB_MANIFEST manifest;
    file.read((char*)manifest.magic, 4);
    file.read((char*)&manifest.version, 4);
    file.read((char*)&manifest.releaseTimestamp, 8);
    file.read((char*)&manifest.recordCount, 4);
    file.read((char*)&manifest.reserved, 4);
    file.read((char*)&manifest.signatureSize, 4);

    if (manifest.signatureSize > 0) {
        manifest.signature.resize(manifest.signatureSize);
        file.read((char*)manifest.signature.data(), manifest.signatureSize);
    }

    // Проверка манифеста (Требование 4)
    auto dataToSign = manifest.GetDataToSign();
    bool manifestValid = VerifySignature(dataToSign.data(), dataToSign.size(),
        manifest.signature.data(), manifest.signatureSize, publicKeyBlob);

    if (!manifestValid) {
        LogToFile(L"[AVDB] Manifest signature INVALID!");
        file.close();

        // Требование 5: Попытка восстановить из бэкапа
        if (primaryPath == backupPath) {
            LogToFile(L"[AVDB] Backup manifest is also invalid.");

            // Проверяем наличие сети
            if (CheckNetworkAvailability()) {
                LogToFile(L"[AVDB] Network available, forcing update...");
                // Запускаем принудительное обновление
                SetEvent(g_hUpdateEvent);

                // Временно загружаем базу по умолчанию
                LoadDefaultAvDatabase();
                return true;
            }
            else {
                // Загружаем базу по умолчанию
                LogToFile(L"[AVDB] No network, loading default database.");
                LoadDefaultAvDatabase();
                return true;
            }
        }
        else {
            // Пробуем удалить поврежденный файл и загрузить бэкап
            LogToFile(L"[AVDB] Trying to recover from backup...");
            std::error_code ec;
            std::filesystem::remove(primaryPath, ec);
            return LoadAndVerifyAvDatabase(dbPath, publicKeyPath);
        }
    }

    LogToFile(L"[AVDB] Manifest verified successfully.");

    // Установка даты выпуска базы
    time_t time = (time_t)manifest.releaseTimestamp;
    struct tm stm;
    gmtime_s(&stm, &time);
    wchar_t dateBuf[64];
    wsprintf(dateBuf, L"%04d-%02d-%02dT%02d:%02d:%02dZ",
        stm.tm_year + 1900, stm.tm_mon + 1, stm.tm_mday,
        stm.tm_hour, stm.tm_min, stm.tm_sec);
    {
        std::lock_guard<std::mutex> lock(g_AvDbMutex);
        g_AvDbReleaseDate = dateBuf;
    }

    // Загрузка записей с проверкой их ЭЦП (Требование 7)
    LoadRecords(file, manifest, publicKeyBlob);
    file.close();

    // Создание резервной копии успешно загруженной базы
    if (primaryPath == dbPath) {
        std::error_code ec;
        std::filesystem::copy_file(dbPath, backupPath,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            LogToFile(L"[AVDB] Backup copy created successfully.");
        }
    }

    return true;
}

bool CheckForUpdates() {
    LogToFile(L"[UPDATE] Checking for database updates...");

    if (!CheckNetworkAvailability()) {
        LogToFile(L"[UPDATE] No network available");
        return false;
    }

    std::wstring authToken;
    {
        std::lock_guard<std::mutex> lock(g_AuthMutex);
        if (!g_AuthTokens) {
            LogToFile(L"[UPDATE] No auth token");
            return false;
        }
        authToken = g_AuthTokens->accessToken;
    }

    // Получаем версию с сервера
    std::wstring serverVersion = GetServerVersion(authToken);
    if (serverVersion.empty()) {
        LogToFile(L"[UPDATE] Failed to get server version");
        return false;
    }

    // Сравниваем с локальной версией
    std::wstring localVersion;
    {
        std::lock_guard<std::mutex> lock(g_AvDbMutex);
        localVersion = g_AvDbReleaseDate;
    }

    if (serverVersion > localVersion || g_bForceUpdate) {
        LogToFile(L"[UPDATE] New version available, updating...");

        // Скачиваем новую базу
        std::wstring tempPath = g_ServiceDirectory + L"\\av_database_new.bin";
        std::wstring updateUrl = std::wstring(API_USE_HTTPS ? L"https://" : L"http://") +
            std::wstring(API_HOST) + L":" + std::to_wstring(API_PORT) +
            UPDATE_ENDPOINT;

        if (DownloadFile(updateUrl, tempPath, authToken)) {
            LogToFile(L"[UPDATE] New database downloaded");

            // Обновляем через существующую функцию
            if (UpdateAvDatabase(tempPath)) {
                std::filesystem::remove(tempPath);
                g_bForceUpdate = false;
                LogToFile(L"[UPDATE] Database updated successfully");
                return true;
            }
        }
    }
    else {
        LogToFile(L"[UPDATE] Database is up to date");
    }

    return false;
}

DWORD WINAPI UpdateThread(LPVOID lpParam) {
    (void)lpParam;
    LogToFile(L"[UPDATE] Update thread started");

    // Первая проверка через 30 секунд после запуска
    Sleep(30000);

    while (!g_bStopRefreshThreads) {
        // Ждем либо интервал, либо сигнал принудительного обновления
        DWORD waitResult = WaitForSingleObject(g_hUpdateEvent, UPDATE_CHECK_INTERVAL);

        if (g_bStopRefreshThreads) break;

        // Проверяем обновления либо по таймеру, либо по сигналу
        if (waitResult == WAIT_OBJECT_0 || waitResult == WAIT_TIMEOUT) {
            bool hasLicense = false;
            {
                std::lock_guard<std::mutex> lock(g_LicenseMutex);
                hasLicense = (g_LicenseInfo && g_LicenseInfo->active);
            }

            if (hasLicense) {
                CheckForUpdates();
            }

            // Сбрасываем событие после обработки
            if (waitResult == WAIT_OBJECT_0) {
                ResetEvent(g_hUpdateEvent);
            }
        }
    }

    LogToFile(L"[UPDATE] Update thread stopped");
    return 0;
}

bool TryLoadAvDatabaseStrict(const std::wstring& dbPath) {
    std::wstring publicKeyPath = g_ServiceDirectory + L"\\public_key_cryptoapi.pem";

    std::ifstream file(dbPath, std::ios::binary);
    if (!file) {
        LogToFile(L"[AVDB] Strict load failed: database file not found");
        return false;
    }

    std::string publicKeyBlob = AVDB_PUBLIC_KEY_BLOB;

    std::ifstream keyFile(publicKeyPath);
    if (keyFile) {
        std::stringstream buffer;
        buffer << keyFile.rdbuf();
        publicKeyBlob = buffer.str();
    }

    AVDB_MANIFEST manifest;
    file.read((char*)manifest.magic, 4);
    file.read((char*)&manifest.version, 4);
    file.read((char*)&manifest.releaseTimestamp, 8);
    file.read((char*)&manifest.recordCount, 4);
    file.read((char*)&manifest.reserved, 4);
    file.read((char*)&manifest.signatureSize, 4);

    if (!file || memcmp(manifest.magic, "AVDB", 4) != 0) {
        LogToFile(L"[AVDB] Strict load failed: invalid manifest header");
        return false;
    }

    if (manifest.signatureSize == 0 || manifest.signatureSize > 4096) {
        LogToFile(L"[AVDB] Strict load failed: invalid manifest signature size");
        return false;
    }

    manifest.signature.resize(manifest.signatureSize);
    file.read((char*)manifest.signature.data(), manifest.signatureSize);

    auto dataToSign = manifest.GetDataToSign();

    if (!VerifySignature(
        dataToSign.data(),
        dataToSign.size(),
        manifest.signature.data(),
        manifest.signatureSize,
        publicKeyBlob
    )) {
        LogToFile(L"[AVDB] Strict load failed: manifest signature invalid");
        return false;
    }

    return LoadRecords(file, manifest, publicKeyBlob);
}

// Функция обновления базы с резервированием и откатом (Необязательные требования 2-4)
bool UpdateAvDatabase(const std::wstring& newDbPath) {
    LogToFile(L"[AVDB] Starting database update...");
    std::wstring currentDbPath = g_ServiceDirectory + L"\\av_database.bin";
    std::wstring backupPath = currentDbPath + L".bak";

    // Резервное копирование текущих баз
    if (std::filesystem::exists(currentDbPath)) {
        std::error_code ec;
        std::filesystem::copy_file(currentDbPath, backupPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            LogToFile(L"[AVDB] Failed to backup current database before update.");
            return false;
        }
    }

    // Замена файла базы новым
    std::error_code ec;
    std::filesystem::copy_file(newDbPath, currentDbPath, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        LogToFile(L"[AVDB] Failed to replace database file.");
        // Откат не требуется, так как оригинальный файл еще не трогали
    }

    // Загрузка обновленной базы
    bool loaded = TryLoadAvDatabaseStrict(currentDbPath);

    if (!loaded) {
        LogToFile(L"[AVDB] Failed to load updated database. Rolling back...");
        // Откат к резервной копии (Требование 4)
        std::filesystem::copy_file(backupPath, currentDbPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            TryLoadAvDatabaseStrict(currentDbPath);
        }
    }

    return loaded;
}

ObjectType DetectFileType(const std::vector<uint8_t>& data) {
    if (data.size() < 4) return ObjectType::PE_FILE;

    // PE файл: MZ сигнатура
    if (data[0] == 'M' && data[1] == 'Z')
        return ObjectType::PE_FILE;

    // Java Class: CA FE BA BE
    if (data.size() >= 4 &&
        data[0] == 0xCA && data[1] == 0xFE &&
        data[2] == 0xBA && data[3] == 0xBE)
        return ObjectType::JAVA_CLASS;

    // .NET Assembly: MZ + PE
    if (data[0] == 'M' && data[1] == 'Z') {
        // Проверить наличие CLR заголовка
        return ObjectType::DOTNET_ASSEMBLY;
    }

    return ObjectType::PE_FILE;
}

std::vector<uint8_t> CalculateHash(const std::vector<uint8_t>& data) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    std::vector<uint8_t> hash(32); // SHA-256

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT))
        return hash;

    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return hash;
    }

    if (!CryptHashData(hHash, data.data(), (DWORD)data.size(), 0)) {
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return hash;
    }

    DWORD hashLen = 32;
    CryptGetHashParam(hHash, HP_HASHVAL, hash.data(), &hashLen, 0);

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);
    return hash;
}

struct ScanResult {
    bool isMalicious;
    std::vector<AvRecord> matchedRecords;
    std::wstring filePath;
};

ScanResult ScanStream(const std::vector<uint8_t>& data, ObjectType fileType) {
    ScanResult result = { false, {}, L"" };

    if (data.size() < 8) return result;

    size_t position = 0;

    while (position <= data.size() - 8) {
        uint64_t prefix = 0;
        memcpy(&prefix, data.data() + position, 8);

        std::lock_guard<std::mutex> lock(g_AvDbMutex);
        auto it = g_AvDatabase.find(prefix);

        if (it != g_AvDatabase.end()) {
            // Работаем с shared_ptr
            for (const auto& recordPtr : it->second) {
                const auto& record = *recordPtr;  // Разыменовываем для удобства

                if (record.objectType != fileType)
                    continue;

                if (position < record.offsetBegin || position > record.offsetEnd)
                    continue;

                if (position + record.objectSignatureLength > data.size())
                    continue;

                std::vector<uint8_t> signatureData(
                    data.begin() + position,
                    data.begin() + position + record.objectSignatureLength
                );

                auto hash = CalculateHash(signatureData);

                if (hash == record.objectSignature) {
                    result.isMalicious = true;
                    result.matchedRecords.push_back(record);  // Копируем для результата
                }
            }

            if (result.isMalicious)
                break;
        }

        position++;
    }

    return result;
}

bool IsScanTarget(const std::wstring& filePath) {
    // Convert to lowercase for comparison
    std::wstring lower = filePath;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    // Check file extensions that could be malicious
    static const std::vector<std::wstring> targetExtensions = {
        L".exe", L".dll", L".sys", L".ocx", L".com", L".scr",
        L".msi", L".bat", L".cmd", L".ps1", L".vbs", L".vbe",
        L".js", L".jse", L".wsf", L".wsh", L".hta", L".jar",
        L".py", L".pyc", L".php", L".asp", L".aspx",
        L".doc", L".docm", L".xls", L".xlsm", L".ppt", L".pptm"
    };

    for (const auto& ext : targetExtensions) {
        if (lower.length() >= ext.length() &&
            lower.compare(lower.length() - ext.length(), ext.length(), ext) == 0) {
            return true;
        }
    }

    return false;
}

void ProcessFileNotification(const std::wstring& filePath) {

    wchar_t buf[512];
    wsprintf(buf, L"[MONITOR] Checking: %s", filePath.c_str());
    LogToFile(buf);

    // Быстрая проверка - только расширение файла
    if (!IsScanTarget(filePath)) {
        return;  // Без логирования, без проверок диска
    }

    // Быстрая проверка лицензии
    bool isLicensed = false;
    {
        std::lock_guard<std::mutex> lock(g_LicenseMutex);
        isLicensed = (g_LicenseInfo && g_LicenseInfo->active);
    }

    if (!isLicensed) {
        return;  // Без логирования
    }

    // Только теперь проверяем существование файла
    if (!std::filesystem::exists(filePath)) {
        return;
    }

    if (!std::filesystem::is_regular_file(filePath)) {
        return;
    }

    // Проверяем размер файла
    std::error_code ec;
    auto fileSize = std::filesystem::file_size(filePath, ec);
    if (ec) {
        return;
    }

    const uint64_t MAX_FILE_SIZE = 100 * 1024 * 1024;
    const uint64_t MIN_FILE_SIZE = 8;

    if (fileSize > MAX_FILE_SIZE || fileSize < MIN_FILE_SIZE) {
        return;
    }

    // Логируем и сканируем
    wsprintf(buf, L"[MONITOR] Scanning: %s (%llu bytes)", filePath.c_str(), fileSize);
    LogToFile(buf);

    ScanResultData result = { 0 };
    long scanRes = ScanFile(NULL, filePath.c_str(), &result);

    if (scanRes == 0) {
        if (result.isMalicious) {
            wsprintf(buf, L"[MALWARE DETECTED] File: %s", filePath.c_str());
            LogToFile(buf);
            wsprintf(buf, L"[MALWARE DETECTED] Size: %llu bytes", fileSize);
            LogToFile(buf);
            wsprintf(buf, L"[MALWARE DETECTED] Signatures matched: %d", result.recordCount);
            LogToFile(buf);
        }
    }
    else {
        wsprintf(buf, L"[MONITOR] Scan failed with code: %d", scanRes);
        LogToFile(buf);
    }

    if (result.filePath) MIDL_user_free(result.filePath);
}

long AddMonitoredDirectory(const std::wstring& path, bool recursive) {
    std::lock_guard<std::mutex> lock(g_MonitorMutex);

    // Check if already monitored
    for (const auto& dir : g_MonitoredDirs) {
        if (dir.path == path) return 0;
    }

    HANDLE hDir = CreateFileW(
        path.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (hDir == INVALID_HANDLE_VALUE) {
        wchar_t buf[256];
        wsprintf(buf, L"AddMonitoredDirectory: Failed to open %s, error=%d",
            path.c_str(), GetLastError());
        LogToFile(buf);
        return 1;
    }

    MonitoredDirectory md;
    md.path = path;
    md.hDir = hDir;
    md.recursive = recursive;
    md.overlapped = { 0 };

    g_MonitoredDirs.push_back(md);

    wchar_t buf[256];
    wsprintf(buf, L"Monitoring directory: %s (recursive=%d)", path.c_str(), recursive);
    LogToFile(buf);

    return 0;
}

void CollectFileChanges(MonitoredDirectory& dir, BYTE* buffer, DWORD bufferSize,
    std::vector<std::wstring>& filesToProcess);

DWORD WINAPI DirectoryMonitorThread(LPVOID lpParam) {
    LogToFile(L"DirectoryMonitor: Started");

    const DWORD bufferSize = 65536;
    std::vector<uint8_t> buffer(bufferSize);

    while (g_bMonitorActive) {
        std::vector<std::wstring> filesToProcess;

        {
            std::lock_guard<std::mutex> lock(g_MonitorMutex);

            for (auto& dir : g_MonitoredDirs) {
                if (dir.hDir == INVALID_HANDLE_VALUE) continue;

                DWORD bytesReturned = 0;

                ZeroMemory(&dir.overlapped, sizeof(OVERLAPPED));
                dir.overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
                if (!dir.overlapped.hEvent) continue;

                BOOL success = ReadDirectoryChangesW(
                    dir.hDir,
                    buffer.data(),
                    bufferSize,
                    dir.recursive,
                    FILE_NOTIFY_CHANGE_FILE_NAME |
                    FILE_NOTIFY_CHANGE_DIR_NAME |
                    FILE_NOTIFY_CHANGE_SIZE |
                    FILE_NOTIFY_CHANGE_LAST_WRITE,
                    &bytesReturned,
                    &dir.overlapped,
                    NULL
                );

                if (!success) {
                    // ЛОГИРУЕМ ОШИБКУ
                    wchar_t buf[256];
                    wsprintf(buf, L"DirectoryMonitor: ReadDirectoryChangesW failed for %s, error=%d",
                        dir.path.c_str(), GetLastError());
                    LogToFile(buf);
                    CloseHandle(dir.overlapped.hEvent);
                    dir.overlapped.hEvent = NULL;
                    continue;
                }

                DWORD waitResult = WaitForSingleObject(dir.overlapped.hEvent, 500);

                if (waitResult == WAIT_OBJECT_0) {
                    DWORD bytesTransferred = 0;
                    if (GetOverlappedResult(dir.hDir, &dir.overlapped, &bytesTransferred, FALSE)) {
                        // ЛОГИРУЕМ КОЛИЧЕСТВО БАЙТ
                        wchar_t buf[256];
                        wsprintf(buf, L"DirectoryMonitor: Got %d bytes of changes", bytesTransferred);
                        LogToFile(buf);

                        CollectFileChanges(dir, buffer.data(), bytesTransferred, filesToProcess);
                    }
                }

                CloseHandle(dir.overlapped.hEvent);
                dir.overlapped.hEvent = NULL;
            }
        }

        // ЛОГИРУЕМ КОЛИЧЕСТВО ФАЙЛОВ ДЛЯ ОБРАБОТКИ
        if (!filesToProcess.empty()) {
            wchar_t buf[256];
            wsprintf(buf, L"DirectoryMonitor: Processing %zu files", filesToProcess.size());
            LogToFile(buf);
        }

        for (const auto& filePath : filesToProcess) {
            if (!g_bMonitorActive) break;
            ProcessFileNotification(filePath);
        }

        Sleep(100);
    }

    LogToFile(L"DirectoryMonitor: Exiting");
    return 0;
}

// Новая функция для сбора изменений (без длительных операций)
void CollectFileChanges(MonitoredDirectory& dir, BYTE* buffer, DWORD bufferSize,
    std::vector<std::wstring>& filesToProcess) {
    if (bufferSize == 0) return;

    FILE_NOTIFY_INFORMATION* notify = (FILE_NOTIFY_INFORMATION*)buffer;

    do {
        std::wstring fileName(notify->FileName, notify->FileNameLength / sizeof(wchar_t));
        std::wstring fullPath = dir.path + L"\\" + fileName;

        if (notify->Action == FILE_ACTION_ADDED ||
            notify->Action == FILE_ACTION_MODIFIED ||
            notify->Action == FILE_ACTION_RENAMED_NEW_NAME) {

            // ЛОГИРУЕМ ВСЕ ФАЙЛЫ
            wchar_t buf[512];
            wsprintf(buf, L"DirectoryMonitor: File changed: %s", fullPath.c_str());
            LogToFile(buf);

            if (IsScanTarget(fullPath)) {
                filesToProcess.push_back(fullPath);
            }
        }

        if (notify->NextEntryOffset == 0) break;
        notify = (FILE_NOTIFY_INFORMATION*)((BYTE*)notify + notify->NextEntryOffset);
    } while (true);
}


long RemoveMonitoredDirectory(const std::wstring& path) {
    std::lock_guard<std::mutex> lock(g_MonitorMutex);

    for (auto it = g_MonitoredDirs.begin(); it != g_MonitoredDirs.end(); ++it) {
        if (it->path == path) {
            if (it->hDir != INVALID_HANDLE_VALUE) {
                CloseHandle(it->hDir);
                CloseHandle(it->overlapped.hEvent);
            }
            g_MonitoredDirs.erase(it);

            wchar_t buf[256];
            wsprintf(buf, L"Stopped monitoring: %s", path.c_str());
            LogToFile(buf);
            return 0;
        }
    }

    return 1;  // Не найдена
}

void StartMonitoring() {
    if (g_bMonitorActive) {
        LogToFile(L"StartMonitoring: Already active, skipping");
        return;
    }

    g_bMonitorActive = true;
    g_hMonitorThread = CreateThread(NULL, 0, DirectoryMonitorThread, NULL, 0, NULL);
    if (g_hMonitorThread) {
        LogToFile(L"File monitoring started");
    }
    else {
        LogToFile(L"Failed to create monitoring thread");
        g_bMonitorActive = false;
    }
}

void StopMonitoring() {
    g_bMonitorActive = false;

    if (g_hMonitorThread) {
        WaitForSingleObject(g_hMonitorThread, 5000);
        CloseHandle(g_hMonitorThread);
        g_hMonitorThread = NULL;
    }

    std::lock_guard<std::mutex> lock(g_MonitorMutex);
    for (auto& dir : g_MonitoredDirs) {
        if (dir.hDir != INVALID_HANDLE_VALUE) {
            CloseHandle(dir.hDir);
            CloseHandle(dir.overlapped.hEvent);
        }
    }
    g_MonitoredDirs.clear();

    LogToFile(L"File monitoring stopped");
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
    std::wstring licenseId;

    {
        std::lock_guard<std::mutex> authLock(g_AuthMutex);
        if (!g_AuthTokens) {
            LogToFile(L"RequestLicenseStatus: No auth token");
            return false;
        }
        accessToken = g_AuthTokens->accessToken;
    }

    {
        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        if (g_LicenseInfo) {
            licenseId = g_LicenseInfo->licenseId;
        }
    }

    LogToFile(L"RequestLicenseStatus: Checking license...");

    HttpClient client(API_HOST, API_PORT, API_USE_HTTPS);
    if (!client.Connect()) {
        LogToFile(L"RequestLicenseStatus: Connection failed");
        // СБРАСЫВАЕМ ЛИЦЕНЗИЮ ПРИ ОШИБКЕ
        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        if (g_LicenseInfo) g_LicenseInfo->active = false;
        return false;
    }

    std::wstring body;
    if (!licenseId.empty()) {
        body = L"{\"deviceMac\":\"" + GetDeviceMac() +
            L"\",\"productId\":\"" + PRODUCT_ID +
            L"\",\"licenseId\":\"" + licenseId + L"\"}";
    }
    else {
        body = L"{\"deviceMac\":\"" + GetDeviceMac() +
            L"\",\"productId\":\"" + PRODUCT_ID + L"\"}";
    }

    if (!client.SendRequest(L"POST", LICENSE_CHECK_ENDPOINT, body, accessToken)) {
        LogToFile(L"RequestLicenseStatus: Request failed");
        // СБРАСЫВАЕМ ЛИЦЕНЗИЮ ПРИ ОШИБКЕ
        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        if (g_LicenseInfo) g_LicenseInfo->active = false;
        return false;
    }

    DWORD status = client.GetStatusCode();
    wchar_t buf[64];
    wsprintf(buf, L"RequestLicenseStatus: HTTP status = %d", status);
    LogToFile(buf);

    if (status != 200) {
        LogToFile(L"RequestLicenseStatus: Non-200 status");
        // СБРАСЫВАЕМ ЛИЦЕНЗИЮ ПРИ ОШИБКЕ СЕРВЕРА
        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        if (g_LicenseInfo) g_LicenseInfo->active = false;
        return false;
    }

    std::wstring r = client.GetResponse();
    bool licenseActivated = false;

    // ПРОВЕРЯЕМ, НЕ ВЕРНУЛ ЛИ СЕРВЕР СПИСОК ЛИЦЕНЗИЙ
    if (r.find(L"\"licenses\"") != std::wstring::npos) {
        // Сервер вернул список лицензий — нужно выбрать одну
        // Пока берем первую (позже можно добавить UI для выбора)
        LogToFile(L"RequestLicenseStatus: Multiple licenses found, selecting first");

        std::wstring firstLicenseId = ExtractJsonValue(r, L"licenseId");
        std::wstring expDate = ExtractJsonValue(r, L"expirationDate");
        std::wstring blocked = ExtractJsonValue(r, L"blocked");

        bool isBlocked = (blocked == L"true");

        if (!expDate.empty() && !isBlocked) {
            auto expiryDate = ParseExpirationDate(expDate);
            auto now = std::chrono::system_clock::now();
            bool isExpired = (expiryDate < now);

            if (!isExpired) {
                std::lock_guard<std::mutex> ll(g_LicenseMutex);
                if (!g_LicenseInfo) g_LicenseInfo = std::make_unique<LicenseInfo>();
                g_LicenseInfo->licenseId = firstLicenseId;
                g_LicenseInfo->active = true;
                g_LicenseInfo->expiryDate = expiryDate;

                LogToFile((L"RequestLicenseStatus: Selected license " + firstLicenseId).c_str());
                licenseActivated = true;
            }
        }
    }

    // ОДИНОЧНАЯ ЛИЦЕНЗИЯ (старая логика)
    if (r.find(L"\"ticketSignature\"") != std::wstring::npos ||
        r.find(L"\"licenseCode\"") != std::wstring::npos) {

        std::wstring lid = ExtractJsonValue(r, L"licenseId");
        std::wstring expDate = ExtractJsonValue(r, L"expirationDate");
        std::wstring blocked = ExtractJsonValue(r, L"blocked");

        bool isBlocked = (blocked == L"true");
        bool isExpired = false;
        std::chrono::system_clock::time_point expiryDate;

        if (!expDate.empty()) {
            expiryDate = ParseExpirationDate(expDate);  // ОДИН ВЫЗОВ
            auto now = std::chrono::system_clock::now();
            isExpired = (expiryDate < now);
        }

        bool isActive = !isBlocked && !isExpired;

        std::lock_guard<std::mutex> ll(g_LicenseMutex);
        if (!g_LicenseInfo) g_LicenseInfo = std::make_unique<LicenseInfo>();
        g_LicenseInfo->active = isActive;
        g_LicenseInfo->licenseId = lid;
        g_LicenseInfo->expiryDate = expiryDate;  // Используем уже вычисленное значение

        wchar_t buf[64];
        wsprintf(buf, L"RequestLicenseStatus: active=%d, licenseId=%s",
            isActive ? 1 : 0, lid.c_str());
        LogToFile(buf);

        licenseActivated = isActive;
    }

    // Если лицензия активирована, запускаем мониторинг
    if (licenseActivated) {
        bool hasDirs = false;
        {
            std::lock_guard<std::mutex> lock(g_MonitorMutex);
            hasDirs = !g_MonitoredDirs.empty();
        }

        if (hasDirs && !g_bMonitorActive) {
            LogToFile(L"RequestLicenseStatus: License active, starting file monitoring...");
            g_bMonitorActive = true;
            g_hMonitorThread = CreateThread(NULL, 0, DirectoryMonitorThread, NULL, 0, NULL);
            LogToFile(L"File monitoring started after license check");
        }
        return true;
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

        if (g_LicenseInfo->active) {
            std::wstring dbPath = g_ServiceDirectory + L"\\av_database.bin";
            if (LoadAndVerifyAvDatabase(dbPath, g_ServiceDirectory + L"\\public_key.pem")) {
                // База загружена, дата уже установлена внутри LoadAndVerifyAvDatabase
                LogToFile(L"AV Database loaded successfully from file");
            }
            else {
                // Внутренний обработчик уже загрузит дефолтную базу при необходимости
                LogToFile(L"AV Database loading issue, defaults may have been used.");
            }

            // Запускаем мониторинг после успешной активации
            bool hasDirs = false;
            {
                std::lock_guard<std::mutex> lock(g_MonitorMutex);
                hasDirs = !g_MonitoredDirs.empty();
            }

            if (hasDirs && !g_bMonitorActive) {
                LogToFile(L"ActivateLicense: Starting file monitoring after activation...");
                g_bMonitorActive = true;
                g_hMonitorThread = CreateThread(NULL, 0, DirectoryMonitorThread, NULL, 0, NULL);
                LogToFile(L"File monitoring started after license activation");
            }
        }

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

    // Проверяем наличие токена
    {
        std::lock_guard<std::mutex> lock(g_AuthMutex);
        if (!g_AuthTokens || g_AuthenticatedUser.empty()) {
            *daysRemaining = 0;
            *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * 16);
            if (*expiryDate) wcscpy_s(*expiryDate, 16, L"No License");
            return 1;
        }
    }

    // Запрашиваем статус лицензии с сервера
    LogToFile(L"GetLicenseInfo: Checking server for current status...");
    RequestLicenseStatus();

    // Читаем результат ПОД ЗАЩИТОЙ МЬЮТЕКСА и сразу копируем данные
    std::lock_guard<std::mutex> lock(g_LicenseMutex);
    if (g_LicenseInfo && g_LicenseInfo->active) {
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::hours>(
            g_LicenseInfo->expiryDate - now);
        long days = (long)(duration.count() / 24);

        if (days <= 0) {
            *daysRemaining = 0;
            *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * 16);
            if (*expiryDate) wcscpy_s(*expiryDate, 16, L"Expired");
            return 1;
        }

        *daysRemaining = days;

        // Копируем дату в локальную переменную, пока мьютекс еще заблокирован
        time_t expiry = std::chrono::system_clock::to_time_t(g_LicenseInfo->expiryDate);
        struct tm stm;
        localtime_s(&stm, &expiry);

        std::wstringstream wss;
        wss << std::put_time(&stm, L"%Y-%m-%d");
        std::wstring dateStr = wss.str();

        *expiryDate = (wchar_t*)MIDL_user_allocate(sizeof(wchar_t) * (dateStr.length() + 1));
        if (*expiryDate) {
            wcscpy_s(*expiryDate, dateStr.length() + 1, dateStr.c_str());
        }

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

long ScanFile(handle_t h, const wchar_t* filePath, ScanResultData* result) {
    (void)h;

    if (!result) return 1;

    // Проверяем лицензию
    {
        std::lock_guard<std::mutex> lock(g_LicenseMutex);
        if (!g_LicenseInfo || !g_LicenseInfo->active) return 2;
    }

    // Читаем файл
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return 1;

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    // ОТЛАДКА: логируем первые 8 байт
    if (data.size() >= 8) {
        wchar_t buf[512];
        wsprintf(buf, L"ScanFile: First 8 bytes of %s: %02X %02X %02X %02X %02X %02X %02X %02X",
            filePath,
            data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
        LogToFile(buf);

        // Вычисляем префикс как uint64_t
        uint64_t prefix = 0;
        memcpy(&prefix, data.data(), 8);
        wsprintf(buf, L"ScanFile: Prefix as uint64: 0x%016llX", prefix);
        LogToFile(buf);

        // Вычисляем хеш первых 8 байт
        auto hash = CalculateHash(std::vector<uint8_t>(data.begin(), data.begin() + 8));
        wsprintf(buf, L"ScanFile: Hash of first 8 bytes: %02X%02X%02X%02X...",
            hash[0], hash[1], hash[2], hash[3]);
        LogToFile(buf);

        // Сравниваем с хешем в базе
        {
            std::lock_guard<std::mutex> lock(g_AvDbMutex);
            auto it = g_AvDatabase.find(prefix);
            if (it != g_AvDatabase.end() && !it->second.empty()) {
                const auto& recordPtr = it->second[0];  // Это shared_ptr
                const auto& record = *recordPtr;         // Разыменовываем
                wchar_t buf2[512];
                wsprintf(buf2, L"ScanFile: Found in DB, DB hash: %02X%02X%02X%02X..., Match: %s",
                    record.objectSignature[0], record.objectSignature[1],
                    record.objectSignature[2], record.objectSignature[3],
                    hash == record.objectSignature ? L"YES" : L"NO");
                LogToFile(buf2);
            }
            else {
                LogToFile(L"ScanFile: Prefix not found in database");
            }
        }
    }

    // Определяем тип
    ObjectType type = DetectFileType(data);

    // Сканируем
    ScanResult scanResult = ScanStream(data, type);

    // Заполняем результат
    result->isMalicious = scanResult.isMalicious ? 1 : 0;
    result->recordCount = (long)scanResult.matchedRecords.size();
    result->filePath = (wchar_t*)MIDL_user_allocate((wcslen(filePath) + 1) * sizeof(wchar_t));
    if (result->filePath) wcscpy_s(result->filePath, wcslen(filePath) + 1, filePath);

    return 0;
}

long ScanDirectory(handle_t h, const wchar_t* dirPath, ScanResultData* results, long* resultCount) {
    (void)h;

    if (!results || !resultCount) return 1;

    // Проверяем лицензию
    {
        std::lock_guard<std::mutex> lock(g_LicenseMutex);
        if (!g_LicenseInfo || !g_LicenseInfo->active) return 2;
    }

    *resultCount = 0;
    const long MAX_RESULTS = 100;

    try {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dirPath)) {
            if (*resultCount >= MAX_RESULTS) break;

            if (entry.is_regular_file()) {
                ScanResultData fileResult = { 0 };
                long scanRes = ScanFile(h, entry.path().c_str(), &fileResult);

                if (scanRes == 0 && fileResult.isMalicious) {
                    // Copy result to array
                    results[*resultCount] = fileResult;
                    (*resultCount)++;
                }
                else {
                    // Clean up if not malicious
                    if (fileResult.filePath) {
                        MIDL_user_free(fileResult.filePath);
                    }
                }
            }
        }
    }
    catch (const std::exception& e) {
        // Log error
        std::wstring wError(e.what(), e.what() + strlen(e.what()));
        LogToFile((L"ScanDirectory exception: " + wError).c_str());
        return 1;
    }

    return 0;
}

long GetAvDbInfo(handle_t h, AvDbInfo* info) {
    (void)h;

    if (!info) return 1;

    std::lock_guard<std::mutex> lock(g_AvDbMutex);

    info->recordCount = (long)g_AvDbRecordCount;
    info->releaseDate = (wchar_t*)MIDL_user_allocate((g_AvDbReleaseDate.length() + 1) * sizeof(wchar_t));
    if (info->releaseDate) wcscpy_s(info->releaseDate, g_AvDbReleaseDate.length() + 1, g_AvDbReleaseDate.c_str());

    return 0;
}

// RPC-обертки для управления мониторингом
long AddMonitoredDir(handle_t h, const wchar_t* dirPath, long recursive) {
    (void)h;

    // Проверяем лицензию
    {
        std::lock_guard<std::mutex> lock(g_LicenseMutex);
        if (!g_LicenseInfo || !g_LicenseInfo->active) return 2;
    }

    return AddMonitoredDirectory(dirPath, recursive != 0);
}

long RemoveMonitoredDir(handle_t h, const wchar_t* dirPath) {
    (void)h;

    // Проверяем лицензию
    {
        std::lock_guard<std::mutex> lock(g_LicenseMutex);
        if (!g_LicenseInfo || !g_LicenseInfo->active) return 2;
    }

    return RemoveMonitoredDirectory(dirPath);
}

long GetMonitoredDirs(handle_t h, wchar_t** dirList) {
    (void)h;

    if (!dirList) return 1;

    // Проверяем лицензию
    {
        std::lock_guard<std::mutex> lock(g_LicenseMutex);
        if (!g_LicenseInfo || !g_LicenseInfo->active) return 2;
    }

    std::lock_guard<std::mutex> lock(g_MonitorMutex);

    // Формируем строку со списком директорий
    std::wstring listStr;
    for (const auto& dir : g_MonitoredDirs) {
        if (!listStr.empty()) listStr += L"|";
        listStr += dir.path;
    }

    *dirList = (wchar_t*)MIDL_user_allocate((listStr.length() + 1) * sizeof(wchar_t));
    if (*dirList) wcscpy_s(*dirList, listStr.length() + 1, listStr.c_str());

    return 0;
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

    g_hUpdateEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!g_hUpdateEvent) {
        LogToFile(L"[UPDATE] Failed to create update event");
    }
    else {
        g_hUpdateThread = CreateThread(NULL, 0, UpdateThread, NULL, 0, NULL);
        if (!g_hUpdateThread) {
            LogToFile(L"[UPDATE] Failed to create update thread");
        }
    }

    g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
    g_ServiceStatus.dwCheckPoint = 0;
    g_ServiceStatus.dwWaitHint = 0;
    g_ServiceStatus.dwWin32ExitCode = NO_ERROR;
    SetServiceStatus(g_StatusHandle, &g_ServiceStatus);
    LogToFile(L"ServiceMain: RUNNING");

    {
        std::wstring dbPath = g_ServiceDirectory + L"\\av_database.bin";
        if (LoadAndVerifyAvDatabase(dbPath, g_ServiceDirectory + L"\\public_key_cryptoapi.pem")) {
            // База загружена, дата уже установлена внутри LoadAndVerifyAvDatabase
            LogToFile(L"AV Database loaded successfully from file");
        }
        else {
            // Внутренний обработчик уже загрузит дефолтную базу при необходимости
            LogToFile(L"AV Database loading issue, defaults may have been used.");
        }
    }

    // Добавляем директорию для мониторинга и запускаем мониторинг
    AddMonitoredDirectory(L"C:\\Users", true);
    StartMonitoring();

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

    if (g_hUpdateEvent) {
        SetEvent(g_hUpdateEvent);
    }

    if (g_hUpdateThread) {
        WaitForSingleObject(g_hUpdateThread, 5000);
        CloseHandle(g_hUpdateThread);
        g_hUpdateThread = NULL;
    }

    if (g_hUpdateEvent) {
        CloseHandle(g_hUpdateEvent);
        g_hUpdateEvent = NULL;
    }

    // Очистка временного криптоконтейнера
    HCRYPTPROV hProv = 0;
    LPCWSTR containerName = L"TrayAppTempContainer_{12345678-1234-1234-1234-123456789012}";
    if (CryptAcquireContext(&hProv, containerName, MS_ENHANCED_PROV, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        CryptReleaseContext(hProv, 0);
        CryptAcquireContext(&hProv, containerName, MS_ENHANCED_PROV, PROV_RSA_AES, CRYPT_DELETEKEYSET);
    }

    StopMonitoring();
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

                // Проверяем, есть ли уже ЖИВОЙ процесс в этой сессии
                bool hasRunningProcess = false;
                {
                    std::lock_guard<std::mutex> lock(g_ProcessMutex);
                    auto it = g_SessionProcesses.find(ps[i].SessionId);
                    if (it != g_SessionProcesses.end()) {
                        for (HANDLE h : it->second) {
                            DWORD exitCode = 0;
                            if (GetExitCodeProcess(h, &exitCode) && exitCode == STILL_ACTIVE) {
                                hasRunningProcess = true;
                                break;
                            }
                        }
                    }
                }

                // Запускаем только если нет живого процесса
                if (!hasRunningProcess && ps[i].State == WTSActive) {
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

        // Ждем и проверяем, жив ли процесс
        Sleep(3000);
        DWORD exitCode = 0;
        if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
            if (exitCode == STILL_ACTIVE) {
                LogToFile(L"StartAppInSession: TrayApp.exe is running");
            }
            else {
                wsprintf(buf, L"StartAppInSession: TrayApp.exe exited with code %d", exitCode);
                LogToFile(buf);
            }
        }

        ProtectProcessFromAdmins(pi.hProcess);
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
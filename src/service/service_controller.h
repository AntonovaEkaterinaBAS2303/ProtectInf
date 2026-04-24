#pragma once

#include <windows.h>
#include <string>

// Имя службы
#define SERVICE_NAME L"TrayAppService"
#define SERVICE_DISPLAY_NAME L"Tray Application Service"
#define SERVICE_DESCRIPTION L"Windows service for managing TrayApp GUI applications"

// RPC endpoint
#define RPC_ENDPOINT L"TrayAppServiceRPC"

// Класс для управления службой
class ServiceController
{
public:
    ServiceController();
    ~ServiceController();

    // Установка службы
    DWORD InstallService();

    // Удаление службы
    DWORD UninstallService();

    // Запуск службы
    DWORD StartService();

    // Остановка службы
    DWORD StopService();

    // Проверка состояния
    DWORD GetServiceStatus(DWORD& dwCurrentState);

    bool IsRunning();
    bool IsStopped();
    bool IsInstalled();

private:
    SC_HANDLE m_hSCManager;
    SC_HANDLE m_hService;
    SERVICE_STATUS m_serviceStatus;

    void OpenSCManager();
    void OpenService();
    void CloseHandles();
};
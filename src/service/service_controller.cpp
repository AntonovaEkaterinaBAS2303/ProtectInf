#include "service_controller.h"
#include <iostream>

ServiceController::ServiceController()
    : m_hSCManager(NULL)
    , m_hService(NULL)
{
    ZeroMemory(&m_serviceStatus, sizeof(m_serviceStatus));
}

ServiceController::~ServiceController()
{
    CloseHandles();
}

void ServiceController::OpenSCManager()
{
    m_hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!m_hSCManager)
    {
        throw std::runtime_error("Failed to open Service Control Manager");
    }
}

void ServiceController::OpenService()
{
    m_hService = OpenService(
        m_hSCManager,
        SERVICE_NAME,
        SERVICE_ALL_ACCESS
    );
}

void ServiceController::CloseHandles()
{
    if (m_hService)
    {
        CloseServiceHandle(m_hService);
        m_hService = NULL;
    }
    if (m_hSCManager)
    {
        CloseServiceHandle(m_hSCManager);
        m_hSCManager = NULL;
    }
}

DWORD ServiceController::InstallService()
{
    try
    {
        OpenSCManager();

        // Полный путь к исполняемому файлу службы
        WCHAR szPath[MAX_PATH];
        GetModuleFileName(NULL, szPath, MAX_PATH);

        m_hService = CreateService(
            m_hSCManager,
            SERVICE_NAME,
            SERVICE_DISPLAY_NAME,
            SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS,
            SERVICE_AUTO_START,
            SERVICE_ERROR_NORMAL,
            szPath,
            NULL, NULL, NULL, NULL, NULL
        );

        if (!m_hService)
        {
            DWORD dwError = GetLastError();
            if (dwError == ERROR_SERVICE_EXISTS)
            {
                return ERROR_SERVICE_EXISTS;
            }
            return dwError;
        }

        // Установить описание службы
        SERVICE_DESCRIPTION sd;
        sd.lpDescription = (LPWSTR)SERVICE_DESCRIPTION;
        ChangeServiceConfig2(m_hService, SERVICE_CONFIG_DESCRIPTION, &sd);

        // Отключить обработку Stop и Shutdown
        SERVICE_FAILURE_ACTIONS sfa;
        SC_ACTION actions[3];

        actions[0].Type = SC_ACTION_NONE;
        actions[0].Delay = 0;
        actions[1].Type = SC_ACTION_NONE;
        actions[1].Delay = 0;
        actions[2].Type = SC_ACTION_NONE;
        actions[2].Delay = 0;

        sfa.dwResetPeriod = INFINITE;
        sfa.lpRebootMsg = NULL;
        sfa.lpCommand = NULL;
        sfa.cActions = 3;
        sfa.lpsaActions = actions;

        ChangeServiceConfig2(m_hService, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa);

        CloseHandles();
        return ERROR_SUCCESS;
    }
    catch (...)
    {
        CloseHandles();
        return GetLastError();
    }
}

DWORD ServiceController::UninstallService()
{
    try
    {
        OpenSCManager();
        OpenService();

        if (!DeleteService(m_hService))
        {
            return GetLastError();
        }

        CloseHandles();
        return ERROR_SUCCESS;
    }
    catch (...)
    {
        CloseHandles();
        return GetLastError();
    }
}

DWORD ServiceController::StartService()
{
    try
    {
        OpenSCManager();
        OpenService();

        if (!::StartService(m_hService, 0, NULL))
        {
            return GetLastError();
        }

        // Ждать запуска службы
        Sleep(1000);

        SERVICE_STATUS ssStatus;
        QueryServiceStatus(m_hService, &ssStatus);

        int retries = 0;
        while (ssStatus.dwCurrentState == SERVICE_START_PENDING && retries < 30)
        {
            Sleep(1000);
            QueryServiceStatus(m_hService, &ssStatus);
            retries++;
        }

        CloseHandles();
        return ERROR_SUCCESS;
    }
    catch (...)
    {
        CloseHandles();
        return GetLastError();
    }
}

DWORD ServiceController::StopService()
{
    try
    {
        OpenSCManager();
        OpenService();

        SERVICE_STATUS ssStatus;
        ControlService(m_hService, SERVICE_CONTROL_STOP, &ssStatus);

        // Ждать остановки службы
        Sleep(1000);
        QueryServiceStatus(m_hService, &ssStatus);

        int retries = 0;
        while (ssStatus.dwCurrentState == SERVICE_STOP_PENDING && retries < 30)
        {
            Sleep(1000);
            QueryServiceStatus(m_hService, &ssStatus);
            retries++;
        }

        CloseHandles();
        return ERROR_SUCCESS;
    }
    catch (...)
    {
        CloseHandles();
        return GetLastError();
    }
}

DWORD ServiceController::GetServiceStatus(DWORD& dwCurrentState)
{
    try
    {
        OpenSCManager();
        OpenService();

        SERVICE_STATUS ssStatus;
        if (!QueryServiceStatus(m_hService, &ssStatus))
        {
            return GetLastError();
        }

        dwCurrentState = ssStatus.dwCurrentState;
        CloseHandles();
        return ERROR_SUCCESS;
    }
    catch (...)
    {
        CloseHandles();
        return GetLastError();
    }
}

bool ServiceController::IsRunning()
{
    DWORD dwState = 0;
    if (GetServiceStatus(dwState) == ERROR_SUCCESS)
    {
        return (dwState == SERVICE_RUNNING);
    }
    return false;
}

bool ServiceController::IsStopped()
{
    DWORD dwState = 0;
    if (GetServiceStatus(dwState) == ERROR_SUCCESS)
    {
        return (dwState == SERVICE_STOPPED);
    }
    return true;
}

bool ServiceController::IsInstalled()
{
    try
    {
        OpenSCManager();
        OpenService();
        CloseHandles();
        return true;
    }
    catch (...)
    {
        return false;
    }
}
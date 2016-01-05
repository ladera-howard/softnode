// softnode.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include <setupapi.h>
#include <cfgmgr32.h>

#define MY_SOFTNODE_DEVID      L"SOFTNODE\\{12202015-1222-0000-0123-456789ABCDEF}"
#define MY_SOFTNODE_NAME       L"My soft node device"

// Helpers:
void PrintGUID(GUID* guid)
{
	printf("GUID: {0x%08x, 0x%04x, 0x%04x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x}\n",
		guid->Data1,
		guid->Data2,
		guid->Data3,
		guid->Data4[0], guid->Data4[1], guid->Data4[2], guid->Data4[3],
		guid->Data4[4], guid->Data4[5], guid->Data4[6], guid->Data4[7]);
}

BOOL CreateSoftNode(	// Create root-enumerated device (software device)
	TCHAR* InfPath,		// Full path and file name of the INF to install
	TCHAR* DevId,		// Hardware ID for the device node instance
	TCHAR* DevName		// Device display 
	)
{
	BOOL bRet = FALSE;

	if (!InfPath || !InfPath[0])
	{
		_tprintf(_T("ERROR: Invalid INF file path\n"));
		return FALSE;
	}
	if (!DevId || !DevId[0])
	{
		_tprintf(_T("ERROR: Invalid hardware ID string\n"));
		return FALSE;
	}

	//
	// Use the INF File to extract the Class GUID.
	//
	GUID ClassGUID;
	TCHAR ClassName[MAX_CLASS_NAME_LEN];
	if (!SetupDiGetINFClass(InfPath, &ClassGUID, ClassName, sizeof(ClassName) / sizeof(ClassName[0]), 0))
	{
		_tprintf(_T("ERROR: SetupDiGetINFClass fails, error code = 0x%08x\n"), GetLastError());
		return FALSE;
	}
	_tprintf(_T("Class Name: %s\n"), ClassName);
	PrintGUID(&ClassGUID);


	//
	// Create the container for the to-be-created Device Information Element.
	//
	HDEVINFO DeviceInfoSet = INVALID_HANDLE_VALUE;
	SP_DEVINFO_DATA DeviceInfoData;

	DeviceInfoSet = SetupDiCreateDeviceInfoList(&ClassGUID, 0);
	if (DeviceInfoSet == INVALID_HANDLE_VALUE)
	{
		_tprintf(_T("ERROR: SetupDiCreateDeviceInfoList fails, error code = 0x%08x\n"), GetLastError());
		return FALSE;
	}
		
	//
	// Now create the element. Use the Class GUID and Name from the INF file.
	//
	ZeroMemory(&DeviceInfoData, sizeof(SP_DEVINFO_DATA));
	DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	if (!SetupDiCreateDeviceInfo(DeviceInfoSet,
		ClassName,
		&ClassGUID,
		NULL,
		0,
		DICD_GENERATE_ID,
		&DeviceInfoData))
	{
		_tprintf(_T("ERROR: SetupDiCreateDeviceInfo fails, error code = 0x%08x\n"), GetLastError());
		return FALSE;
	}

	//
	// Add device properties to the info data
	//

	// Add HardwareID property.
	if (!SetupDiSetDeviceRegistryProperty(DeviceInfoSet,
		&DeviceInfoData,
		SPDRP_HARDWAREID,
		(LPBYTE)DevId,
		DWORD((_tcslen(DevId) + 1 + 1)*sizeof(TCHAR))))
	{
		_tprintf(_T("ERROR: SetupDiSetDeviceRegistryProperty(SPDRP_HARDWAREID) fails, error code = 0x%08x\n"), GetLastError());
		return FALSE;
	}

	//
	// Add FriendlyName property.
	//
	if (!SetupDiSetDeviceRegistryProperty(DeviceInfoSet,
		&DeviceInfoData,
		SPDRP_FRIENDLYNAME,
		(LPBYTE)DevName,
		DWORD((_tcslen(DevName) + 1 + 1)*sizeof(TCHAR))))
	{
		_tprintf(_T("ERROR: SetupDiSetDeviceRegistryProperty(SPDRP_FRIENDLYNAME) fails, error code = 0x%08x\n"), GetLastError());
		return FALSE;
	}

	//
	// Creat root-enumerated device
	//
	if (!SetupDiCallClassInstaller(DIF_REGISTERDEVICE,
		DeviceInfoSet,
		&DeviceInfoData))
	{
		_tprintf(_T("ERROR: SetupDiCallClassInstaller fails, error code = 0x%08x\n"), GetLastError());
		return FALSE;
	}

	return TRUE;
}

BOOL RemoveCallback(_In_ HDEVINFO DeviceInfoSet, _In_ PSP_DEVINFO_DATA DeviceInfoData)
/*++

Routine Description:

	Callback for use by Remove
	Invokes DIF_REMOVE uses SetupDiCallClassInstaller so cannot be done for remote devices
	Don't use CM_xxx API's, they bypass class/co-installers and this is bad.

Arguments:

	DeviceInfoSet    - uniquely identify the device
	DeviceInfoData	

Return Value:

 TRUE/FALSE

--*/
{
	SP_REMOVEDEVICE_PARAMS rmdParams;
	SP_DEVINSTALL_PARAMS devParams;

	//
	// need hardware ID before trying to remove, as we wont have it after
	//
	TCHAR devID[MAX_DEVICE_ID_LEN];
	SP_DEVINFO_LIST_DETAIL_DATA devInfoListDetail;

	devInfoListDetail.cbSize = sizeof(devInfoListDetail);
	if ((!SetupDiGetDeviceInfoListDetail(DeviceInfoSet, &devInfoListDetail)) ||
		(CM_Get_Device_ID_Ex(DeviceInfoData->DevInst, devID, MAX_DEVICE_ID_LEN, 0, devInfoListDetail.RemoteMachineHandle) != CR_SUCCESS)) {
		//
		// skip this
		//
		_tprintf(_T("ERROR: SetupDiGetDeviceInfoListDetail fails, error code = 0x%08x\n"), GetLastError());
		return FALSE;
	}

	rmdParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
	rmdParams.ClassInstallHeader.InstallFunction = DIF_REMOVE;
	rmdParams.Scope = DI_REMOVEDEVICE_GLOBAL;
	rmdParams.HwProfile = 0;
	if (!SetupDiSetClassInstallParams(DeviceInfoSet, DeviceInfoData, &rmdParams.ClassInstallHeader, sizeof(rmdParams)) ||
		!SetupDiCallClassInstaller(DIF_REMOVE, DeviceInfoSet, DeviceInfoData)) {
		//
		// failed to invoke DIF_REMOVE
		//
		_tprintf(_T("ERROR: fails to remove device, error code = 0x%08x\n"), GetLastError());
		return FALSE;
	}
	else {
		//
		// see if device needs reboot
		//
		devParams.cbSize = sizeof(devParams);
		if (SetupDiGetDeviceInstallParams(DeviceInfoSet, DeviceInfoData, &devParams) && (devParams.Flags & (DI_NEEDRESTART | DI_NEEDREBOOT))) {
			//
			// reboot required
			//
			_tprintf(_T("Waring: device was removed but need reboot..\n"));
		}
		else {
			//
			// appears to have succeeded
			//
			_tprintf(_T("Device was removed, no need reboot.\n"));
		}
	}

	_tprintf(_T("Device id %s was removed\n"), devID);

	return TRUE;
}

BOOL RemoveSoftNode(
	TCHAR* HwId
	)
{
	// Enumerate through all devices and find the device matching the HwId

	HDEVINFO DeviceInfoSet = INVALID_HANDLE_VALUE;

	DeviceInfoSet = SetupDiGetClassDevs(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	if (INVALID_HANDLE_VALUE == DeviceInfoSet)
	{
		_tprintf(_T("ERROR: SetupDiGetClassDevs fails, error code = 0x%08x\n"), GetLastError());
		return FALSE;
	}

	DWORD MemberIndex = 0;
	SP_DEVINFO_DATA DeviceInfoData;
	ZeroMemory(&DeviceInfoData, sizeof(SP_DEVINFO_DATA));
	DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	while (SetupDiEnumDeviceInfo(DeviceInfoSet, MemberIndex, &DeviceInfoData))
	{
		TCHAR devID[MAX_DEVICE_ID_LEN];
		if (SetupDiGetDeviceRegistryProperty(
			DeviceInfoSet,
			&DeviceInfoData,
			SPDRP_HARDWAREID,
			NULL,
			(PBYTE)devID,
			sizeof(devID),
			NULL
			))
		{
			_tprintf(_T("DevID: %s\n"), devID);
			if (_tcsicmp(devID, HwId) == 0)
			{
				RemoveCallback(DeviceInfoSet, &DeviceInfoData);
			}
		}
		MemberIndex++;
	}
	return TRUE;
}

int _tmain(int argc, _TCHAR* argv[])
{
	if (argc == 1)
	{
		CreateSoftNode(L"c:\\windows\\inf\\c_swdevice.inf", MY_SOFTNODE_DEVID, MY_SOFTNODE_NAME);
	}
	else
	{
		RemoveSoftNode(MY_SOFTNODE_DEVID);
	}
	return 0;
}


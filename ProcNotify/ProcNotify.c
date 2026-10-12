#include <windows.h>
#include "..\KProcNotify\ProcNotifyCommon.h"

int main()
{
	HANDLE hDevice = CreateFileW(L"\\\\.\\KProcNotify", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDevice == INVALID_HANDLE_VALUE) {
		wprintf(L"Failed to open device (0x%08X)\n", GetLastError());
		return 1;
	}

	CloseHandle(hDevice);
    return 0;
}

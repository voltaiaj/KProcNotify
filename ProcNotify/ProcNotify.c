#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include "..\KProcNotify\ProcNotifyCommon.h"

void DisplayTime(ULONGLONG);
void DisplayData(const BYTE*, DWORD);

int main()
{
	HANDLE hDevice = CreateFileW(L"\\\\.\\KProcNotify", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDevice == INVALID_HANDLE_VALUE) {
		wprintf(L"Failed to open device (0x%08X)\n", GetLastError());
		return 1;
	}

	HANDLE hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	if (!hEvent) {
		wprintf(L"Failed to create event (0x%08X)\n", GetLastError());
		CloseHandle(hDevice);
		return 1;
	}

	DWORD bytesReturned;
	BOOL result = DeviceIoControl(
		hDevice, 
		IOCTL_KPROC_REGISTER_PROCESS_NOTIFY_EVENT, 
		&hEvent, 
		sizeof(hEvent), 
		NULL, 
		0, 
		NULL, 
		NULL);

	if (!result) {
		wprintf(L"Failed to register process notify event (0x%08X)\n", GetLastError());
		CloseHandle(hEvent);
		CloseHandle(hDevice);
		return 1;
	}

	BYTE buffer[1 << 16];
	for (;;) {
		DWORD waitResult = WaitForSingleObject(hEvent, INFINITE);
		if (waitResult == WAIT_FAILED) {
			wprintf(L"WaitForSingleObject failed (0x%08X)\n", GetLastError());
			break;
		}
		if (waitResult == WAIT_TIMEOUT) {
			wprintf(L"WaitForSingleObject timed out\n");
			continue;
		}
		bytesReturned = 0;
		result = DeviceIoControl(
			hDevice, 
			IOCTL_KPROC_READ_PROCESS_NOTIFY, 
			NULL, 
			0, 
			buffer, 
			sizeof(buffer), 
			&bytesReturned, 
			NULL);

		if (!result) {
			wprintf(L"DeviceIoControl failed (0x%08X)\n", GetLastError());
			break;
		}

		if (bytesReturned > 0) 
			DisplayData((const BYTE*)buffer, bytesReturned);
	}

	CloseHandle(hEvent);
	CloseHandle(hDevice);
    return 0;
}

void DisplayTime(ULONGLONG time)
{
	FILETIME ft;
	SYSTEMTIME st;
	ULARGE_INTEGER uli;
	uli.QuadPart = time;
	ft.dwLowDateTime = uli.LowPart;
	ft.dwHighDateTime = uli.HighPart;
	FileTimeToSystemTime(&ft, &st);
	wprintf(L"%02d/%02d/%04d %02d:%02d:%02d.%03d ",
		st.wMonth, st.wDay, st.wYear,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

void DisplayData(const BYTE* buffer, DWORD size) {
	while (size > 0) {
		MonitorEvent* event = (MonitorEvent*)buffer;
		DisplayTime(event->TimeStamp);
		switch (event->EventType) {
		case ProcessCreated:
		{
			const int commandLineLengthChars = (int)event->Data.ProcessCreated.CommandLineLength;

			wprintf(L"Process Created: PID=%lu, Parent PID=%lu, Command Line=%.*ls\n",
				event->Data.ProcessCreated.ProcessId,
				event->Data.ProcessCreated.ParentProcessId,
				commandLineLengthChars,
				event->Data.ProcessCreated.CommandLine);
			break;
		}
		default:
			wprintf(L"Unknown event type: %d\n", event->EventType);
			break;
		}
		buffer += event->Size;
		size -= event->Size;
	}
}

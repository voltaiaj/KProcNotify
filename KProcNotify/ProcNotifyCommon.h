#pragma once

#define KPROC_NOTIFY_DEVICE 0x8000
#define IOCTL_KPROC_REGISTER_PROCESS_NOTIFY_EVENT CTL_CODE(KPROC_NOTIFY_DEVICE, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_KPROC_READ_PROCESS_NOTIFY CTL_CODE(KPROC_NOTIFY_DEVICE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _ProcessCreatedInfo
{
	ULONGLONG CreatedTime;
	ULONG ProcessId;
	ULONG ParentProcessId;
	ULONG CommandLineLength;
	WCHAR CommandLine[1];
} ProcessCreatedInfo;

typedef enum _MonitorEventType
{
	ProcessCreated
} MonitorEventType;

typedef struct _MonitorEvent
{
	MonitorEventType EventType;
	ULONGLONG TimeStamp;
	USHORT Size;
	union
	{
		ProcessCreatedInfo ProcessCreated;
	} Data;
} MonitorEvent;
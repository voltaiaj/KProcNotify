#include <ntifs.h>
#include "ProcNotifyCommon.h"

#define DRIVER_TAG 'KPNO'
#define DRIVER_PREFIX "KProcNotify: "

#define MAX_EVENTS 0x1024

FAST_MUTEX g_Mutex;

typedef struct _Globals
{
	LIST_ENTRY ItemsHead;
	int ItemCount;
	PKEVENT ProcessNotifyEvent;
} Globals;

Globals g_Globals;

typedef struct _MonitorEventFull
{
	LIST_ENTRY ListEntry;
	MonitorEventType EventType;
	ULONGLONG TimeStamp;
	USHORT Size;
	union
	{
		ProcessCreatedInfo ProcessCreated;
	} Data;
} MonitorEventFull;

void DriverUnload(PDRIVER_OBJECT);
NTSTATUS KProcNotifyCreateClose(PDEVICE_OBJECT, PIRP);
NTSTATUS KProcNotifyDeviceControl(PDEVICE_OBJECT, PIRP);
void ProcessNotifyCallback(PEPROCESS, HANDLE, PPS_CREATE_NOTIFY_INFO);
void PushToEventQueue(MonitorEventFull*);
MonitorEvent* PopFromEventQueue();
MonitorEvent* ConvertMonitorEventFullToMonitorEvent(MonitorEventFull*);

LARGE_INTEGER GetCurrentTime()
{
	LARGE_INTEGER currentTime;
	KeQuerySystemTime(&currentTime);
	return currentTime;
}



NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	// Set up the driver unload routine
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT deviceObject = NULL;
	BOOLEAN symbolicLinkCreated = FALSE;
	do {
		UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\KProcNotify");

		status = IoCreateDevice(DriverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "Failed to create device (0x%08X)\n", status));
			break;
		}

		UNICODE_STRING symbolicLinkName = RTL_CONSTANT_STRING(L"\\??\\KProcNotify");
		status = IoCreateSymbolicLink(&symbolicLinkName, &deviceName);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "Failed to create symbolic link (0x%08X)\n", status));
			break;
		}
		symbolicLinkCreated = TRUE;

		deviceObject->Flags |= DO_BUFFERED_IO;

		InitializeListHead(&g_Globals.ItemsHead);
		g_Globals.ItemCount = 0;

		ExInitializeFastMutex(&g_Mutex);

		status = PsSetCreateProcessNotifyRoutineEx(ProcessNotifyCallback, FALSE);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "Failed to set process notify routine (0x%08X)\n", status));
			break;
		}

		DriverObject->DriverUnload = DriverUnload;
		DriverObject->MajorFunction[IRP_MJ_CREATE] = KProcNotifyCreateClose;
		DriverObject->MajorFunction[IRP_MJ_CLOSE] = KProcNotifyCreateClose;
		DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = KProcNotifyDeviceControl;
	} while (FALSE);
	
	if (!NT_SUCCESS(status)) {		
		if (symbolicLinkCreated) {
			UNICODE_STRING symbolicLinkName = RTL_CONSTANT_STRING(L"\\??\\KProcNotify");
			IoDeleteSymbolicLink(&symbolicLinkName);
		}
		if (deviceObject) {
			IoDeleteDevice(deviceObject);
		}
	}

	// Perform any necessary initialization here
	return status;
}

void DriverUnload(PDRIVER_OBJECT DriverObject)
{
	PsSetCreateProcessNotifyRoutineEx(ProcessNotifyCallback, TRUE);

	while (!IsListEmpty(&g_Globals.ItemsHead)) {
		PLIST_ENTRY entry = RemoveHeadList(&g_Globals.ItemsHead);
		MonitorEventFull* event = CONTAINING_RECORD(entry, MonitorEventFull, ListEntry);
		ExFreePool2(event, DRIVER_TAG, NULL, 0);
		g_Globals.ItemCount--;
	}

	if (g_Globals.ProcessNotifyEvent) {
		ObDereferenceObject(g_Globals.ProcessNotifyEvent);
		g_Globals.ProcessNotifyEvent = NULL;
	}

	UNICODE_STRING symbolicLinkName = RTL_CONSTANT_STRING(L"\\??\\KProcNotify");
	IoDeleteSymbolicLink(&symbolicLinkName);
	IoDeleteDevice(DriverObject->DeviceObject);
}

NTSTATUS KProcNotifyCreateClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS KProcNotifyDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	UNREFERENCED_PARAMETER(DeviceObject);
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
	ULONG outputBufferLength = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
	ULONG written = 0;
	NTSTATUS status = STATUS_SUCCESS;

	switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
		case IOCTL_KPROC_REGISTER_PROCESS_NOTIFY_EVENT: {
			KdPrint((DRIVER_PREFIX "Register Process Notify Event\n"));

			if (irpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(HANDLE)) {
				KdPrint((DRIVER_PREFIX "Input buffer too small for event handle\n"));
				status = STATUS_BUFFER_TOO_SMALL;
				break;
			}

			HANDLE hEvent = *(HANDLE*)Irp->AssociatedIrp.SystemBuffer;
			PKEVENT newEvent;
			status = ObReferenceObjectByHandle(
				hEvent, 
				EVENT_MODIFY_STATE, 
				*ExEventObjectType, 
				UserMode, 
				(PVOID*)&newEvent, 
				NULL);
			
			if (NT_SUCCESS(status)) {
				ExAcquireFastMutex(&g_Mutex);
				PKEVENT oldEvent = g_Globals.ProcessNotifyEvent;
				g_Globals.ProcessNotifyEvent = newEvent;
				ExReleaseFastMutex(&g_Mutex);
				
				if (oldEvent) {
					ObDereferenceObject(oldEvent);
				}
			}
			break;
		}
		case IOCTL_KPROC_READ_PROCESS_NOTIFY: {
			KdPrint((DRIVER_PREFIX "Read Process Notify\n"));
			

			MonitorEvent* event;
			while (outputBufferLength >= sizeof(MonitorEvent)) {
				event = PopFromEventQueue();
				if (!event) {
					KdPrint((DRIVER_PREFIX "No more events to return\n"));
					break;
				}
				USHORT eventSize = (event->Size < outputBufferLength) ? event->Size : (USHORT)outputBufferLength;
				RtlCopyMemory((PUCHAR)Irp->AssociatedIrp.SystemBuffer + written, event, eventSize);
				written += eventSize;
				outputBufferLength -= eventSize;
				ExFreePool2(event, DRIVER_TAG, NULL, 0);
			}
			break;
		}
	}
	return status;
}

void ProcessNotifyCallback(PEPROCESS Process, HANDLE ProcessId, PPS_CREATE_NOTIFY_INFO CreateInfo)
{
	UNREFERENCED_PARAMETER(Process);
	KdPrint((DRIVER_PREFIX "ProcessNotifyCallback called\n"));
	USHORT extraSize = 0;
	if (CreateInfo && CreateInfo->CommandLine) {
		extraSize = (USHORT)CreateInfo->CommandLine->Length;
	}

	MonitorEventFull* event = (MonitorEventFull*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MonitorEventFull) + extraSize, DRIVER_TAG);
	if (!event) {
		KdPrint((DRIVER_PREFIX "Failed to allocate memory for Process event\n"));
		return;
	}

	if (CreateInfo) {
		KdPrint((DRIVER_PREFIX "Process created. PID=%p\n", ProcessId));
		event->EventType = ProcessCreated;
		event->TimeStamp = GetCurrentTime().QuadPart;
		event->Size = sizeof(MonitorEventFull) + extraSize;
		event->Data.ProcessCreated.CreatedTime = GetCurrentTime().QuadPart;
		event->Data.ProcessCreated.ProcessId = (ULONG)(ULONG_PTR)ProcessId;
		event->Data.ProcessCreated.ParentProcessId = (ULONG)(ULONG_PTR)CreateInfo->ParentProcessId;
		if (CreateInfo->CommandLine && CreateInfo->CommandLine->Length > 0) {
			SIZE_T srcChars = CreateInfo->CommandLine->Length / sizeof(WCHAR);
			if (srcChars >= MAX_COMMAND_LINE_LENGTH) {
				srcChars = MAX_COMMAND_LINE_LENGTH - 1;
			}
			RtlCopyMemory(event->Data.ProcessCreated.CommandLine, CreateInfo->CommandLine->Buffer, srcChars * sizeof(WCHAR));
			event->Data.ProcessCreated.CommandLine[srcChars] = L'\0';
			event->Data.ProcessCreated.CommandLineLength = (ULONG)srcChars;
		}
		else {
			event->Data.ProcessCreated.CommandLine[0] = L'\0';
			event->Data.ProcessCreated.CommandLineLength = 0;
		}
		PushToEventQueue(event);
	}
}

void PushToEventQueue(MonitorEventFull* event)
{
	ExAcquireFastMutex(&g_Mutex);

	if (g_Globals.ItemCount >= MAX_EVENTS) {
		KdPrint((DRIVER_PREFIX "Event queue is full. Making room by removing oldest event.\n"));
		PLIST_ENTRY entry = RemoveHeadList(&g_Globals.ItemsHead);
		MonitorEventFull* oldEvent = CONTAINING_RECORD(entry, MonitorEventFull, ListEntry);
		ExFreePoolWithTag(oldEvent, DRIVER_TAG);
		g_Globals.ItemCount--;
	}

	InsertTailList(&g_Globals.ItemsHead, &event->ListEntry);
	g_Globals.ItemCount++;

	if (g_Globals.ProcessNotifyEvent) {
		KeSetEvent(g_Globals.ProcessNotifyEvent, IO_NO_INCREMENT, FALSE);
	}

	ExReleaseFastMutex(&g_Mutex);
}

MonitorEvent* ConvertMonitorEventFullToMonitorEvent(MonitorEventFull* fullEvent)
{
	MonitorEvent* event = (MonitorEvent*)ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(MonitorEvent) + fullEvent->Size - sizeof(MonitorEventFull), DRIVER_TAG);
	if (!event) {
		KdPrint((DRIVER_PREFIX "Failed to allocate memory for MonitorEvent\n"));
		return NULL;
	}
	event->EventType = fullEvent->EventType;
	event->TimeStamp = fullEvent->TimeStamp;
	event->Size = fullEvent->Size;
	switch (fullEvent->EventType) {
	case ProcessCreated:
		event->Data.ProcessCreated = fullEvent->Data.ProcessCreated;
		SIZE_T cmdBytes = fullEvent->Data.ProcessCreated.CommandLineLength * sizeof(WCHAR);
		RtlCopyMemory(event->Data.ProcessCreated.CommandLine, fullEvent->Data.ProcessCreated.CommandLine, cmdBytes + sizeof(WCHAR));
		break;
	default:
		KdPrint((DRIVER_PREFIX "Unknown event type: %d\n", fullEvent->EventType));
		ExFreePoolWithTag(event, DRIVER_TAG);
		return NULL;
	}
	return event;
}

MonitorEvent* PopFromEventQueue()
{
	ExAcquireFastMutex(&g_Mutex);
	MonitorEvent* event = NULL;
	if (!IsListEmpty(&g_Globals.ItemsHead)) {
		PLIST_ENTRY entry = RemoveHeadList(&g_Globals.ItemsHead);
		MonitorEventFull* fullEvent = CONTAINING_RECORD(entry, MonitorEventFull, ListEntry);
		event = ConvertMonitorEventFullToMonitorEvent(fullEvent);
		ExFreePoolWithTag(fullEvent, DRIVER_TAG);
		g_Globals.ItemCount--;
	}
	ExReleaseFastMutex(&g_Mutex);
	return event;
}
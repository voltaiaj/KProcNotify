#include <ntifs.h>


#define DRIVER_TAG 'KPNO'
#define DRIVER_PREFIX "KProcNotify: "

VOID DriverUnload(PDRIVER_OBJECT);
NTSTATUS KProcNotifyCreateClose(PDEVICE_OBJECT, PIRP);
NTSTATUS KProcNotifyDeviceControl(PDEVICE_OBJECT, PIRP);

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	UNREFERENCED_PARAMETER(RegistryPath);
	// Set up the driver unload routine
	NTSTATUS status = STATUS_SUCCESS;
	do {
		UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\KProcNotify");
		PDEVICE_OBJECT deviceObject = NULL;

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

		DriverObject->DriverUnload = DriverUnload;
		DriverObject->MajorFunction[IRP_MJ_CREATE] = KProcNotifyCreateClose;
		DriverObject->MajorFunction[IRP_MJ_CLOSE] = KProcNotifyCreateClose;
		DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = KProcNotifyDeviceControl;
	} while (FALSE);
	
	// Perform any necessary initialization here
	return status;
}

VOID DriverUnload(PDRIVER_OBJECT DriverObject)
{
	UNREFERENCED_PARAMETER(DriverObject);
	// Perform any necessary cleanup here
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
	UNREFERENCED_PARAMETER(Irp);
	// Handle IOCTLs here
	return STATUS_SUCCESS;
}

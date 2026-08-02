#include <ntddk.h>

#define DEVICE_NAME L"\\Device\\SystemWideTransmission"
#define SYM_LINK L"\\DosDevices\\SystemWideTransmission"

#define IOCTL_WIDE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x512, METHOD_BUFFERED, FILE_WRITE_DATA)

#pragma data_seg("NONPAGED_DATA")

typedef __int64 (*pHalpTscQueryCounterOrdered)();

typedef struct {
    ULONG WideTransmission;
    BOOLEAN Mode;
}TransmissionMode, *PTransmissionMode;

typedef struct {
    __int64 LastTime;
    __int64 NowTime;
    __int64 ChangedTime;
}InstantTime;

InstantTime ActualTime;
TransmissionMode Transmission = {1, 1};

ULONG_PTR HalpPerformanceCounterAddress;
ULONG_PTR HalpTimerQueryCounterHandlersAddress;
ULONG_PTR* CounterFunction = NULL;
pHalpTscQueryCounterOrdered HalpTscQueryCounterOrdered = NULL;

KTIMER glTimer;
KDPC glDpc;

unsigned char HalpPerformanceCounterBin8And10[7] = {0x48, 0x8B, 0x3D};
unsigned char HalpPerformanceCounterBin11[7] = {0x48, 0x8B, 0x35};
unsigned char HalpTimerQueryCounterHandlersBin11[10] = {0x48, 0x03, 0xC0, 0x48, 0x8D, 0x0D};

unsigned char (*HalpPerformanceCounterBin)[7] = NULL;
unsigned char (*HalpTimerQueryCounterHandlersBin)[10] = NULL;

#pragma data_seg()

void Overwrite(PVOID Address, PVOID Data, ULONG Size) {
    PVOID MappedAddress = MmMapIoSpace(MmGetPhysicalAddress(Address), Size, MmNonCached);
    RtlMoveMemory(MappedAddress, Data, Size);
    MmUnmapIoSpace(MappedAddress, Size);
}

ULONG_PTR FindAddress(PVOID Address, unsigned char* Bin, SIZE_T size) {
    unsigned char* BytesAddress = (unsigned char*)Address;
    ULONG i;
    for (ULONG find = 0; ; find++) {
        for (i = 0; i < size; i++) {
            if (BytesAddress[find + i] != Bin[i]) {
                break;
            }
        }
        if (i == size) {
            return (ULONG_PTR)(BytesAddress + find);
        }
    }
}

void ChooseSystemVersion() {
    RTL_OSVERSIONINFOW osvi = {0};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    RtlGetVersion(&osvi);
    if(osvi.dwBuildNumber <= 19045) {
        HalpPerformanceCounterBin = &HalpPerformanceCounterBin8And10;
    }
    else if(osvi.dwBuildNumber < 28000) {
        HalpPerformanceCounterBin = &HalpPerformanceCounterBin11;
    }
    else {
        HalpPerformanceCounterBin = &HalpPerformanceCounterBin11;
        HalpTimerQueryCounterHandlersBin = &HalpTimerQueryCounterHandlersBin11;
    }
}

__int64 HalpHookedTscQueryCounterOrdered() {
    ActualTime.NowTime = HalpTscQueryCounterOrdered();
    ActualTime.ChangedTime = Transmission.Mode ? (ActualTime.ChangedTime + (ActualTime.NowTime - ActualTime.LastTime) * Transmission.WideTransmission) : (ActualTime.ChangedTime + (ActualTime.NowTime - ActualTime.LastTime) / Transmission.WideTransmission);
    ActualTime.LastTime = ActualTime.NowTime;
    return ActualTime.ChangedTime;
}

void BanningQpcBypass() {
    unsigned char patch;
    unsigned char original;
    RtlMoveMemory(&original, (PVOID)0x7FFE03C6, 1);
    patch = original & ~1;
    Overwrite((PVOID)0x7FFE03C6, &patch, 1);
}

void HookFunction(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2) {
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    if(HalpTimerQueryCounterHandlersBin) {
        CounterFunction = (ULONG_PTR*)(HalpTimerQueryCounterHandlersAddress + *(ULONG*)(*(ULONG_PTR*)HalpPerformanceCounterAddress + 0xBC) * 16);
    }
    else {
        CounterFunction = (ULONG_PTR*)(*(ULONG_PTR*)HalpPerformanceCounterAddress + 0x70);
    }
    if(*CounterFunction != (ULONG_PTR)&HalpHookedTscQueryCounterOrdered) {
        HalpTscQueryCounterOrdered = (pHalpTscQueryCounterOrdered)(*CounterFunction);
        ActualTime.LastTime = ActualTime.ChangedTime = HalpTscQueryCounterOrdered();
        *CounterFunction = (ULONG_PTR)&HalpHookedTscQueryCounterOrdered;
    }
}

NTSTATUS TransmissionDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    ULONG ioctlCode = stack->Parameters.DeviceIoControl.IoControlCode;
    PTransmissionMode buffer = (PTransmissionMode)(Irp->AssociatedIrp.SystemBuffer);
    if(buffer == NULL || stack->Parameters.DeviceIoControl.InputBufferLength != sizeof(TransmissionMode)) {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }
    if(ioctlCode != IOCTL_WIDE) {
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    if((buffer->WideTransmission == 0) && (!buffer->Mode)) {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }
    Transmission = *buffer;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS TransmissionDispatch(PDEVICE_OBJECT DeviceObject, PIRP Irp) {
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    PDEVICE_OBJECT DeviceObject = NULL;
    UNICODE_STRING devName = RTL_CONSTANT_STRING(DEVICE_NAME);
    UNICODE_STRING symLink = RTL_CONSTANT_STRING(SYM_LINK);
    IoCreateDevice(DriverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &DeviceObject);
    IoCreateSymbolicLink(&symLink, &devName);
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DriverObject->MajorFunction[IRP_MJ_CLOSE] = TransmissionDispatch;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = TransmissionDeviceControl;
    BanningQpcBypass();
    UNICODE_STRING QpcName = RTL_CONSTANT_STRING(L"KeQueryPerformanceCounter");
    PVOID KeQueryPerformanceCounterAddress = MmGetSystemRoutineAddress(&QpcName);
    ChooseSystemVersion();
    ULONG_PTR HalpPerformanceCounterMovCodeAddress = FindAddress(KeQueryPerformanceCounterAddress, *HalpPerformanceCounterBin, sizeof(*HalpPerformanceCounterBin)/sizeof(unsigned char) - sizeof(long));
    HalpPerformanceCounterAddress = (ULONG_PTR)(HalpPerformanceCounterMovCodeAddress + sizeof(*HalpPerformanceCounterBin)/sizeof(unsigned char) + *(long*)(HalpPerformanceCounterMovCodeAddress + (sizeof(*HalpPerformanceCounterBin)/sizeof(unsigned char) - sizeof(long))));
    if(HalpTimerQueryCounterHandlersBin) {
        ULONG_PTR HalpTimerQueryCounterHandlersLeaAddress = FindAddress(KeQueryPerformanceCounterAddress, *HalpTimerQueryCounterHandlersBin, sizeof(*HalpTimerQueryCounterHandlersBin)/sizeof(unsigned char) - sizeof(long));
        HalpTimerQueryCounterHandlersAddress = (ULONG_PTR)(HalpTimerQueryCounterHandlersLeaAddress + sizeof(*HalpTimerQueryCounterHandlersBin)/sizeof(unsigned char) + *(long*)(HalpTimerQueryCounterHandlersLeaAddress + (sizeof(*HalpTimerQueryCounterHandlersBin)/sizeof(unsigned char) - sizeof(long))));
    }
    KeInitializeDpc(&glDpc, HookFunction, NULL);
    KeInitializeTimerEx(&glTimer, SynchronizationTimer);
    LARGE_INTEGER dueTime = {.QuadPart = 0};
    KeSetTimerEx(&glTimer, dueTime, 1000, &glDpc);
    return STATUS_SUCCESS;
}
#include <ntddk.h>
#include <intrin.h>
#include <ntdef.h>
#include <ntstatus.h>
#include <wdm.h>

typedef unsigned __int64  uint64_t;
typedef unsigned int      uint32_t;

// Revision Guide for AMD Family 19h Models 00h-0Fh Processors recommends using CTL2 as opposed to CTL0
// CTL 0 is 0xC0010200ULL so CTL 2 is 0xC0010204ULL
constexpr uint64_t AMD_PERF_CTL2 = 0xC0010204ULL;   // event-select register; CTL[n] = base + 2n
constexpr uint64_t AMD_PERF_CTR2 = 0xC0010205ULL;   // counter register;      CTR[n] = base + 1 + 2n

// AMD PERF_CTL bit fields
constexpr uint64_t AMD_USR = (1ULL << 16);   // Enable Ring 3 tracing (USR mode)
constexpr uint64_t AMD_OS  = (1ULL << 17);   // Enable Ring 0 tracing (Kernel mode) Counts hardware interrupts
constexpr uint64_t AMD_EN  = (1ULL << 22);   // per-counter enable -- this is the enable, not a global MSR

constexpr uint64_t AMD_UNDOC_BIT_43 = (1ULL << 43); //Revision Guide for AMD Family 19h Models 00h-0Fh Processors recommends setting these bits
constexpr uint64_t AMD_UNDOC_BIT_20 = (1ULL << 20);

constexpr uint64_t CR4_PCE_BIT = (1ULL << 8);

constexpr uint64_t AMD_LS_CFG_MSR = 0xC0011020ULL;
constexpr uint64_t AMD_LS_CFG_SPEC_LOCK_MAP_DIS = (1ULL << 54);

#define IOCTL_SET_AMD_PROFILING_EVENT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
UNICODE_STRING DEVICE_NAME = RTL_CONSTANT_STRING(L"\\Device\\AMDDevice");
UNICODE_STRING DEVICE_SYMBOLIC_NAME = RTL_CONSTANT_STRING(L"\\DosDevices\\AMDDeviceLink");

// event code is 12 bits, split: low 8 -> bits[7:0], high 4 -> bits[35:32]
static uint64_t AmdSel(uint32_t eventCode, uint32_t umask) {
    uint64_t sel = 0;
    sel |= static_cast<uint64_t>((eventCode & 0xFF));              // EventSelect[7:0]
    sel |= static_cast<uint64_t>((umask & 0xFF)) << 8;             // UnitMask[7:0]
    sel |= static_cast<uint64_t>(((eventCode >> 8) & 0xF)) << 32;  // EventSelect[11:8]
    sel |= AMD_USR | AMD_EN;
    sel |= AMD_UNDOC_BIT_43; //enable bit 43
    sel &= ~AMD_UNDOC_BIT_20; //disable bit 20
    return sel;
}

static ULONG_PTR ProgramSelOnThisCpu([[maybe_unused]] ULONG_PTR Ctx) {
    // Read current CR4 value
    ULONG_PTR cr4 = __readcr4();

    // Set Bit 8 (PCE) to 1
    cr4 |= CR4_PCE_BIT;
    __writecr4(cr4);

    __writemsr(AMD_PERF_CTR2, 0);

    // event 0xC2 = retired branch instructions
    __writemsr(AMD_PERF_CTL2, AmdSel(0xC2, 0x00));
    return 0;
}

static ULONG_PTR DisableSpecLockMappingOnThisCpu([[maybe_unused]] ULONG_PTR Ctx) {
    uint64_t lsCfg = __readmsr(AMD_LS_CFG_MSR);
    lsCfg |= AMD_LS_CFG_SPEC_LOCK_MAP_DIS;
    DbgPrint("LS CFG MSR value before disabling SpecLockMapping %I64x\n ", lsCfg);
    __writemsr(AMD_LS_CFG_MSR, lsCfg);
    return 0;
}

static ULONG_PTR EnableSpecLockMappingOnThisCpu([[maybe_unused]] ULONG_PTR Ctx) {
    uint64_t lsCfg = __readmsr(AMD_LS_CFG_MSR);
    lsCfg &= ~AMD_LS_CFG_SPEC_LOCK_MAP_DIS;
    DbgPrint("LS CFG MSR value before re-enabling SpecLockMapping %I64x\n ", lsCfg);
    __writemsr(AMD_LS_CFG_MSR, lsCfg);
    return 0;
}

static ULONG_PTR UnloadSelOnThisCpu([[maybe_unused]] ULONG_PTR Ctx) {
    ULONG_PTR cr4 = __readcr4();
    cr4 &= ~CR4_PCE_BIT;
    __writecr4(cr4);
    __writemsr(AMD_PERF_CTR2, 0);
    __writemsr(AMD_PERF_CTL2, 0);
    return 0;
}

static NTSTATUS ConfigureCounters() {
    // Register the PMC index with the OS per-thread virtualization. Must be
    // called while profiling is disabled
    HARDWARE_COUNTER hc; 
    RtlZeroMemory(&hc, sizeof(hc));
    hc.Type = PMCCounter;
    hc.Index = 2;       // the pmc index.  2 for CTL2
    NTSTATUS status = KeSetHardwareCounterConfiguration(&hc, 1);

    if (!NT_SUCCESS(status)) return status;

    // Run the msr writes on every processor
    KeIpiGenericCall(ProgramSelOnThisCpu, 0);

    return STATUS_SUCCESS;
}

NTSTATUS HandleCustomIOCTL([[maybe_unused]] PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION stackLocation = NULL;
	stackLocation = IoGetCurrentIrpStackLocation(Irp);

    NTSTATUS status = STATUS_SUCCESS;
	if (stackLocation->Parameters.DeviceIoControl.IoControlCode == IOCTL_SET_AMD_PROFILING_EVENT) {
        status = ConfigureCounters();
	}

	Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

NTSTATUS HandleOpenAndClose([[maybe_unused]] PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	PIO_STACK_LOCATION stackLocation = NULL;
	stackLocation = IoGetCurrentIrpStackLocation(Irp);

	switch (stackLocation->MajorFunction) {
        case IRP_MJ_CREATE:
            DbgPrint("Handle to symbolink link %wZ opened\n", DEVICE_SYMBOLIC_NAME);
            break;
        case IRP_MJ_CLOSE:
            DbgPrint("Handle to symbolink link %wZ closed\n", DEVICE_SYMBOLIC_NAME);
            break;
        default:
            break;
	}
	
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

void DriverUnload([[maybe_unused]] PDRIVER_OBJECT DriverObject) {
    // Clean up settings across all cores on unload
    KeSetHardwareCounterConfiguration(NULL,0);

    // Clean up the pmu on every processor
    KeIpiGenericCall(UnloadSelOnThisCpu, 0);

    // Re-enable speculative lock mapping on every processor
    KeIpiGenericCall(EnableSpecLockMappingOnThisCpu, 0);

    // Clean up device links
    IoDeleteDevice(DriverObject->DeviceObject);
	IoDeleteSymbolicLink(&DEVICE_SYMBOLIC_NAME);
}

extern "C" NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, [[maybe_unused]] PUNICODE_STRING RegistryPath) {
    NTSTATUS status = 0;
    
    DriverObject->DriverUnload = DriverUnload;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HandleCustomIOCTL;
    
	// routines that will execute once a handle to our device's symbolik link is opened/closed
	DriverObject->MajorFunction[IRP_MJ_CREATE] = HandleOpenAndClose;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = HandleOpenAndClose;

	status = IoCreateDevice(DriverObject, 0, &DEVICE_NAME, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DriverObject->DeviceObject);
	if (!NT_SUCCESS(status)) {
		DbgPrint("Could not create device %wZ\n", DEVICE_NAME);
	} else {
		DbgPrint("Device %wZ created\n", DEVICE_NAME);
	}

	status = IoCreateSymbolicLink(&DEVICE_SYMBOLIC_NAME, &DEVICE_NAME);
	if (NT_SUCCESS(status)) {
		DbgPrint("Symbolic link %wZ created\n", DEVICE_SYMBOLIC_NAME);
	} else {
		DbgPrint("Error creating symbolic link %wZ\n", DEVICE_SYMBOLIC_NAME);
	}

    // Disable speculative lock mapping on every processor while loaded
    KeIpiGenericCall(DisableSpecLockMappingOnThisCpu, 0);

    return STATUS_SUCCESS;
}
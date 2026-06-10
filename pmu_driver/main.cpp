#include <ntddk.h>
#include <intrin.h>
#include <ntstatus.h>
#include <wdm.h>

typedef unsigned __int64  uint64_t;
typedef unsigned int      uint32_t;

constexpr uint64_t AMD_PERF_CTL0 = 0xC0010200ULL;   // event-select register; CTL[n] = base + 2n
constexpr uint64_t AMD_PERF_CTR0 = 0xC0010201ULL;   // counter register;      CTR[n] = base + 1 + 2n

// AMD PERF_CTL bit fields
constexpr uint64_t AMD_USR = (1ULL << 16);   // Enable Ring 3 tracing (USR mode)
constexpr uint64_t AMD_OS  = (1ULL << 17);   // Enable Ring 0 tracing (Kernel mode) Counts hardware interrupts
constexpr uint64_t AMD_EN  = (1ULL << 22);   // per-counter enable -- this is the enable, not a global MSR

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
    return sel;
}

static ULONG_PTR ProgramSelOnThisCpu([[maybe_unused]] ULONG_PTR Ctx) {
    __writemsr(AMD_PERF_CTR0, 0);

    // event 0xC2 = retired branch instructions
    __writemsr(AMD_PERF_CTL0, AmdSel(0xC2, 0x00));
    return 0;
}

static NTSTATUS ConfigureCounters() {
    // claim the counter resource so we don't collide with other profiling drivers
    PHYSICAL_COUNTER_RESOURCE_LIST rl;
    RtlZeroMemory(&rl, sizeof(rl));
    rl.Count = 1;
    rl.Descriptors[0].Type = ResourceTypeSingle;      // single counter
    rl.Descriptors[0].u.CounterIndex = 0;             // the PMC index

    GROUP_AFFINITY ga; 
    RtlZeroMemory(&ga, sizeof(ga));
    
    // specify all the logical processors in group (64 for x64 and 32 for x86)
    // This needs to be rerun and incrementing the group count for more than 64 logical processors
    ga.Mask = ~0ull;   
    ga.Group = 0;

    // Unfortunately this doesn't work for HYPER-V VMs.  Find a workaround later?
    HANDLE counter_handle = nullptr;
    NTSTATUS st = HalAllocateHardwareCounters(&ga, 1, &rl, &counter_handle);
    
    if (!NT_SUCCESS(st)) return st;

    // Run the msr writes on every processor
    KeIpiGenericCall(ProgramSelOnThisCpu, 0);

    // Register the PMC index with the OS per-thread virtualization. Must be
    // called while profiling is disabled
    HARDWARE_COUNTER hc; RtlZeroMemory(&hc, sizeof(hc));
    hc.Type = PMCCounter;
    hc.Index = 0;       // the pmc index
    return KeSetHardwareCounterConfiguration(&hc, 1);
}

NTSTATUS HandleCustomIOCTL([[maybe_unused]] PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    ConfigureCounters();
	Irp->IoStatus.Status = STATUS_SUCCESS;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return STATUS_SUCCESS;
}

NTSTATUS HandleOpenAndClose([[maybe_unused]] PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
	PIO_STACK_LOCATION stackLocation = NULL;
	stackLocation = IoGetCurrentIrpStackLocation(Irp);

	switch (stackLocation->MajorFunction) {
        case IRP_MJ_CREATE:
            DbgPrint("Handle to symbolink link %wZ opened", DEVICE_SYMBOLIC_NAME);
            break;
        case IRP_MJ_CLOSE:
            DbgPrint("Handle to symbolink link %wZ closed", DEVICE_SYMBOLIC_NAME);
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
    HalFreeHardwareCounters(0);

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
		DbgPrint("Could not create device %wZ", DEVICE_NAME);
	} else {
		DbgPrint("Device %wZ created", DEVICE_NAME);
	}

	status = IoCreateSymbolicLink(&DEVICE_SYMBOLIC_NAME, &DEVICE_NAME);
	if (NT_SUCCESS(status)) {
		DbgPrint("Symbolic link %wZ created", DEVICE_SYMBOLIC_NAME);
	} else {
		DbgPrint("Error creating symbolic link %wZ", DEVICE_SYMBOLIC_NAME);
	}    
    return STATUS_SUCCESS;
}
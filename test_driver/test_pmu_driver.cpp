#include <Windows.h>
#include <print>
#include <string>
#include <winnt.h>
#include "test_pmu_driver.h"

#define IOCTL_SET_AMD_PROFILING_EVENT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

// Keep the optimizer from collapsing the calibration loop into a closed form.
#pragma optimize("", off)
static volatile uint64_t g_sink;
static uint64_t measure(HANDLE h, uint64_t iters) {
    uint64_t a, b;
    PERFORMANCE_DATA pd_1, pd_2; 
    ZeroMemory(&pd_1, sizeof(pd_1));
    ZeroMemory(&pd_2, sizeof(pd_2));

    pd_1.Size    = sizeof(pd_1);
    pd_1.Version = PERFORMANCE_DATA_VERSION;

    pd_2.Size    = sizeof(pd_2);
    pd_2.Version = PERFORMANCE_DATA_VERSION; 
    
    // Read counter to start
    ReadThreadProfilingData(h, READ_THREAD_PROFILING_FLAG_HARDWARE_COUNTERS, &pd_1);
    a = pd_1.HwCounters[0].Value;

    // branch runner inline to prevent some switches
    for (uint64_t i = 0; i < iters; i++) g_sink += i;  // back-edge = our branch

    // Read counter to finish
    ReadThreadProfilingData(h, READ_THREAD_PROFILING_FLAG_HARDWARE_COUNTERS, &pd_1);
    b = pd_1.HwCounters[0].Value;
    return b - a;
}

#pragma optimize("", on)

int main() {
    HANDLE device = INVALID_HANDLE_VALUE;
    BOOL status = FALSE;                 
    DWORD bytesReturned = 0;
    CHAR inBuffer[128] = {0};
    CHAR outBuffer[128] = {0};
    
    LoadDriver(L"PMUDriver", L"C:\\drivers\\pmu_driver.sys");

    device = CreateFileW(L"\\\\.\\AMDDeviceLink", GENERIC_WRITE | GENERIC_READ | GENERIC_EXECUTE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM, 0);
    
    if (device == INVALID_HANDLE_VALUE)
    {
        std::print("> Could not open device with error {}\n", GetLastError());
        CloseHandle(device);
        return 1;
    }

    SetThreadAffinityMask(GetCurrentThread(), 1ull << 0);   // pin: remove migration as a variable for now

    status = DeviceIoControl(device, IOCTL_SET_AMD_PROFILING_EVENT, inBuffer, sizeof(inBuffer), outBuffer, sizeof(outBuffer), &bytesReturned, (LPOVERLAPPED)NULL);
    if (status == 0) {
        std::print("DeviceIoControl returned status {}", GetLastError());
    }

    HANDLE prof = nullptr;
    if (EnableThreadProfiling(GetCurrentThread(), 0, 0x1 /*index 0*/, &prof) != ERROR_SUCCESS) {
        printf("EnableThreadProfiling failed: %lu\n", GetLastError());
        UnloadDriver(L"PMUDriver");
        CloseHandle(device);
        return 1;
    }

    const uint64_t N = 10'000'000;
    uint64_t c1 = measure(prof, N), c2 = measure(prof, 2*N);
    printf("N=%llu br=%llu | 2N br=%llu | ratio=%.4f (want ~2.0) | per-iter=%.4f\n",
           (unsigned long long)N, (unsigned long long)c1, (unsigned long long)c2,
           c1 ? (double)c2/c1 : 0.0, c1 ? (double)(c2-c1)/N : 0.0);

    const int K = 1000; uint64_t mn=~0ull, mx=0, first=0;
    for (int k=0;k<K;k++){ uint64_t c=measure(prof,N); if(!k)first=c; if(c<mn)mn=c; if(c>mx)mx=c; }
    printf("determinism: first=%llu min=%llu max=%llu spread=%llu\n",
           (unsigned long long)first,(unsigned long long)mn,(unsigned long long)mx,
           (unsigned long long)(mx-mn));

    DisableThreadProfiling(prof);    

    UnloadDriver(L"PMUDriver");

    CloseHandle(device);
    return 0;
}


void LoadDriver(const std::wstring& serviceName, const std::wstring& driverPath) {
    SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) throw std::runtime_error("OpenSCManager failed: " + GetLastErrorStr());

    // Try to create the service; if it already exists, open it
    SC_HANDLE svc = CreateServiceW(
        scm,
        serviceName.c_str(),       // service name
        serviceName.c_str(),       // display name
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,     // driver type
        SERVICE_DEMAND_START,      // manual start
        SERVICE_ERROR_NORMAL,
        driverPath.c_str(),        // full path to .sys file
        nullptr, nullptr, nullptr, nullptr, nullptr
    );

    if (!svc) {
        if (GetLastError() == ERROR_SERVICE_EXISTS)
            svc = OpenServiceW(scm, serviceName.c_str(), SERVICE_ALL_ACCESS);
        if (!svc) {
            CloseServiceHandle(scm);
            throw std::runtime_error("CreateService/OpenService failed: " + GetLastErrorStr());
        }
    }

    // Start the driver
    if (!StartService(svc, 0, nullptr)) {
        DWORD err = GetLastError();
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        if (err != ERROR_SERVICE_ALREADY_RUNNING)
            throw std::runtime_error("StartService failed: " + GetLastErrorStr(err));
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
}

void UnloadDriver(const std::wstring& serviceName) {
    SC_HANDLE scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (!scm) throw std::runtime_error("OpenSCManager failed: " + GetLastErrorStr());

    SC_HANDLE svc = OpenServiceW(scm, serviceName.c_str(), SERVICE_ALL_ACCESS);
    if (!svc) {
        CloseServiceHandle(scm);
        throw std::runtime_error("OpenService failed: " + GetLastErrorStr());
    }

    // Stop the driver
    SERVICE_STATUS status{};
    if (!ControlService(svc, SERVICE_CONTROL_STOP, &status)) {
        DWORD err = GetLastError();
        if (err != ERROR_SERVICE_NOT_ACTIVE) {
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            throw std::runtime_error("ControlService(STOP) failed: " + GetLastErrorStr(err));
        }
    }

    // Delete the service entry
    if (!DeleteService(svc)) {
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        throw std::runtime_error("DeleteService failed: " + GetLastErrorStr());
    }

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
}

std::string GetLastErrorStr(DWORD err) {
    if (!err) err = GetLastError();
    char buf[256]{};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, 0, buf, sizeof(buf), nullptr);
    return std::string(buf) + " (" + std::to_string(err) + ")";
}
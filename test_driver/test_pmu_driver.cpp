#include <Windows.h>
#include <print>
#include <string>
#include "test_pmu_driver.h"

#define IOCTL_SPOTLESS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x2049, METHOD_BUFFERED, FILE_ANY_ACCESS)

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
        return 1;
    }

    status = DeviceIoControl(device, IOCTL_SPOTLESS, inBuffer, sizeof(inBuffer), outBuffer, sizeof(outBuffer), &bytesReturned, (LPOVERLAPPED)NULL);
    std::print("DeviceIoControl returned status {}", status);

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
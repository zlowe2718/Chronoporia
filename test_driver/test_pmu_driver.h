#include <Windows.h>
#include <string>

void LoadDriver(const std::wstring& serviceName, const std::wstring& driverPath);
void UnloadDriver(const std::wstring& serviceName);
std::string GetLastErrorStr(DWORD err = 0);
#include "compiler.h"
#include <Windows.h>
#include <sstream>


namespace breakpoint_plugin {

    bool compile_condition(const std::string &source, const std::wstring &output_dll_path) {
        wchar_t temp_dir[MAX_PATH];
        GetTempPathW(MAX_PATH, temp_dir);
        wchar_t source_path[MAX_PATH];
        GetTempFileNameW(temp_dir, L"cnd", 0, source_path);

        std::wstring cpp_path = std::wstring(source_path) + L".cpp";
        DeleteFileW(source_path);

        HANDLE file_handle = CreateFileW(cpp_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_handle == INVALID_HANDLE_VALUE) {
            printf("Failed to write condition source file");
            return false;
        }
        DWORD written = 0;
        WriteFile(file_handle, source.data(), static_cast<DWORD>(source.size()), &written, nullptr);
        CloseHandle(file_handle);

        // TODO: find a way to make this compiler agnostic via config files or something
        // need to launch from developer command prompt for easy path finding of windows libraries
        // TODO: add a search mechanism to find this
        std::wstring command = L"cmd.exe /C \"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat\" && cl.exe /nologo /LD /EHsc /std:c++20 /Fe:{output} {source}";
        auto replace_all = [](std::wstring &str, const std::wstring &from, const std::wstring &to) {
            size_t pos = 0;
            while ((pos = str.find(from, pos)) != std::wstring::npos) {
                str.replace(pos, from.size(), to);
                pos += to.size();
            }
        };
        replace_all(command, L"{source}", cpp_path);
        replace_all(command, L"{output}", output_dll_path);

        STARTUPINFOW startup_info{};
        startup_info.cb = sizeof(startup_info);
        PROCESS_INFORMATION process_info{};

        bool success = CreateProcessW(
            nullptr, command.data(), nullptr, nullptr, FALSE,
            0, nullptr, nullptr, &startup_info, &process_info
        );

        if (!success) {
            printf("Failed to launch compiler command for condition expression");
            DeleteFileW(cpp_path.c_str());
            return false;
        }

        WaitForSingleObject(process_info.hProcess, INFINITE);
        DWORD exit_code = 1;
        GetExitCodeProcess(process_info.hProcess, &exit_code);
        CloseHandle(process_info.hProcess);
        CloseHandle(process_info.hThread);
        DeleteFileW(cpp_path.c_str());

        if (exit_code != 0) {
            printf("Compiler failed to build condition expression (exit code %lu)", exit_code);
            return false;
        }

        return true;
    }

    std::string build_conditional_source_code(const std::string &expression, const std::vector<VariableInfo> &vars) {
        std::ostringstream src = {};
        src << "#include <cstring>\n#include <cstdint>\n";
        src << "extern \"C\" __declspec(dllexport) bool __cdecl evaluate_condition(const int64_t* args) {\n";

        for (size_t i = 0; i < vars.size(); ++i) {
            const VariableInfo &var = vars[i];
            switch (var.kind) {
                case VariableKind::Bool:
                    src << "    bool " << var.name << " = args[" << i << "] != 0;\n";
                    break;
                case VariableKind::Char:
                case VariableKind::WChar: // This might need to be validated
                case VariableKind::UInt8:
                case VariableKind::UInt16:
                case VariableKind::UInt32:
                case VariableKind::UInt64:
                case VariableKind::Int8:
                case VariableKind::Int16:
                case VariableKind::Int32:
                case VariableKind::Int64:
                    src << "    " << var.cpp_type << " " << var.name << " = (" << var.cpp_type << ")args[" << i << "];\n";
                    break;
                case VariableKind::Float: {
                    src << "    float " << var.name << "; { int32_t _bits = (int32_t)args[" << i << "]; memcpy(&" << var.name << ", &_bits, sizeof(" << var.name << ")); }\n";
                    break;
                }
                case VariableKind::Double: {
                    src << "    double " << var.name << "; memcpy(&" << var.name << ", &args[" << i << "], sizeof(" << var.name << "));\n";
                    break;
                }
                case VariableKind::Pointer:
                    src << "    void* " << var.name << " = reinterpret_cast<void*>(args[" << i << "]);\n";
                    break;
            }
        }

        src << "    return (" << expression << ");\n}\n";
        return src.str();
    }

}
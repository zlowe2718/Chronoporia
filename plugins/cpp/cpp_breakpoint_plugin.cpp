#include "cpp_breakpoint_plugin.h"
#include <TlHelp32.h>
#include <atlcomcli.h>
#include <combaseapi.h>
#include <cstdint>
#include <cvconst.h>
#include <dia2.h>
#include <atlbase.h>
#include <unordered_set>
#include <winerror.h>
#include <winnt.h>

namespace {
    CComPtr<IDiaSession> dia_session;
    CComPtr<IDiaSymbol> dia_global_scope;
    CComPtr<IDiaEnumSymbols> dia_compilands;
    std::unordered_set<DWORD> loaded_source_file_ids;
    std::vector<breakpoint_plugin::SourceLocation> source_locations;
}

namespace breakpoint_plugin {

    // TODO: update main to take in a path or folder to search
    // TODO: What should the behaviour be if we can't load pdb information? Fall back to smaller snapshots and replaying manually?
    void InitializePlugin(DWORD process_id, const std::wstring &dll_name, const std::wstring &pdb_path) {
        // Intialize COM library
        CoInitializeEx(nullptr, 0);

        bool success = LoadPDB(pdb_path);
        if (!success) {
            printf("Failed to load debug information for the program");
            return;
        }

        uintptr_t image_base = GetImageBase(process_id, dll_name);
        LoadSourceLocations(image_base);
    }

    void UninitializePlugin() {
        dia_session.Release();
        dia_global_scope.Release();
        CoUninitialize();
    }

    void InitializeBreakpointsAtAllLines() {
        for (const auto &source_location : source_locations ) {
            
        }
    }

    void SetBreakpoint(SourceLocation) {

    }
    void ClearBreakpoint(SourceLocation) {

    }
    void ClearAllBreakpoints() {

    }

    bool ReachedBreakpoint(const uintptr_t code_address) {
        return true;
    }

    bool LoadPDB(const std::wstring &path) {
        CComPtr<IDiaDataSource> dia_data_source;
        HRESULT h_result = CoCreateInstance(
            CLSID_DiaSource, nullptr, CLSCTX_INPROC_SERVER,
            __uuidof(IDiaDataSource), reinterpret_cast<LPVOID *>(&dia_data_source)
        );

        if (FAILED(h_result))
        {
            printf("Could not CoCreate CLSID_DiaSource. Register msdia80.dll." );
            return false;
        }

        h_result = dia_data_source->loadDataFromPdb(path.c_str());
        if (FAILED(h_result)) {
            // try loading as an exe
            h_result = dia_data_source->loadDataForExe( path.c_str(), nullptr, nullptr );
            if (FAILED(h_result)) {
                printf("Could not load from pdb or exe");
                return false;
            }
        }

        h_result = dia_data_source->openSession(&dia_session);
        if (FAILED(h_result)) {
            printf("Could not open pdb session");
            return false;
        }

        h_result = dia_session->get_globalScope(&dia_global_scope);
        if (FAILED(h_result)) {
            printf("Could not get the dia global scope");
            return false;
        }

        // get all compilands (translation units / .obj files)
        h_result = dia_global_scope->findChildren(SymTagCompiland, nullptr, nsNone, &dia_compilands);
        if (FAILED(h_result)) {
            printf("Could not get all compilands for pdb file");
            return false;
        }

        return true;
    }

    void LoadSourceLocations(uintptr_t image_base) {
        CComPtr<IDiaSymbol>dia_compiland;
        ULONG fetched = 0;
        CComPtr<IDiaEnumSourceFiles> source_files;
        CComBSTR file_name;
        DWORD source_file_id;
        
        while (SUCCEEDED(dia_compilands->Next(1, &dia_compiland, &fetched)) && fetched == 1) {

            // Need the source file to iterate over the lines
            dia_session->findFile(dia_compiland, nullptr, nsNone, &source_files);

            CComPtr<IDiaSourceFile> source_file;
            while(SUCCEEDED(source_files->Next(1, &source_file, &fetched)) && fetched == 1) {
                source_file->get_uniqueId(&source_file_id);

                // If we've already seen this source file then skip over it
                if (loaded_source_file_ids.contains(source_file_id)) {
                    source_file.Release();
                    continue;
                };
                loaded_source_file_ids.insert(source_file_id);

                source_file->get_fileName(&file_name);
                
                // Get the line number table for this compiland
                CComPtr<IDiaEnumLineNumbers> line_numbers;
                dia_session->findLines(dia_compiland, source_file, &line_numbers);

                CComPtr<IDiaLineNumber> line;
                uint64_t line_number = 0;
                uint64_t line_length = 0;
                DWORD image_address_offset = 0;
                SourceLocation source_location {};
                while (SUCCEEDED(line_numbers->Next(1, &line, &fetched)) && fetched == 1) {
                    line->get_lineNumber(reinterpret_cast<unsigned long *>(&line_number));
                    line->get_relativeVirtualAddress(&image_address_offset);
                    line->get_length(reinterpret_cast<unsigned long *>(&line_length));

                    line.Release();

                    source_location = SourceLocation {
                        file_name.m_str,
                        line_number,
                        line_length,
                        image_base + image_address_offset,
                    };
                    source_locations.push_back(source_location);
                }
                line_numbers.Release();
                source_file.Release();
            }
            source_files.Release();
            dia_compiland.Release();
        }
        dia_compilands.Release();
    }

    uintptr_t GetImageBase(DWORD process_id, const std::wstring &dll_name) {
        uintptr_t image_base = 0;
        
        HANDLE snap_handle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, process_id);
        
        if (snap_handle != INVALID_HANDLE_VALUE) {
            MODULEENTRY32 module_entry;
            module_entry.dwSize = sizeof(module_entry);
            
            if (Module32First(snap_handle, &module_entry)) {
                do {
                    if (std::wstring_view(module_entry.szModule).contains(dll_name.c_str())) {
                        image_base = reinterpret_cast<uintptr_t>(module_entry.modBaseAddr);
                        break;
                    }
                } while (Module32Next(snap_handle, &module_entry));
            }
        }
        
        CloseHandle(snap_handle);
        return image_base;
    }
}
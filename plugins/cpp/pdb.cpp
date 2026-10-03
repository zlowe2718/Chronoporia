#define NOMINMAX
#include "pdb.h"
#include "base_breakpoint_plugin.h"
#include "cvconst.h"
#include "dia2.h"
#include "utils.h"
#include <algorithm>
#include <combaseapi.h>
#include <atlcomcli.h>
#include <cstdint>
#include <optional>
#include <print>
#include <variant>
#include <string_view>
#include <ranges>


namespace {
    CComPtr<IDiaSession> dia_session;
    CComPtr<IDiaSymbol> dia_global_scope;
    CComPtr<IDiaEnumSymbols> dia_compilands;
    std::vector<breakpoint_plugin::SourceLocation> source_locations;
    std::variant<int, double, std::string> v;

    void print_variable(breakpoint_plugin::VariableKind kind, std::string type, std::string name, uint64_t raw_value) {
        switch (kind) {
            case breakpoint_plugin::VariableKind::Pointer: {
                std::println("    {} {} = {:p}", type, name, reinterpret_cast<void *>(raw_value));
                break;
            }
            case breakpoint_plugin::VariableKind::Bool: {
                std::println("    {} {} = {}", type, name, static_cast<bool>(raw_value));
                break;
            }
            case breakpoint_plugin::VariableKind::Char: {
                std::println("    {} {} = {}", type, name, static_cast<char>(raw_value));
                break;
            }
            case breakpoint_plugin::VariableKind::WChar: {
                std::wstring wchar = {static_cast<wchar_t>(raw_value)};
                printf("    %s %s = %ls\n", type.c_str(), name.c_str(), wchar.c_str());
                break;
            }
            case breakpoint_plugin::VariableKind::Float: {
                std::println("    {} {} = {}", type, name, static_cast<float>(raw_value));
                break;                
            }
            case breakpoint_plugin::VariableKind::Double: {
                std::println("    {} {} = {}", type, name, static_cast<double>(raw_value));
                break;                
            }
            case breakpoint_plugin::VariableKind::Int8: {
                std::println("    {} {} = {}", type, name, static_cast<int8_t>(raw_value));
                break;                
            }
            case breakpoint_plugin::VariableKind::Int16: {
                std::println("    {} {} = {}", type, name, static_cast<int16_t>(raw_value));
                break;                
            }
            case breakpoint_plugin::VariableKind::Int32: {
                std::println("    {} {} = {}", type, name, static_cast<int32_t>(raw_value));
                break;                
            }
            case breakpoint_plugin::VariableKind::Int64: {
                std::println("    {} {} = {}", type, name, static_cast<int64_t>(raw_value));
                break;                
            }
            case breakpoint_plugin::VariableKind::UInt8: {
                std::println("    {} {} = {}", type, name, static_cast<uint8_t>(raw_value));
                break;                
            }
            case breakpoint_plugin::VariableKind::UInt16: {
                std::println("    {} {} = {}", type, name, static_cast<uint16_t>(raw_value));
                break;                
            }
            case breakpoint_plugin::VariableKind::UInt32: {
                std::println("    {} {} = {}", type, name, static_cast<uint32_t>(raw_value));
                break;                
            }
            case breakpoint_plugin::VariableKind::UInt64: {
                std::println("    {} {} = {}", type, name, static_cast<uint64_t>(raw_value));
                break;                
            }
        }
    }

    std::wstring get_type_name(IDiaSymbol* type_symbol) {
        if (!type_symbol) return L"unknown";

        DWORD symTag = 0;
        type_symbol->get_symTag(&symTag);

        if (symTag == SymTagUDT || symTag == SymTagEnum) {
            CComBSTR name {};
            if (type_symbol->get_name(&name) == S_OK && name) {
                std::wstring res(name);
                return res;
            }
        }

        if (symTag == SymTagBaseType) {
            DWORD base_type = 0;
            ULONGLONG length = 0;
            type_symbol->get_baseType(&base_type);
            type_symbol->get_length(&length);

            switch (base_type) {
                case btInt:    return L"Int" + std::to_wstring(length) + L"B";
                case btUInt:   return L"UInt" + std::to_wstring(length) + L"B";
                case btFloat:  return L"Float" + std::to_wstring(length) + L"B";
                case btChar:   return L"Char";
                case btWChar:  return L"WChar";
                case btBool:   return L"Bool";
                case btVoid:   return L"Void";
                default:       return L"BaseType_" + std::to_wstring(base_type);
            }
        }

        if (symTag == SymTagPointerType) {
            CComPtr<IDiaSymbol> underlying_type {};
            std::wstring pointerStr = L"Ptr64 ";
            
            if (type_symbol->get_type(&underlying_type) == S_OK) {
                std::wstring innerType = get_type_name(underlying_type);
                return pointerStr + innerType;
            }
        }

        if (symTag == SymTagArrayType) {
            CComPtr<IDiaSymbol> underlying_type {};
            ULONGLONG length = 0;
            type_symbol->get_length(&length);

            if (type_symbol->get_type(&underlying_type) == S_OK) {
                ULONGLONG elementSize = 0;
                underlying_type->get_length(&elementSize);
                std::wstring innerType = get_type_name(underlying_type);

                ULONGLONG elements = elementSize ? (length / elementSize) : 0;
                return innerType + L"[" + std::to_wstring(elements) + L"]";
            }
        }

        return L"complex_type";
    }
}

namespace breakpoint_plugin {

    bool load_pdb(const std::wstring &path) {
        // Intialize COM library
        CoInitializeEx(nullptr, 0);

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

    void unload_pdb() {
        dia_session.Release();
        dia_global_scope.Release();
        dia_compilands.Release();
        CoUninitialize();
    }


    SourceLocation load_breakpoint_from_pdb(const OriginalAddress original_address, const uintptr_t image_base) {
        auto itr = std::find_if(source_locations.begin(), source_locations.end(), 
            [original_address](auto& source_location) {
                return source_location.code_address == original_address.get();
            }
        );
        if (itr != source_locations.end()) return *itr;

        SourceLocation source = {};

        CComPtr<IDiaEnumLineNumbers> pLines;
        RVA rva = static_cast<RVA>(original_address.get() - image_base);
        // Find exact line
        if (SUCCEEDED(dia_session->findLinesByRVA(rva, 1, &pLines))) {
            CComPtr<IDiaLineNumber> pLine;
            ULONG celt = 0;
            
            // Iterate through the lines found at this RVA
            while (SUCCEEDED(pLines->Next(1, &pLine, &celt)) && celt == 1) {
                DWORD lineNum = 0;
                pLine->get_lineNumber(&lineNum);
                
                CComPtr<IDiaSourceFile> pSourceFile;
                if (SUCCEEDED(pLine->get_sourceFile(&pSourceFile))) {
                    BSTR bstrFileName;
                    if (SUCCEEDED(pSourceFile->get_fileName(&bstrFileName))) {
                        uint64_t file_name_size = std::wcslen(bstrFileName);
                        source.line_number = lineNum;
                        wcsncpy_s(source.file_name, file_name_size + 1, bstrFileName, file_name_size); 
                        SysFreeString(bstrFileName);

                        // if we got the correct line then add in if its the statement (function) beginning
                        BOOL statement_beginning;
                        pLine->get_statement(&statement_beginning);
                        source.statement_beginning = statement_beginning == TRUE ? true : false;
                    }
                }
                pLine.Release();
            }
        }
        source.code_address = original_address.get();
        source_locations.push_back(source);
        return source;
    }

    // TODO: Need to eventually support containers, custom structs, and pointers like char *
    std::optional<VariableType> resolve_variable_type(IDiaSymbol *type_symbol) {
        if (!type_symbol) return std::nullopt;

        VariableType variable_type = {};

        DWORD sym_tag = 0;
        type_symbol->get_symTag(&sym_tag);

        if (sym_tag == SymTagPointerType) {
            variable_type.kind = breakpoint_plugin::VariableKind::Pointer;
            variable_type.cpp_type = "void*";
            return variable_type;
        }

        if (sym_tag != SymTagBaseType) return std::nullopt;

        DWORD base_type = 0;
        ULONGLONG length = 0;
        type_symbol->get_baseType(&base_type);
        type_symbol->get_length(&length);

        switch (base_type) {
            case btBool:
                variable_type.kind = breakpoint_plugin::VariableKind::Bool;
                variable_type.cpp_type = "bool";
                return variable_type;
            case btChar:
                variable_type.kind = breakpoint_plugin::VariableKind::Char;
                variable_type.cpp_type = "char";
                return variable_type;
            case btWChar:
                variable_type.kind = breakpoint_plugin::VariableKind::WChar;
                variable_type.cpp_type = "wchar_t";
                return variable_type;            
            case btInt:
            case btLong:
                switch (length) {
                    case 1: {
                        variable_type.kind = breakpoint_plugin::VariableKind::Int8;
                        variable_type.cpp_type = "int8_t";
                        break;
                    }
                    case 2: {
                        variable_type.kind = breakpoint_plugin::VariableKind::Int16;
                        variable_type.cpp_type = "int16_t";
                        break;
                    }
                    case 4: {
                        variable_type.kind = breakpoint_plugin::VariableKind::Int32;
                        variable_type.cpp_type = "int32_t";
                        break;
                    }
                    case 8: {
                        variable_type.kind = breakpoint_plugin::VariableKind::Int64;
                        variable_type.cpp_type = "int64_t";
                        break;
                    }
                }
                return variable_type;
            case btUInt:
            case btULong:
                switch (length) {
                    case 1: {
                        variable_type.kind = breakpoint_plugin::VariableKind::UInt8;
                        variable_type.cpp_type = "uint8_t";
                        break;
                    }
                    case 2: {
                        variable_type.kind = breakpoint_plugin::VariableKind::UInt16;
                        variable_type.cpp_type = "uint16_t";
                        break;
                    }
                    case 4: {
                        variable_type.kind = breakpoint_plugin::VariableKind::UInt32;
                        variable_type.cpp_type = "uint32_t";
                        break;
                    }
                    case 8: {
                        variable_type.kind = breakpoint_plugin::VariableKind::UInt64;
                        variable_type.cpp_type = "uint64_t";
                        break;
                    }
                }
                return variable_type;
            case btFloat:
                variable_type.kind = length > 4 ? breakpoint_plugin::VariableKind::Double : breakpoint_plugin::VariableKind::Float;
                variable_type.cpp_type = length > 4 ? "double" : "float";
                return variable_type;
            default:
                return std::nullopt;
        }
    }

    std::vector<VariableInfo> resolve_variables(const OriginalAddress original_address, const uintptr_t image_base) {
        std::vector<VariableInfo> vars;
        if (!dia_session || original_address < image_base) return vars;

        RVA rva = static_cast<RVA>(original_address.get() - image_base);

        CComPtr<IDiaSymbol> function_symbol;
        HRESULT h_result = dia_session->findSymbolByRVA(rva, SymTagFunction, &function_symbol);
        if (FAILED(h_result) || !function_symbol) return vars;

        CComPtr<IDiaEnumSymbols> data_symbols;
        h_result = function_symbol->findChildren(SymTagData, nullptr, nsNone, &data_symbols);
        if (FAILED(h_result) || !data_symbols) return vars;

        CComPtr<IDiaSymbol> data_symbol;
        ULONG fetched = 0;
        while (SUCCEEDED(data_symbols->Next(1, &data_symbol, &fetched)) && fetched == 1) {
            CComBSTR name;
            data_symbol->get_name(&name);

            CComPtr<IDiaSymbol> type_symbol;
            data_symbol->get_type(&type_symbol);

            VariableInfo info{};
            if (name.m_str) {
                int size = WideCharToMultiByte(CP_UTF8, 0, name.m_str, -1, nullptr, 0, nullptr, nullptr);
                if (size > 0) {
                    info.name.resize(size - 1);
                    WideCharToMultiByte(CP_UTF8, 0, name.m_str, -1, info.name.data(), size, nullptr, nullptr);
                }
            }

            auto variable_type = resolve_variable_type(type_symbol);
            if (!variable_type) {
                data_symbol.Release();
                continue;
            }

            info.kind = variable_type->kind;
            info.cpp_type = variable_type->cpp_type;

            DWORD location_type = LocIsNull;
            data_symbol->get_locationType(&location_type);

            if (location_type == LocIsEnregistered) {
                DWORD register_id = 0;
                data_symbol->get_registerId(&register_id);
                info.location.kind = VariableLocation::Kind::Register;
                info.location.register_id = register_id;
            } else if (location_type == LocIsRegRel) {
                DWORD register_id = 0;
                LONG offset = 0;
                data_symbol->get_registerId(&register_id);
                data_symbol->get_offset(&offset);
                info.location.kind = VariableLocation::Kind::RegisterRelative;
                info.location.register_id = register_id;
                info.location.offset = offset;
            } else if (location_type == LocIsStatic) {
                RVA rva_static = 0;
                data_symbol->get_relativeVirtualAddress(&rva_static);
                info.location.kind = VariableLocation::Kind::Static;
                info.location.static_address = OriginalAddress {image_base + rva_static};
            } else if (location_type == LocIsNull) {
                // variable optimized out
                info.location.kind = VariableLocation::Kind::Optimized;
            } else {
                // Unsupported location kind (bitfield, TLS, constant, ...) -- skip.
                data_symbol.Release();
                continue;
            }

            vars.push_back(std::move(info));
            data_symbol.Release();
        }

        return vars;
    }

    std::vector<int64_t> pack_incoming_args(const std::vector<VariableInfo> &vars, DWORD thread_id, HANDLE process_handle) {
        std::vector<int64_t> args(vars.size(), 0);

        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        HANDLE thread_handle = OpenThread(THREAD_GET_CONTEXT, FALSE, thread_id);
        if (thread_handle) {
            GetThreadContext(thread_handle, &context);
            CloseHandle(thread_handle);
        }

        for (size_t i = 0; i < vars.size(); ++i) {
            const VariableLocation &loc = vars[i].location;

            switch (loc.kind) {
                case VariableLocation::Kind::Register: {
                    uint64_t value = get_register_from_id(context, loc.register_id);
                    std::memcpy(&args[i], &value, sizeof(value));
                    break;
                }
                case VariableLocation::Kind::RegisterRelative: {
                    uint64_t base = get_register_from_id(context, loc.register_id);
                    TrueAddress address = static_cast<TrueAddress>(base + loc.offset);
                    uint64_t value = 0;

                    ReadProcessMemory(process_handle, reinterpret_cast<LPCVOID>(address.get()), &value, sizeof(value), nullptr);
                    std::memcpy(&args[i], &value, sizeof(value));
                    break;
                }
                case VariableLocation::Kind::Static: {
                    uint64_t value = 0;

                    ReadProcessMemory(process_handle, reinterpret_cast<LPCVOID>(loc.static_address.get()), &value, sizeof(value), nullptr);
                    std::memcpy(&args[i], &value, sizeof(value));
                    break;
                }
            }
        }

        return args;       
    }

    void print_all_pdb_scoped_variables(const OriginalAddress code_address, const DWORD thread_id, const uintptr_t image_base, HANDLE process_handle) {
        CONTEXT context{};
        context.ContextFlags = CONTEXT_FULL;
        HANDLE thread_handle = OpenThread(THREAD_GET_CONTEXT, FALSE, thread_id);
        if (thread_handle) {
            GetThreadContext(thread_handle, &context);
            CloseHandle(thread_handle);
        }

        print_locals(code_address, context, image_base, process_handle);
        print_globals(code_address, context, image_base, process_handle);
    }

    void print_locals(const OriginalAddress original_address, const CONTEXT& ctx, const uintptr_t image_base, HANDLE process_handle) {
        RVA rva = static_cast<RVA>(original_address.get() - image_base);

        CComPtr<IDiaSymbol> function_symbol;
        HRESULT h_result = dia_session->findSymbolByRVA(rva, SymTagFunction, &function_symbol);
        if (FAILED(h_result) || !function_symbol) return;

        CComPtr<IDiaEnumSymbols> data_symbols;
        h_result = function_symbol->findChildren(SymTagData, nullptr, nsNone, &data_symbols);
        if (FAILED(h_result) || !data_symbols) return;

        CComPtr<IDiaSymbol> data_symbol;
        ULONG fetched = 0;
        printf("Local variables:\n");
        while (SUCCEEDED(data_symbols->Next(1, &data_symbol, &fetched)) && fetched == 1) {
            CComBSTR wide_name;
            std::string name;
            data_symbol->get_name(&wide_name);

            CComPtr<IDiaSymbol> type_symbol;
            data_symbol->get_type(&type_symbol);

            if (wide_name.m_str) {
                int size = WideCharToMultiByte(CP_UTF8, 0, wide_name.m_str, -1, nullptr, 0, nullptr, nullptr);
                if (size > 0) {
                    name.resize(size - 1);
                    WideCharToMultiByte(CP_UTF8, 0, wide_name.m_str, -1, name.data(), size, nullptr, nullptr);
                }
            }

            auto variable_type = resolve_variable_type(type_symbol);
            if (!variable_type) {
                data_symbol.Release();
                continue;
            }

            DWORD location_type = LocIsNull;
            data_symbol->get_locationType(&location_type);

            VariableLocation variable_location = {};
            if (location_type == LocIsEnregistered) {
                DWORD register_id = 0;
                data_symbol->get_registerId(&register_id);
                variable_location.kind = VariableLocation::Kind::Register;
                variable_location.register_id = register_id;
            } else if (location_type == LocIsRegRel) {
                DWORD register_id = 0;
                LONG offset = 0;
                data_symbol->get_registerId(&register_id);
                data_symbol->get_offset(&offset);
                variable_location.kind = VariableLocation::Kind::RegisterRelative;
                variable_location.register_id = register_id;
                variable_location.offset = offset;
            } else if (location_type == LocIsStatic) {
                RVA rva_static = 0;
                data_symbol->get_relativeVirtualAddress(&rva_static);
                variable_location.kind = VariableLocation::Kind::Static;
                variable_location.static_address = OriginalAddress {image_base + rva_static};
            } else if (location_type == LocIsNull) {
                // variable optimized out
                variable_location.kind = VariableLocation::Kind::Optimized;
            } else {
                // Unsupported location kind (bitfield, TLS, constant, ...) -- skip.
                data_symbol.Release();
                continue;
            }

            uint64_t raw_value = read_value_from_kind(variable_location, ctx, process_handle);
            print_variable(variable_type->kind, variable_type->cpp_type, name, raw_value);
            data_symbol.Release();
        };
    }

    // TODO: update so that if dataKind is DataIsFileStatic then we only grab the current file's static variables -> current implementation
    //  Doesn't work since the symtag is getting rolled up into SymTagExe -> so I need to query by the line table like above
    void print_globals(const OriginalAddress code_address, const CONTEXT& ctx, const uintptr_t image_base, HANDLE process_handle) {
        CComPtr<IDiaEnumSymbols> pEnum;
        if (FAILED(dia_global_scope->findChildren(SymTagData, NULL, nsNone, &pEnum))) return;

        CComPtr<IDiaSymbol> pSymbol;
        ULONG celt = 0;

        auto source = load_breakpoint_from_pdb(code_address, image_base);

        printf("File/Global static variables: \n");
        while (SUCCEEDED(pEnum->Next(1, &pSymbol, &celt)) && celt == 1) {
            DWORD dataKind = 0;
            pSymbol->get_dataKind(&dataKind);

            // Filter for Globals, File Statics, or Module-Level Static variables
            if (dataKind == DataIsGlobal || dataKind == DataIsFileStatic || dataKind == DataIsStaticLocal) {
                // Ignore file statics if not our file
                if (dataKind == DataIsFileStatic) {
                    CComPtr<IDiaSymbol> pLexicalParent;
                    pSymbol->get_lexicalParent(&pLexicalParent);
                    DWORD symTag = 0;
                    pLexicalParent->get_symTag(&symTag);

                    if (symTag != SymTagCompiland) {
                        pLexicalParent.Release();
                        pSymbol.Release();
                        continue;
                    };
                    
                    CComBSTR file_name; 
                    pLexicalParent->get_name(&file_name);
                    if (wcscmp(source.file_name, file_name) != 0) {
                        pLexicalParent.Release();
                        pSymbol.Release();
                        continue;
                    };
                    pLexicalParent.Release();
                }
                CComBSTR wide_name;
                pSymbol->get_name(&wide_name);

                std::string name;
                if (wide_name.m_str) {
                    int size = WideCharToMultiByte(CP_UTF8, 0, wide_name.m_str, -1, nullptr, 0, nullptr, nullptr);
                    if (size > 0) {
                        name.resize(size - 1);
                        WideCharToMultiByte(CP_UTF8, 0, wide_name.m_str, -1, name.data(), size, nullptr, nullptr);
                    }
                }

                CComPtr<IDiaSymbol> type_symbol;
                pSymbol->get_type(&type_symbol);

                auto variable_type = resolve_variable_type(type_symbol);
                if (!variable_type) {
                    pSymbol.Release();
                    continue;
                }

                DWORD locType = 0;
                pSymbol->get_locationType(&locType);

                VariableLocation variable_location = {};
                // Globals are mapped statically to Relative Virtual Addresses (RVA)
                if (locType == LocIsStatic) {
                    RVA rva_static = 0;
                    pSymbol->get_relativeVirtualAddress(&rva_static);
                    variable_location.kind = VariableLocation::Kind::Static;
                    variable_location.static_address = OriginalAddress {image_base + rva_static};
                } else {
                    pSymbol.Release();
                    continue;                   
                }
                
                uint64_t raw_value = read_value_from_kind(variable_location, ctx, process_handle);
                print_variable(variable_type->kind, variable_type->cpp_type, name, raw_value);
            }
            pSymbol.Release();
        }
    }

    // TODO: update to use any pdb file specified.  If no pdb specified then look at current dll first and if not found then loop over all loaded dlls
    // TODO: if data address supplied then use the data address to fetch the values (no recursion though so no further than pointers)
    void print_object_from_type(const std::string& object_type, const std::string& pdb_file, const uintptr_t data_address) {
        CComPtr<IDiaEnumSymbols> symbol_enumerator {};
        dia_global_scope->findChildren(SymTagUDT, str_to_wstr(object_type).data(), nsNone, &symbol_enumerator);

        CComPtr<IDiaSymbol> symbol_udt {};
        ULONG celt {};
        if (symbol_enumerator->Next(1, &symbol_udt, &celt) == S_OK && celt > 0) {
            CComBSTR udt_name {};
            symbol_udt->get_name(&udt_name);
            wprintf(L"Struct: %s\n", udt_name.m_str);

            // Get members of the struct
            CComPtr<IDiaEnumSymbols> udt_member_enumarator {};
            symbol_udt->findChildren(SymTagNull, NULL, nsNone, &udt_member_enumarator);
            
            CComPtr<IDiaSymbol> struct_member {};
            while (udt_member_enumarator->Next(1, &struct_member, &celt) == S_OK && celt > 0) {
                CComBSTR member_name {};
                DWORD tag {};
                ULONGLONG length {};
                LONG offset {};
                DWORD location_type {};

                struct_member->get_name(&member_name);
                struct_member->get_offset(&offset);
                struct_member->get_locationType(&location_type);

                std::wstring type_name;
                CComPtr<IDiaSymbol> field_type;
                if (struct_member->get_type(&field_type) == S_OK) {
                    type_name = get_type_name(field_type);
                }

                if (location_type == LocIsBitField) {
                    DWORD position {};
                    ULONGLONG length {};
                    struct_member->get_bitPosition(&position);
                    struct_member->get_length(&length);

                    wprintf(L"  +0x%03x %-20s : Pos %d, %I64u Bits\n", 
                            offset, member_name.m_str, position, length);
                } else {
                    wprintf(L"  +0x%03x %-20s : %s\n", offset, member_name.m_str, type_name.c_str());
                }

                struct_member.Release();
            }
        }
    }

    uint64_t read_value_from_kind(const VariableLocation& loc, const CONTEXT& ctx, HANDLE process_handle) {
        switch (loc.kind) {
            case VariableLocation::Kind::Register: {
                return get_register_from_id(ctx, loc.register_id);
            }
            case VariableLocation::Kind::RegisterRelative: {
                uint64_t base = get_register_from_id(ctx, loc.register_id);
                TrueAddress address = static_cast<TrueAddress>(base + loc.offset);
                uint64_t value = 0;

                ReadProcessMemory(process_handle, reinterpret_cast<LPCVOID>(address.get()), &value, sizeof(value), nullptr);
                return value;
            }
            case VariableLocation::Kind::Static: {
                uint64_t value = 0;

                ReadProcessMemory(process_handle, reinterpret_cast<LPCVOID>(loc.static_address.get()), &value, sizeof(value), nullptr);
                return value;
            }
        }
        return 0;
    }

    OriginalAddress get_address_from_func_name(const std::string& name, const uintptr_t image_base) {
        CComPtr<IDiaEnumSymbols> pEnumSymbols;
        CComBSTR bstrName(CA2W(name.data()));

        HRESULT hr = dia_global_scope->findChildren(
            SymTagFunction,        
            bstrName,          
            nsNone,              
            &pEnumSymbols
        );

        if (SUCCEEDED(hr) && pEnumSymbols != nullptr) {
            CComPtr<IDiaSymbol> pFunctionSymbol;
            ULONG celt = 0;

            // Grab the first matching symbol found
            if (pEnumSymbols->Next(1, &pFunctionSymbol, &celt) == S_OK && celt == 1) {
                RVA rva = 0;
                
                if (pFunctionSymbol->get_relativeVirtualAddress(&rva) == S_OK) {
                    return OriginalAddress {rva + image_base};
                } else {
                    // Function might be inlined or stripped of a direct address
                    printf("Could not grab address for function\n");
                }
            } else {
                printf("Function name not found in symbols\n");
            }
        }
        return OriginalAddress {0};
    }

    OriginalAddress get_address_from_c_label(const std::string& name, const uintptr_t image_base) {
        CComPtr<IDiaEnumSymbols> pEnumSymbols;
        CComBSTR bstrName(CA2W(name.data()));

        HRESULT hr = dia_global_scope->findChildren(
            SymTagLabel,        
            bstrName,          
            nsNone,              
            &pEnumSymbols
        );

        if (SUCCEEDED(hr) && pEnumSymbols != nullptr) {
            CComPtr<IDiaSymbol> pLabelSymbol;
            ULONG celt = 0;

            // Grab the first matching symbol found
            if (pEnumSymbols->Next(1, &pLabelSymbol, &celt) == S_OK && celt == 1) {
                RVA rva = 0;
                
                if (pLabelSymbol->get_relativeVirtualAddress(&rva) == S_OK) {
                    return OriginalAddress {rva + image_base};
                } else {
                    // Function might be inlined or stripped of a direct address
                    printf("Could not grab address for label\n");
                }
            } else {
                printf("label name not found in symbols\n");
            }
        }
        return OriginalAddress {0};    
    }

    OriginalAddress get_address_from_file_line(const std::string& name, const uintptr_t image_base) {
        std::string_view delimiter = ":";
        auto split = std::views::split(name, delimiter) | std::ranges::to<std::vector<std::string>>();
        auto file = split[0];
        auto line = split[1];

        auto targetLine = std::stoull(line.data()); 

        CComPtr<IDiaEnumSourceFiles> pSourceFiles;
        CComBSTR bstrName(CA2W(file.data()));
        std::wstring wide_file(bstrName);

        // look through all files
        HRESULT hr = dia_session->findFile(
            nullptr,        
            nullptr,          
            nsCaseInsensitive,              
            &pSourceFiles
        );

        if (SUCCEEDED(hr) && pSourceFiles != nullptr) {
            CComPtr<IDiaSourceFile> pSourceFile;
            ULONG celt = 0;

            while (pSourceFiles->Next(1, &pSourceFile, &celt) == S_OK && celt == 1) {
                RVA rva = 0;

                CComBSTR bstrFileName;
                pSourceFile->get_fileName(&bstrFileName);
                std::wstring currentPath(bstrFileName);

                if (!currentPath.contains(wide_file)) {
                    pSourceFile.Release();
                    continue;
                }
                

                CComPtr<IDiaEnumSymbols> pEnumCompilands;
                pSourceFile->get_compilands(&pEnumCompilands);
                CComPtr<IDiaSymbol> pCompiland;
                while(pEnumCompilands->Next(1, &pCompiland, &celt) == S_OK &&celt == 1) {
                    CComPtr<IDiaEnumLineNumbers> pLines;
                    hr = dia_session->findLinesByLinenum(pCompiland, pSourceFile, static_cast<DWORD>(targetLine), 0, &pLines);

                    CComPtr<IDiaLineNumber> pLine;
                    if (SUCCEEDED(hr) && pLines && SUCCEEDED(pLines->Next(1, &pLine, &celt)) && celt == 1) 
                    {
                        RVA rva = 0;
                        pLine->get_relativeVirtualAddress(&rva);
                        return OriginalAddress {rva + image_base};
                    }
                    pLines.Release();
                    pLine.Release();
                    pCompiland.Release();
                }
            }
            printf("Source File not found or line doesn't exist");
        }
        return OriginalAddress {0};
    }

    FunctionBounds get_function_bounds_from_address(const OriginalAddress code_address, const uintptr_t image_base) {
        RVA rva = static_cast<RVA>(code_address.get() - image_base);

        FunctionBounds function_bounds;

        CComPtr<IDiaSymbol> pFuncSymbol;
        DWORD64 function_length = 1; // Default fallback
        DWORD function_rva;

        if (SUCCEEDED(dia_session->findSymbolByRVA(rva, SymTagFunction, &pFuncSymbol))) {
            pFuncSymbol->get_length(&function_length);
            pFuncSymbol->get_relativeVirtualAddress(&function_rva);
            OriginalAddress function_start {image_base + function_rva};
            function_bounds.start = function_start.get();
            function_bounds.end = function_start.get() + function_length;
        }
        return function_bounds;
    }

    std::vector<FunctionRange> get_all_functions(const uintptr_t image_base) {
        std::vector<FunctionRange> functions;

        CComPtr<IDiaEnumSymbols> func_symbols;
        if (FAILED(dia_global_scope->findChildren(SymTagFunction, nullptr, nsNone, &func_symbols))) {
            return functions;
        }

        CComPtr<IDiaSymbol> func_symbol;
        ULONG fetched = 0;
        while (SUCCEEDED(func_symbols->Next(1, &func_symbol, &fetched)) && fetched == 1) {
            RVA rva = 0;
            ULONGLONG length = 0;
            func_symbol->get_relativeVirtualAddress(&rva);
            func_symbol->get_length(&length);

            if (rva != 0 && length != 0) {
                CComBSTR wide_name;
                func_symbol->get_name(&wide_name);

                std::string name;
                if (wide_name.m_str) {
                    int size = WideCharToMultiByte(CP_UTF8, 0, wide_name.m_str, -1, nullptr, 0, nullptr, nullptr);
                    if (size > 0) {
                        name.resize(size - 1);
                        WideCharToMultiByte(CP_UTF8, 0, wide_name.m_str, -1, name.data(), size, nullptr, nullptr);
                    }
                }

                uintptr_t start = image_base + rva;
                functions.push_back({OriginalAddress {start}, OriginalAddress {start + length}, std::move(name)});
            }
            func_symbol.Release();
        }
        return functions;        
    }

    // TODO: cache results
    std::vector<LineRange> get_line_ranges_from_address(const OriginalAddress original_address, const uintptr_t image_base) {
        RVA rva = static_cast<RVA>(original_address.get() - image_base);
        std::vector<LineRange> line_ranges {};

        // query line at address
        CComPtr<IDiaEnumLineNumbers> lines;
        if (FAILED(dia_session->findLinesByRVA(rva, 1, &lines))) return line_ranges;

        CComPtr<IDiaLineNumber> line;
        ULONG celt = 0;
        if (FAILED(lines->Next(1, &line, &celt)) || celt == 0) return line_ranges;

        DWORD target_line = 0;
        line->get_lineNumber(&target_line);

        CComPtr<IDiaSourceFile> source_file;
        if (FAILED(line->get_sourceFile(&source_file))) return line_ranges;

        CComBSTR file_name = nullptr;
        if (FAILED(source_file->get_fileName(&file_name))) return line_ranges;

        CComPtr<IDiaSymbol> line_compiland;
        if (FAILED(line->get_compiland(&line_compiland))) return line_ranges;

        CComPtr<IDiaEnumLineNumbers> all_line_ranges;
        if (FAILED(dia_session->findLinesByLinenum(line_compiland, source_file, target_line, 0, &all_line_ranges))) return line_ranges;

        CComPtr<IDiaLineNumber> line_range;
        while (SUCCEEDED(all_line_ranges->Next(1, &line_range, &celt)) && celt == 1) {
            DWORD line_number = 0;
            RVA line_rva = 0;
            DWORD length = 0;
            line_range->get_lineNumber(&line_number);
            line_range->get_relativeVirtualAddress(&line_rva);
            line_range->get_length(&length);
            
            // if code is inlined then its associated with the same line but we'd want to be able to
            // "step" to the next line or whatever so we need to remove inlined lines from our line information
            if (length != 0) {
                CComPtr<IDiaSourceFile> compare_source_file;
                if (SUCCEEDED(line->get_sourceFile(&compare_source_file))) {
                    CComBSTR compare_file_name = nullptr;
                    if (SUCCEEDED(compare_source_file->get_fileName(&compare_file_name))) {
                        if(wcscmp(compare_file_name, file_name) == 0) {
                            line_ranges.push_back({
                                OriginalAddress {image_base + line_rva}, 
                                OriginalAddress {image_base + line_rva + length}
                            });
                        }
                    }
                }
            }
            line_range.Release();
        }

        // sort and merge contiguous entries so we know for sure where a line "ends" and "begins"
        std::sort(line_ranges.begin(), line_ranges.end(),
                  [](const LineRange& lhs, const LineRange& rhs) { return lhs.start < rhs.start; });        

        std::vector<LineRange> merged;
        for (const LineRange& range : line_ranges) {
            if (!merged.empty() && range.start <= merged.back().end) {
                merged.back().end = std::max(merged.back().end, range.end);
            } else {
                merged.push_back(range);
            }
        }

        return merged;
    }

    bool address_in_line_ranges(const std::vector<LineRange> &line_ranges, const OriginalAddress address) {
        for (const auto line_range : line_ranges) {
            if (address < line_range.end && address >= line_range.start) return true;
        }
        return false;
    }
}
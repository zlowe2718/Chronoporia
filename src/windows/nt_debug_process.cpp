// #include "nt_debug_process.h"
// #include "nt_wrappers.h"
// #include "nt_thread.h"
// #include "nt_dlls.h"
// #include "trampoline.h"
// #include "breakpoints.h"
// #include "globals.h"
// #include "event_log.h"
// #include "shellcode.h"
// #include <chrono>


// namespace {
//     uintptr_t child_entry_address;
//     bool process_loaded = false;
// }

// namespace chronoporia {
//     bool AttachToProcess() {
//         DebugSetProcessKillOnExit(FALSE);

//         return true;
//     }

//     // windows suspends the program when a debug event is sent so we don't have to worry about manually suspending
//     // TODO: Need to track the entry load breakpoint to take a snapshot then like before
//     void DebugLoop() {
//         DEBUG_EVENT de;
//         DWORD last_error;
//         bool debug_event_success;

//         long long wait_timeout = globals::snapshot_interval * 1000; //microseconds
//         auto execution_resume_time = std::chrono::system_clock::now();

//         while (globals::running) {
//             debug_event_success = WaitForDebugEvent(&de, wait_timeout / 1000); //convert back to milliseconds
//             auto execution_stop_time = std::chrono::system_clock::now();

//             if (debug_event_success) {
//                 long long microseconds_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(execution_stop_time - execution_resume_time).count(); 
//                 wait_timeout -= microseconds_elapsed;

//                 if (wait_timeout < 0) wait_timeout = 0;

//                 DWORD continue_status = DBG_CONTINUE;

//                 switch (de.dwDebugEventCode) {

//                     case EXCEPTION_DEBUG_EVENT:
//                         continue_status = HandleDebugException(&de);
//                         break;
//                     // TODO: This will eventually be removed in favor of tracking the actual thread create/destroy call
//                     // case CREATE_THREAD_DEBUG_EVENT:
//                     //     TrackThread(de.dwThreadId);
//                     //     break;
//                     // case EXIT_THREAD_DEBUG_EVENT:
//                     //     UntrackThread(de.dwThreadId);
//                     //     break;
//                 //     case EXIT_PROCESS_DEBUG_EVENT:
//                 //         globals::running = false;
//                 //         continue_status = DBG_EXCEPTION_NOT_HANDLED;
//                 //         printf("Process exiting with error code %lx\n", de.u.ExitProcess.dwExitCode);
//                 //         break;
//                     // TODO: This will eventually be removed in favor of tracking the actual dll load/unload call
//                     case LOAD_DLL_DEBUG_EVENT:
//                         TrackDLL(de);
//                         break;
//                     case UNLOAD_DLL_DEBUG_EVENT:
//                         UntrackDLL(de.u.UnloadDll.lpBaseOfDll);
//                         break;
//                 }

//                 ContinueDebugEvent(
//                     de.dwProcessId,
//                     de.dwThreadId,
//                     continue_status
//                 );
//                 execution_resume_time = std::chrono::system_clock::now();
//             } else {
//                 last_error = GetLastError();

//                 // timeout occurred, take a snapshot.  Threads are NOT suspended on a timeout
//                 if (last_error == ERROR_SEM_TIMEOUT) {
//                     SuspendProcess();
//                     // std::vector<DirtyPage> dirty_pages = GetDirtyPages();
//                     // TakeCoarseSnapshot(&dirty_pages);
//                     // UntrackAllDirtyPages();
//                     // RestoreMemoryAtAllBreakpoints();
//                     // TODO: fix this later, right now this is hard coded to the first snapshot
//                     JumpToEvent(0);
//                     // SetBreakpointAtAddress(child_entry_address, BreakpointCaller::EntryBreakpoint);
//                     ResumeProcess();
//                     wait_timeout = globals::snapshot_interval * 1000;
//                     // current_handling_bp_address = nullptr;
//                 } else {
//                     printf("Unknown error encountered from WaitDebugEvent %ld\n", last_error);
//                     globals::running = false;
//                 }
//                 execution_resume_time = std::chrono::system_clock::now();
//             }
//         }       
//     }

//     DWORD HandleDebugException(const DEBUG_EVENT* debug_event) {
//         const EXCEPTION_RECORD* er = &debug_event->u.Exception.ExceptionRecord;

//         switch (er->ExceptionCode) {
//             case STATUS_BREAKPOINT: {
//                 // Can only set the child breakpoint once the windows debug breakpoint has been sent
//                 // This guarantees all necessary dlls (including child process) for launch have been initialized 
//                 if (!process_loaded) {
//                     process_loaded = true;
//                     child_entry_address = GetChildEntryAddress();
//                     CreateReturnBreakpoint(child_entry_address, 0);
//                     CreateTrampolineRegion();
//                     SetupNonDetCapture();
//                     return DBG_CONTINUE;
//                 }
//                 return HandleBreakpoint(debug_event);
//             }

//             // case STATUS_GUARD_PAGE_VIOLATION: {
//             //     // TODO: Do I need to worry about stack overflows messing with this?
//             //     ULONG_PTR fault_address = er->ExceptionInformation[1];
//             //     ULONG_PTR fault_page_start_addr = fault_address & ~0xFFFULL; //reset the page nibble

//             //     if (!WasPageArmed(reinterpret_cast<void *>(fault_page_start_addr))) {
//             //         return DBG_EXCEPTION_NOT_HANDLED;
//             //     }

//             //     TrackDirtyPage(reinterpret_cast<void *>(fault_page_start_addr));
//             //     return DBG_CONTINUE;
//             // }

//             // We haven't handled this case yet log and print what happened
//             // default: {
//             //     CONTEXT ctx = GetThreadContextFromId(debug_event->dwThreadId);
//             //     MEMORY_BASIC_INFORMATION m_addr;
//             //     MEMORY_BASIC_INFORMATION m_rip;
//             //     MEMORY_BASIC_INFORMATION m_rsp;
//             //     VirtualQueryEx(globals::process_handle, (void*)er->ExceptionAddress, &m_addr, sizeof(m_addr));
//             //     VirtualQueryEx(globals::process_handle, (void*)ctx.Rip, &m_rip, sizeof(m_rip));
//             //     VirtualQueryEx(globals::process_handle, (void*)ctx.Rsp, &m_rsp, sizeof(m_rsp));
//             //     printf("(Thread: %ld) Unhandled Debug Exception: \n"
//             //            "    Exception Code: %ld\n"
//             //            "    Exception Address: %p\n"
//             //            "    Thread Rip: %p\n"
//             //            "    Thread Rsp: %p\n"
//             //         , debug_event->dwThreadId
//             //         , er->ExceptionCode
//             //         , ctx.Rip
//             //         , ctx.Rsp
//             //     );
//             //     return DBG_EXCEPTION_NOT_HANDLED;
//             // }
//         }

//         return DBG_EXCEPTION_NOT_HANDLED;
//     }

//     DWORD HandleBreakpoint(const DEBUG_EVENT *debug_event) {
//         DWORD thread_id = debug_event->dwThreadId;
//         // Need to roll back one instruction as the rip stores the pointer to the next instruction
//         uintptr_t rip_address = GetRipAddress(thread_id) - 1;
//         auto bp_type = GetBreakpointType(rip_address, thread_id);

//         CONTEXT ctx = RollBackInstructionPointRegister(thread_id);
//         if (rip_address == child_entry_address) {
//             RemoveBreakpoint(child_entry_address, 0);

//             auto snapshot = CreateCoarseSnapshotEvent(thread_id, false);
//             LogEvent(std::move(snapshot));
//         } else {
//             switch (bp_type) {
//                 case BreakpointType::Return: {
//                     OnBreakpointReturn(rip_address, thread_id, ctx);
//                     break;
//                 }
//                 case BreakpointType::Permanent: {
//                     OnBreakpointEnter(rip_address, thread_id);
//                     break;
//                 }
//                 case BreakpointType::SpinLock: {
//                     break;
//                 }
//                 case BreakpointType::ShellCode: {
//                     FreeShellCodeAtAddress(code_address);
//                     break;
//                 }
//             }
//         }
//         return DBG_CONTINUE;
//         // BreakpointCaller bp_caller = GetBreakpointCaller(rip_address);

//         // switch (bp_caller) {
//         //     case BreakpointCaller::EnterNtAllocateVirtualMemory: {
//         //         HandleEnterNtAllocateVirtualMemory(debug_event);
//         //         SuspendAllThreadsExceptCurrent(debug_event->dwThreadId);
//         //         break;
//         //     }
//         //     case BreakpointCaller::ReturnNtAllocateVirtualMemory: {
//         //         ResumeAllThreads();
//         //         HandleReturnNtAllocateVirtualMemory(debug_event);
//         //         break;
//         //     }
//         //     case BreakpointCaller::EnterNtFreeVirtualMemory: {
//         //         HandleEnterNtFreeVirtualMemory(debug_event);
//         //         SuspendAllThreadsExceptCurrent(debug_event->dwThreadId);
//         //         break;
//         //     }
//         //     case BreakpointCaller::ReturnNtFreeVirtualMemory: {
//         //         ResumeAllThreads();
//         //         HandleReturnNtFreeVirtualMemory(debug_event);
//         //         break;
//         //     }
//         //     case BreakpointCaller::EnterNtProtectVirtualMemory: {
//         //         HandleEnterNtProtectVirtualMemory(debug_event);
//         //         SuspendAllThreadsExceptCurrent(debug_event->dwThreadId);
//         //         break;
//         //     }
//         //     case BreakpointCaller::ReturnNtProtectVirtualMemory: {
//         //         ResumeAllThreads();
//         //         HandleReturnNtProtectVirtualMemory(debug_event);
//         //         break;
//         //     }
//         //     case BreakpointCaller::UserBreakpoint: {
//         //         // Once LdrInitializeThunk is complete then we should be safe to copy memory,
//         //         // thread contexts, and place breakpoints
//         //         if (!ntdll_loader_initialized) {
//         //             TrackAllProcessThreads();

//         //             SetBreakpointAtAddress(child_entry_address, BreakpointCaller::EntryBreakpoint);
//         //             ntdll_loader_initialized = true;
//         //             return DBG_CONTINUE;
//         //         }
//         //         // A second thread hit a breakpoint we're currently handling and Windows queued up both sequentially
//         //         // so just roll the instruction back
//         //         if (rip_address == current_handling_bp_address) {
//         //             RollBackInstructionPointRegister(debug_event->dwThreadId);
//         //             return DBG_CONTINUE;
//         //         }
//         //         return DBG_EXCEPTION_NOT_HANDLED;
//         //     }
//         //     case BreakpointCaller::EntryBreakpoint: {
//         //         RestoreMemoryAtAddress(child_entry_address);
//         //         RollBackInstructionPointRegister(debug_event->dwThreadId);
//         //         TakeCoarseSnapshot();
//         //         ArmAllPages();
//         //         InterceptNtVirtualMemory();
//         //         printf("Snapshot taken\n");
//         //     }
//         // }
//     }

//     void SetupNonDetCapture() {
//         CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(NtCreateThreadEx));
//         FinalizeTrampolineRegion();
//     }
// }
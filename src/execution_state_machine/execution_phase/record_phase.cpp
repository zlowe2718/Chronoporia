#include "record_phase.h"
#include "nt_wrappers.h"
#include "thread_utils.h"
#include "trampoline.h"
#include "breakpoint_manager.h"
#include "globals.h"
#include "event_log.h"
#include "snapshot_events.h"
#include "module_manager.h"
#include "module_utils.h"
#include <chrono>
#include <minwinbase.h>


namespace chronoporia {

    // On Recording start create the trampoline of non-det functions
    void RecordingPhase::Enter() {
        child_entry_address = GetChildEntryAddress();

        if (process_suspended) {
            ResumeProcess();
        }
    }

    Transition RecordingPhase::Run() {
        ResumeProcess();
        return DebugLoop();
    }

    void RecordingPhase::Exit() {
        RemoveAllPermanentBreakpoints();
        DestroyTrampolineRegion();
    }

    Transition RecordingPhase::DebugLoop() {
        DEBUG_EVENT de;
        DWORD last_error;
        bool debug_event_success;

        long long wait_timeout = globals::snapshot_interval * 1000; //microseconds
        auto execution_resume_time = std::chrono::system_clock::now();

        while (globals::running) {
            debug_event_success = WaitForDebugEvent(&de, wait_timeout / 1000); //convert back to milliseconds
            auto execution_stop_time = std::chrono::system_clock::now();

            if (debug_event_success) {
                long long microseconds_elapsed = std::chrono::duration_cast<std::chrono::microseconds>(execution_stop_time - execution_resume_time).count(); 
                wait_timeout -= microseconds_elapsed;

                if (wait_timeout < 0) wait_timeout = 0;

                DWORD continue_status = DBG_CONTINUE;

                switch (de.dwDebugEventCode) {

                    case EXCEPTION_DEBUG_EVENT:
                        continue_status = HandleDebugException(&de);
                        break;
                    case LOAD_DLL_DEBUG_EVENT: {
                        LOAD_DLL_DEBUG_INFO debug_info = de.u.LoadDll;
                        std::wstring image_name = GetDllNameFromLp(debug_info.lpImageName);
                        CloseHandle(debug_info.hFile);
                        TrackDLL(reinterpret_cast<HMODULE>(debug_info.lpBaseOfDll), image_name, globals::global_sequence);
                        break;
                    }
                    case UNLOAD_DLL_DEBUG_EVENT: {
                        LOAD_DLL_DEBUG_INFO debug_info = de.u.LoadDll;
                        UntrackDLL(reinterpret_cast<HMODULE>(debug_info.lpBaseOfDll), globals::global_sequence);
                        break;
                    }
                    case EXIT_PROCESS_DEBUG_EVENT:
                        globals::running = false;
                        continue_status = DBG_EXCEPTION_NOT_HANDLED;
                        printf("Process exiting with error code %lx\n", de.u.ExitProcess.dwExitCode);
                        break;
                }

                ContinueDebugEvent(
                    de.dwProcessId,
                    de.dwThreadId,
                    continue_status
                );
                execution_resume_time = std::chrono::system_clock::now();
            } else {
                last_error = GetLastError();

                // timeout occurred, take a snapshot.  Threads are NOT suspended on a timeout
                if (last_error == ERROR_SEM_TIMEOUT) {
                    SuspendProcess();
                    // std::vector<DirtyPage> dirty_pages = GetDirtyPages();
                    // TakeCoarseSnapshot(&dirty_pages);
                    // UntrackAllDirtyPages();
                    // RestoreMemoryAtAllBreakpoints();
                    uint64_t target_sequence = GetMostRecentCoarseEvent();
                    return TransitionToTimeRestore {true, target_sequence };
                    // SetBreakpointAtAddress(child_entry_address, BreakpointCaller::EntryBreakpoint);
                    // ResumeProcess();
                    // wait_timeout = globals::snapshot_interval * 1000;
                    // current_handling_bp_address = nullptr;
                } else {
                    printf("Unknown error encountered from WaitDebugEvent %ld\n", last_error);
                    globals::running = false;
                    return TransitionToError {last_error};
                }
                execution_resume_time = std::chrono::system_clock::now();
            }
        }

        return TransitionToError {0};
    }

    DWORD RecordingPhase::HandleDebugException(const DEBUG_EVENT* debug_event) {
        const EXCEPTION_RECORD* er = &debug_event->u.Exception.ExceptionRecord;

        switch (er->ExceptionCode) {
            case STATUS_BREAKPOINT: {
                return HandleBreakpoint(debug_event);
            }
        }

        return DBG_EXCEPTION_NOT_HANDLED;
    }

    DWORD RecordingPhase::HandleBreakpoint(const DEBUG_EVENT *debug_event) {
        DWORD thread_id = debug_event->dwThreadId;
        // Need to roll back one instruction as the rip stores the pointer to the next instruction
        uintptr_t rip_address = GetRipAddress(thread_id) - 1;
        auto bp_type = GetBreakpointType(rip_address, thread_id);

        CONTEXT ctx = RollBackInstructionPointRegister(thread_id);
        if (rip_address == child_entry_address) {
            RemoveBreakpoint(child_entry_address, globals::main_thread_id);

            auto snapshot = CreateCoarseSnapshotEvent(thread_id, false);
            LogEvent(std::move(snapshot));

            // make trampoline after we save the memory other I'm saving the breakpoints in the data
            CreateTrampolineRegion();
            CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(NtCreateThreadEx));
            CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(NtTerminateThread));
            CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(LdrLoadDll));
            CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(LdrUnloadDll));
            FinalizeTrampolineRegion();
        } else {
            switch (bp_type) {
                case BreakpointType::Return: {
                    OnBreakpointReturn(rip_address, thread_id, ctx);
                    break;
                }
                case BreakpointType::Permanent: {
                    OnBreakpointEnter(rip_address, thread_id);
                    break;
                }
                case BreakpointType::SpinLock: {
                    break;
                }
            }
        }
        return DBG_CONTINUE;
    }

    void RecordingPhase::SetupNonDetCapture() {
        CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(NtCreateThreadEx));
        FinalizeTrampolineRegion();
    }

}
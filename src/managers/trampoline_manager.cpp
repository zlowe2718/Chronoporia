#include "breakpoint_manager.h"
#include "memory_manager.h"
#include "nt_wrappers.h"
#include "trampoline.h"

namespace chronoporia {

    void LoadTrampoline() {
        CreateTrampolineRegion();
        CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(NtCreateThreadEx));
        CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(NtTerminateThread));
        CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(LdrLoadDll));
        CreatePermanentBreakpoint(reinterpret_cast<uintptr_t>(LdrUnloadDll));
        uintptr_t trampoline_address = FinalizeTrampolineRegion();

        AddBlacklistAddress(trampoline_address);
    }

    void UnloadTrampoline() {
        RemoveAllPermanentBreakpoints();
        DestroyTrampolineRegion();
    }

}
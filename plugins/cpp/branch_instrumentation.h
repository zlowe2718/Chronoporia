#pragma once

#include "base_breakpoint_plugin.h"
#include "pdb.h"
#include <Windows.h>
#include <Zydis/Zydis.h>
#include <cstdint>
#include <utility>
#include <vector>

// This creates a trampoline that increments a global per-thread execution counter at each instrumented basic
// block's entry.  The purpose is to not need hardware performance counters because while faster they're not
// 100% determinstic depending on the cpu architecture.  Since we only need retired branch counters we can
// instrument a "fast" version in the code

// As an example I'll abstract the trampoline away and show it would work for writing it manually
// For some simple function: 
// int count_positives(const int* data, int n) {
//     int count = 0;
//     for (int i = 0; i < n; ++i) {
//         if (data[i] > 0)
//             count += data[i];
//     }
//     return count;
// }
// 
// The instrumented version looks like something below
// thread_local uint64_t block_hits;

// int count_positives_instrumented(const int* data, int n) {
//     int count = 0;
//     ++block_hits;
//     for (int i = 0; i < n; ++i) {
//         ++block_hits;
//         if (i < n) {             // added to show the i < n condition in the for loop
//             ++block_hits[2];
//             if (data[i] > 0) {
//                 ++block_hits[3];
//                 count += data[i];
//             }
//             ++block_hits[4];
//             ++i;                 // added to show the ++i section in the for loop
//         }
//     }
//     ++block_hits[5];                       // <-- B5 entry (hit once)
//     return count;
// }

namespace breakpoint_plugin {

    enum class BranchKind {
        Conditional,      // jcc  (rel8 / rel32)
        UnconditionalJmp, // jmp  (rel8 / rel32)
        Call,             // call (rel32)
        Return,           // ret / retn -- no target
        IndirectJump,     // jmp r/m: target 0, jump table or tail call
        IndirectCall      // call r/m: target 0, only the cpu knows it
    };

    // A single branch instruction found while linearly disassembling a function.
    struct BranchSite {
        OriginalAddress address;      // VA of the branch instruction itself
        OriginalAddress next_address; // VA of the following instruction (fallthrough)
        OriginalAddress target;       // absolute branch target, 0 if indirect/return
        uint8_t   length;       // encoded length of the branch instruction
        BranchKind kind;
        bool is_back_edge;      // target lands earlier inside the same function
    };

    // A loop, recovered from a back-edge (DIA has no loop symbol).
    struct LoopInfo {
        OriginalAddress header;    // top of the loop body (the back-edge target)
        OriginalAddress back_edge; // the branch instruction that closes the loop
    };

    struct FunctionAnalysis {
        FunctionRange info;
        std::vector<BranchSite> branches;
        std::vector<LoopInfo>   loops;
    };

    // A single-entry / single-exit range within one function.
    struct BasicBlock {
        OriginalAddress start; // VA of the block's leader (first instruction)
        OriginalAddress end;   // VA one-past the block's last byte
    };
    // Enumerate functions (via DIA) then disassemble each (via Zydis) to locate
    // every branch and recover loops from back-edges.
    std::vector<FunctionAnalysis> analyze_module(HANDLE process, uintptr_t image_base);

    // Recover basic blocks for one already-analyzed function from its branch
    // list. Leaders are: the function entry, every instruction after a jump
    // (calls and rets do not split a block), and every in-function branch target.
    std::vector<BasicBlock> discover_blocks(const FunctionAnalysis& fn);

    // Reserve the executable region that will hold all stubs. Must be called
    // before instrument_block(). `anchor` is the instrumented module's image
    // base -- the region is placed within ±2GB of it so E9 rel32 detours reach.
    bool create_instrumentation_region(const HANDLE process, const uint32_t tls_counter_offset, const uint32_t tls_target_offset, const uintptr_t image_base, const uint64_t region_size = 0x10000);

    // Emit a stub that bumps the global per-thread counter on entry to this
    // block, then patch the block's leader to detour into it. Returns the
    // stub VA, or 0 if the block is too small to patch / on failure.
    uintptr_t instrument_block(OriginalAddress block_start, OriginalAddress block_end, const uintptr_t image_base);

    // Flush the stub region into the target and apply all recorded patches.
    // Returns the region base, or 0 on failure.
    uintptr_t commit_instrumentation_region();

    void destroy();

    std::pair<TrueAddress, uint64_t> get_true_instruction_address(const OriginalAddress original_address);
    OriginalAddress get_original_instruction_address(const TrueAddress true_address);
    uint16_t address_rsp_fix_bytes(const TrueAddress true_address);

    // If stopped anywhere inside the trampolined region, we return all the original bytes that were displaced
    std::optional<std::vector<uint8_t>> get_original_bytes(const TrueAddress true_address);
}
#include "predecessor_analysis.h"
#include "base_breakpoint_plugin.h"
#include "branch_instrumentation.h"
#include "pdb.h"
#include <Zydis/Zydis.h>
#include <algorithm>
#include <memoryapi.h>
#include <set>
#include <vector>
#include <Windows.h>

// ---- analysis cache -------------------------------------------------------
// Populate ONCE, right after you run analyze_module() and instrument. Keep the
// FunctionAnalysis vector (branch/target data) plus the set of block leaders
// that actually received a stub, and a HANDLE + decoder for backward decodes.
namespace {
    using namespace breakpoint_plugin;

    std::vector<FunctionAnalysis> g_analysis;
    std::set<OriginalAddress>     g_instrumented_leaders;
    HANDLE                        process_handle = nullptr;
    ZydisDecoder                  g_decoder;
}

namespace breakpoint_plugin {
    // Call this from your instrumentation driver. `instrumented_leaders` = every
    // block_start for which instrument_block() returned non-zero (i.e. got a stub).
    // In cpp_breakpoint_plugin.cpp, collect them where you already loop blocks:
    //     if (instrument_block(b.start, b.end)) instrumented_leaders.insert(b.start);
    void init_reverse_step_analysis(HANDLE process,
                                    std::vector<FunctionAnalysis> analysis,
                                    std::set<OriginalAddress> instrumented_leaders) {
        process_handle          = process;
        g_analysis             = std::move(analysis);
        g_instrumented_leaders = std::move(instrumented_leaders);
        ZydisDecoderInit(&g_decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    }

    std::optional<FunctionAnalysis> function_analysis_containing_address(OriginalAddress original_address) {
        for (const auto& fn : g_analysis)
            if (original_address >= fn.info.start && original_address < fn.info.end) return fn;
        return std::nullopt;
    }

    // True iff `rip` is a block leader whose entry actually bumps the TLS counter.
    bool is_instrumented_leader(OriginalAddress rip) {
        return g_instrumented_leaders.count(rip) != 0;
    }

    // Every DIRECT branch/jmp/call whose resolved target == rip. Indirect branches
    // and `ret` have target 0 and are invisible here -- see the note below.
    std::vector<OriginalAddress> branch_sources_targeting(OriginalAddress rip) {
        std::vector<OriginalAddress> out;
        for (const auto& fn : g_analysis)
            for (const auto& b : fn.branches)
                if (b.target == rip)
                    out.push_back(b.address);
        return out;
    }

    // The instruction that falls through into `rip` (i.e. the one ending exactly at
    // rip that also *continues* into it), or 0 if there is none. Backward decode is
    // unreliable on x86, so we linear-decode the containing function forward and
    // keep the instruction whose end == rip.
    OriginalAddress fallthrough_predecessor(OriginalAddress rip) {
        OriginalAddress null_address {0};
        const auto fn = function_analysis_containing_address(rip);
        if (!fn || rip == fn->info.start) return null_address;      // entry has no fallthrough pred

        const size_t size = fn->info.end.get() - fn->info.start.get();
        std::vector<uint8_t> bytes(size);
        SIZE_T got = 0;
        if (!ReadProcessMemory(process_handle, reinterpret_cast<LPCVOID>(fn->info.start.get()),
                            bytes.data(), size, &got) || got != size)
            return null_address;

        OriginalAddress         prev_addr {0};
        ZydisDecodedInstruction prev_instr{};
        bool                    have_prev  = false;

        size_t off = 0;
        while (off < size) {
            const OriginalAddress addr {fn->info.start.get() + off};
            if (addr == rip) break;                       // reached anchor; prev_* precedes it
            
            // If the address was displaced the read the original buffer into bytes so Zydis can process it correctly
            auto [true_address, instruction_size] = get_true_instruction_address(addr);
            if (true_address.get() != addr.get()) {
                std::vector<uint8_t> bytes_displaced(instruction_size);
                ReadProcessMemory(process_handle, reinterpret_cast<void *>(true_address.get()), bytes_displaced.data(), instruction_size, nullptr);
                memcpy(bytes.data() + off, bytes_displaced.data(), instruction_size);
            }

            ZydisDecodedInstruction instr;
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(
                    &g_decoder, nullptr, bytes.data() + off, size - off, &instr)))
                return null_address;                                 // desync -- give up
            prev_addr = addr; 
            prev_instr = instr; 
            have_prev = true;
            off += instr.length;
        }

        if (!have_prev || prev_addr.get() + prev_instr.length != rip.get()) return null_address;

        // Only instructions that actually continue into rip count as fallthrough.
        // jmp / ret never fall through; a CALL reaches rip only via the callee's
        // return (an indirect transfer), so it is NOT the immediate predecessor.
        switch (prev_instr.meta.category) {
            case ZYDIS_CATEGORY_UNCOND_BR:   // jmp
            case ZYDIS_CATEGORY_RET:         // ret
            case ZYDIS_CATEGORY_CALL:        // predecessor is the callee's ret, not this
                return null_address;
            default:
                return prev_addr;            // cond-branch (not taken) or plain instruction
        }
    }

    // Build the candidate set + the counter value each must show.
    std::vector<PotentialPredecessor> get_predecessors_of_address(TrueAddress current_address, uint64_t current_branch_counter, const ReverseStepMode reverse_step_mode, const uintptr_t image_base) {
        const auto original_address = get_original_instruction_address(current_address);
        switch (reverse_step_mode) {
            case ReverseStepMode::Instruction: {
                return get_instruction_predecessors_of_address(original_address, current_branch_counter);
            }
            case ReverseStepMode::Line: {
                return get_line_predecessors_of_address(original_address, current_branch_counter, image_base);
            }
            case ReverseStepMode::Into: {
                return get_into_predecessors_of_address(original_address, current_branch_counter, image_base);
            }
        }
    }

    std::vector<PotentialPredecessor> get_instruction_predecessors_of_address(OriginalAddress original_address, uint64_t current_branch_counter) {
        const bool is_block_leader = is_instrumented_leader(original_address);
        const uint64_t predecessor_branch_counter = is_block_leader ? current_branch_counter - 1 : current_branch_counter;

        std::vector<PotentialPredecessor> out;

        if (is_block_leader) {
            // A leader can also be reached by any branch
            for (const auto& br : branch_sources_targeting(original_address))
                out.push_back({br.get(), predecessor_branch_counter, is_block_leader});
        }
        // Get the previous instruction that can "fallthrough" to this address
        const auto& fallthrough = fallthrough_predecessor(original_address);
        if (fallthrough.get()) {
            // If the fallthrough is displaced, get the correct displaced address for breakpoints
            const auto displaced = get_true_instruction_address(fallthrough);
            out.push_back({displaced.first.get(), predecessor_branch_counter, is_block_leader});

        }
        return out;
    }

    std::vector<PotentialPredecessor> get_line_predecessors_of_address(OriginalAddress original_address, uint64_t current_branch_counter, const uintptr_t image_base) {
        const auto& line_ranges = get_line_ranges_from_address(original_address, image_base);

        // if instruction tagged to a line range that contains block leader then line is a block leader (will this have issues with merged line ranges?) 
        // Since my functions target instruction walk back the easiest way then is to get the starting instruction (or leader instruction)
        // find the instructions that can target the starting instruction, then find the targeting's instruction line start
        bool is_block_leader = false;
        OriginalAddress line_start {0};
        for (const auto& line_range : line_ranges) {
            if (original_address >= line_range.start && original_address < line_range.end) {
                for (const auto& leader : g_instrumented_leaders) {
                    if (leader >= line_range.start && leader < line_range.end) {
                        is_block_leader = true;
                        line_start = OriginalAddress {leader};
                        break;
                    }
                }
                line_start = OriginalAddress {line_range.start};
            }
        }

        const uint64_t predecessor_branch_counter = is_block_leader ? current_branch_counter - 1 : current_branch_counter;
        std::vector<PotentialPredecessor> out;

        if (is_block_leader) {
            // A leader can also be reached by any branch
            for (const auto& br : branch_sources_targeting(line_start)) {
                const auto& br_ranges = get_line_ranges_from_address(br, image_base);
                for (const auto& br_range : br_ranges) {
                    // if displaced then get the real address
                    const auto true_address = get_true_instruction_address(br_range.start);
                    out.push_back({true_address.first.get(), predecessor_branch_counter, is_block_leader});
                }
            }
        }
        // Get the previous instruction that can "fallthrough" to this address
        const auto& fallthrough = fallthrough_predecessor(line_start);
        if (fallthrough.get()) {
            const auto& fallthrough_ranges = get_line_ranges_from_address(fallthrough, image_base);
            for (const auto fallthrough_range : fallthrough_ranges) {
                // If the fallthrough is displaced, get the correct displaced address for breakpoints
                const auto true_address = get_true_instruction_address(fallthrough_range.start);
                out.push_back({true_address.first.get(), predecessor_branch_counter, is_block_leader});
            }
        }
        return out;
    }

    std::vector<PotentialPredecessor> get_into_predecessors_of_address(OriginalAddress original_address, uint64_t current_branch_counter, const uintptr_t image_base) {
        std::vector<PotentialPredecessor> into_predecessors;
        const auto line_predecessors = get_line_predecessors_of_address(original_address, current_branch_counter, image_base);

        const auto function_analysis = function_analysis_containing_address(original_address);
        if (!function_analysis) return line_predecessors;

        for (const auto line_predecessor : line_predecessors) {
            const auto original_line_predecessor = get_original_instruction_address(TrueAddress {line_predecessor.true_address});
            const auto line_ranges = get_line_ranges_from_address(original_line_predecessor, image_base);
            if (line_ranges.size() == 0)  {
                into_predecessors.push_back(line_predecessor);
                continue;
            }

            // Check the predecessor line ranges for a call or indirect call instruction
            std::vector<BranchSite> predecessor_branches;
            for (const BranchSite& branch : function_analysis->branches) {
                if (!address_in_line_ranges(line_ranges, branch.address)) continue;
                
                // TODO: Handle the Indirect call case.
                if (branch.kind != BranchKind::Call) continue;
                predecessor_branches.push_back(branch);
            }

            // If we didn't find any calls then add the line predecessor back
            if (predecessor_branches.size() == 0) {
                into_predecessors.push_back(line_predecessor);
                continue;
            }

            // sort by last address first since we'll go in reverse order of intos (for chained expression)
            std::ranges::sort(predecessor_branches, 
                [](const BranchSite& branch_1, const BranchSite& branch_2) {
                    return branch_1.address > branch_2.address;
                }
            );

            bool call_site_found = false;
            // TODO:  clean this up, its getting heavily nested
            for (const auto branch : predecessor_branches) {
                // If we're already stopped at a call site I only want the call instructions before the current one
                if (branch.address >= original_address) continue;

                // If no source information for the call then continue until we find one
                const auto target_function_analysis = function_analysis_containing_address(branch.target);
                if (!target_function_analysis) continue;

                std::pair<TrueAddress, uint64_t> true_address {};
                for (const auto target_branch : target_function_analysis->branches) {
                    // Only need to check returns
                    if (target_branch.kind == BranchKind::Return) {
                        // get the line that contains the return
                        const auto target_line_ranges = get_line_ranges_from_address(target_branch.address, image_base);

                        for (const auto& target_line_range : target_line_ranges) {
                            if (target_branch.address >= target_line_range.start && target_branch.address < target_line_range.end) {
                                true_address = get_true_instruction_address(target_line_range.start);
                            }
                        }
                    }
                }

                if (true_address.first.get() == 0) continue;

                call_site_found = true;
                into_predecessors.push_back({true_address.first.get(), line_predecessor.branch_counter, false});
                
                // only want the first reverse branch that passes
                break;
            }

            // If no call sites have symbols then fallback to step line
            if (!call_site_found) {
                into_predecessors.push_back(line_predecessor);
            }
        }
        return into_predecessors;
    }
}

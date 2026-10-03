#pragma once

#include "base_breakpoint_plugin.h"
#include "branch_instrumentation.h"
#include <cstdint>
#include <set>
#include <vector>

namespace breakpoint_plugin {
    void init_reverse_step_analysis(HANDLE process,
                                    std::vector<FunctionAnalysis> analysis,
                                    std::set<OriginalAddress> instrumented_leaders);

    std::optional<FunctionAnalysis> function_analysis_containing_address(OriginalAddress addr);

    bool is_instrumented_leader(OriginalAddress rip);

    std::vector<OriginalAddress> branch_sources_targeting(OriginalAddress rip);

    OriginalAddress fallthrough_predecessor(OriginalAddress rip);

    std::vector<PotentialPredecessor>
    get_predecessors_of_address(TrueAddress current_address, uint64_t current_branch_counter, const ReverseStepMode reverse_step_mode, const uintptr_t image_base);

    std::vector<PotentialPredecessor> get_instruction_predecessors_of_address(OriginalAddress original_address, uint64_t current_branch_counter);
    std::vector<PotentialPredecessor> get_line_predecessors_of_address(OriginalAddress original_address, uint64_t current_branch_counter, const uintptr_t image_base);
    std::vector<PotentialPredecessor> get_into_predecessors_of_address(OriginalAddress original_address, uint64_t current_branch_counter, const uintptr_t image_base);
}
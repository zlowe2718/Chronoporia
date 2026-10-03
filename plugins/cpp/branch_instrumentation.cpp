#include "branch_instrumentation.h"
#include "base_breakpoint_plugin.h"
#include "pdb.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memoryapi.h>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace {
    using namespace breakpoint_plugin;

    HANDLE   process_;
    uint32_t tls_counter_offset_;
    uint32_t tls_target_offset_;
    uintptr_t region_base_ = 0;
    size_t    region_size_ = 0;
    ZydisDecoder decoder_{};
    constexpr uint64_t MAX_BRANCH_COUNTER = static_cast<uint64_t>(-1);

    // Staged trampolined bytes
    std::vector<uint8_t> code_;
    // This maps the original virtual address of the block instructions to the new jump instruction 
    // that will point it to the trampoline
    std::vector<std::pair<OriginalAddress, std::vector<uint8_t>>> patches_;
    std::unordered_map<OriginalAddress, std::pair<TrueAddress, uint64_t>> instruction_remap_;  // maps the original absolute instruction byte address to its new address in the trampoline and its instruction size

    // maps the relocated instruction address to its original virtual address.  This also includes all instructions in the "prologue" and "epilogue"
    std::unordered_map<TrueAddress, OriginalAddress> instruction_address_to_original_; 

    std::unordered_map<TrueAddress, uint16_t> prologue_rsp_fix_instructions;
    const std::unordered_map<uint8_t, uint16_t> tls_instruction_offsets_ 
    {
        {0x0, 0},
        {0x1, 8},
        {0x4, 8},
        {0x5, 8},
        {0xF, 8},
        {0x10, 16},
        {0x19, 16},
        {0x22, 16},
        {0x24, 16},
        {0x25, 16},
        {0x26, 8},
        {0x28, 8},
        {0x29, 8},
    };

    // TODO: add analysis so user can see what asm was skipped from branch counting
    std::vector<OriginalAddress> skipped_address_relocation_;

    // ---- branch classification --------------------------------------------

        // ---- branch classification --------------------------------------------

    bool classify(const ZydisDecodedInstruction& instr, const ZydisDecodedOperand* ops,
                  OriginalAddress addr, OriginalAddress& target_out, BranchKind& kind_out) {
        auto resolve_rel = [&](OriginalAddress& out) -> bool {
            for (int i = 0; i < instr.operand_count; ++i) {
                if (ops[i].type == ZYDIS_OPERAND_TYPE_IMMEDIATE && ops[i].imm.is_relative) {
                    ZyanU64 abs = 0;
                    if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&instr, &ops[i], addr.get(), &abs))) {
                        out = static_cast<OriginalAddress>(abs);
                        return true;
                    }
                }
            }
            return false;
        };

        OriginalAddress null_address {0};
        switch (instr.meta.category) {
            case ZYDIS_CATEGORY_COND_BR:
                kind_out = BranchKind::Conditional;
                return resolve_rel(target_out);
            case ZYDIS_CATEGORY_UNCOND_BR:
                if (resolve_rel(target_out)) { kind_out = BranchKind::UnconditionalJmp; return true; }
                kind_out = BranchKind::IndirectJump; target_out = null_address; return true;
            case ZYDIS_CATEGORY_CALL:
                if (resolve_rel(target_out)) { kind_out = BranchKind::Call; return true; }
                kind_out = BranchKind::IndirectCall; target_out = null_address; return true;
            case ZYDIS_CATEGORY_RET:
                kind_out = BranchKind::Return; target_out = null_address; return true;
            default:
                return false; // not a branch
        }
    }

    // TODO: update constant bytes to be constexpr and use Zydis for architecture safety?
    // Preserve flags cheaply across the counter's memory RMW, then bump the one
    // per-thread TLS-relative counter. Avoids pushfq/popfq (popfq is microcoded,
    // ~20-30 cyc) in favour of seto+lahf / sahf (~a few cyc). Trap on counter >=
    // target
    //
    //   push rax                          ; free a scratch reg (push/pop don't set flags)
    //   seto al                           ; AL = OF
    //   lahf                              ; AH = SF:ZF:x:AF:x:PF:x:CF
    //   add qword ptr gs:[tls], 1         ; the flag-clobbering counter increment
    //   push rcx                          ; free a register for gs -> gs cmp
    //   mov rcx, gs:[tls_2]
    //   cmp gs:[tls], rcx                 ; compare currrent branch counter hit the branch target
    //   jb skip                           ; if current branch < branch target continue
    //   int3                              ; bp
    //   skip:
    //   pop rcx
    //   add al, 0x7f                      ; 0x01+0x7f overflows -> restores OF into flags
    //   sahf                              ; restore SF/ZF/AF/PF/CF (OF left intact)
    //   pop rax
    void emit_tls_increment(std::vector<uint8_t>& out, uint32_t branch_tls_offset, uint32_t target_tls_offset) {
        // push rax   ;0x00
        out.push_back(0x50);
        // seto al    ;0x01
        out.push_back(0x0F); out.push_back(0x90); out.push_back(0xC0);
        // lahf       ;0x04
        out.push_back(0x9F);

        // add qword gs:[c], 1     ;0x05
        const uint8_t add_gs[] = {0x65, 0x48, 0x83, 0x04, 0x25};                        
        out.insert(out.end(), std::begin(add_gs), std::end(add_gs));
        out.insert(out.end(),
                   reinterpret_cast<const uint8_t*>(&branch_tls_offset),
                   reinterpret_cast<const uint8_t*>(&branch_tls_offset) + 4);
        out.push_back(0x01);

        // push rcx         ;0x0F
        out.push_back(0x51);
        
        // mov rcx, gs:[t]     ;0x10
        const uint8_t mov_gs[] = {0x65, 0x48, 0x8B, 0x0C, 0x25};                        
        out.insert(out.end(), std::begin(mov_gs), std::end(mov_gs));
        out.insert(out.end(),
                   reinterpret_cast<const uint8_t*>(&target_tls_offset),
                   reinterpret_cast<const uint8_t*>(&target_tls_offset) + 4);

        // cmp gs:[c], rcx     ;0x19
        const uint8_t cmp_gs_rcx[] = {0x65, 0x48, 0x39, 0x0C, 0x25};                    
        out.insert(out.end(), std::begin(cmp_gs_rcx), std::end(cmp_gs_rcx)); 
        out.insert(out.end(),
                   reinterpret_cast<const uint8_t*>(&branch_tls_offset),
                   reinterpret_cast<const uint8_t*>(&branch_tls_offset) + 4);

        // jb skip     ;0x22
        out.push_back(0x72); out.push_back(0x01);
        // int 3     ;0x24
        out.push_back(0xCC);

        // skip:
        // pop rcx     ;0x25
        out.push_back(0x59);
        // add al, 0x7f  (restore OF)     ;0x26                                              
        out.push_back(0x04); out.push_back(0x7F);
        // sahf          (restore rest)     ;0x28                                   
        out.push_back(0x9E);   
        //pop rax     ;0x29                                        
        out.push_back(0x58);
    }

    // FF 25 00000000 <qword absolute>  -> jmp [rip+0]; .qword dest
    void emit_abs_jmp(std::vector<uint8_t>& out, uintptr_t dest) {
        const uint8_t stub[] = {0xFF, 0x25, 0x00, 0x00, 0x00, 0x00};
        out.insert(out.end(), std::begin(stub), std::end(stub));
        out.insert(out.end(),
                   reinterpret_cast<const uint8_t*>(&dest),
                   reinterpret_cast<const uint8_t*>(&dest) + 8);
    }

    // E9 <rel32> from `at` to `dest`. Returns false if `dest` is out of the
    // ±2GB reach of a 32-bit relative jump -- the caller must skip the block
    // rather than let the displacement truncate into a wild jump.
    bool emit_rel_jmp(std::vector<uint8_t>& out, OriginalAddress at, uintptr_t dest) {
        int64_t rel = static_cast<int64_t>(dest) - static_cast<int64_t>(at.get() + 5);
        if (rel < INT32_MIN || rel > INT32_MAX) return false;
        int32_t rel32 = static_cast<int32_t>(rel);
        out.push_back(0xE9);
        out.insert(out.end(),
                   reinterpret_cast<const uint8_t*>(&rel32),
                   reinterpret_cast<const uint8_t*>(&rel32) + 4);
        return true;
    }

    // Commit `size` RWX bytes within ±2GB of `anchor` so E9 rel32 detours from
    // the instrumented module can always reach the stub region. VirtualAllocEx
    // with a null base can land arbitrarily far away, which is what put the
    // detours out of rel32 range. We search outward from the anchor (up first,
    // then down) for the nearest free block that fits.
    // TODO: For windows machine code is limited to ~2 GB (in practice about 1.8GB) and the entire dll is limited to 4GB  
    // (2GB machine code + 2GB static assets).  Probably need to account then for max size dlls and exes.  
    // Would have to ensure 2GB before the dlls are free and then move instructions to 
    // fit the 2GB distance limit 
    uintptr_t alloc_near(HANDLE process, uintptr_t image_base, size_t size) {
        SYSTEM_INFO si; GetSystemInfo(&si);
        const uint64_t gran  = si.dwAllocationGranularity;
        const uint64_t reach = 0x40000000ULL; // 1GB each side -- ample rel32 margin

        auto search = [&](bool up) -> uintptr_t {
            uintptr_t addr = image_base;
            MEMORY_BASIC_INFORMATION mbi;
            while (VirtualQueryEx(process, reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi))) {
                uintptr_t region = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
                if (mbi.State == MEM_FREE && mbi.RegionSize >= size) {
                    uintptr_t base = up
                        ? ((region + gran - 1) & ~(gran - 1))                 // lowest aligned slot
                        : ((region + mbi.RegionSize - size) & ~(gran - 1));   // highest aligned slot
                    if (base >= region && base + size <= region + mbi.RegionSize) {
                        uint64_t dist = base > image_base ? base - image_base : image_base - base;
                        if (dist > reach) return 0;
                        if (void* p = VirtualAllocEx(process, reinterpret_cast<void*>(base),
                                size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE))
                            return reinterpret_cast<uintptr_t>(p);
                    }
                }
                addr = up ? region + mbi.RegionSize : region - 1;
                if (!up && region <= gran) break;
                uint64_t dist = addr > image_base ? addr - image_base : image_base - addr;
                if (dist > reach) break;
            }
            return 0;
        };

        uintptr_t p = search(true);
        return p ? p : search(false);
    }

    // Copy `count` bytes of leader instructions into `out`, adjusting any 32-bit
    // RIP-relative displacement so it still resolves to the same absolute target
    // from the stub. Returns false if an insn won't decode or a fixed
    // displacement no longer fits in int32.
    // vector is probably overkill since the max instructions this can overwrite is 6(?) but is fine for now
    std::optional<std::vector<uint64_t>> relocate_leader(std::vector<uint8_t>& out, ZydisDecoder& decoder,
                         const uint8_t* src, size_t count,
                         OriginalAddress old_start, uintptr_t region_base) {
        size_t off = 0;
        std::vector<uint64_t> instruction_offsets;
        while (off < count) {
            ZydisDecodedInstruction instr {};
            ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT] {};
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(
                    &decoder, src + off, count - off, &instr, ops)))
                return std::nullopt;

            size_t insn_start = out.size();                  // offset within the stub
            out.insert(out.end(), src + off, src + off + instr.length);
            instruction_offsets.push_back(insn_start);

            bool rip_rel = false;
            for (int i = 0; i < instr.operand_count; ++i)
                if (ops[i].type == ZYDIS_OPERAND_TYPE_MEMORY &&
                    ops[i].mem.base == ZYDIS_REGISTER_RIP) { rip_rel = true; break; }

            if (rip_rel && instr.raw.disp.size == 32) {
                int32_t old_disp;
                std::memcpy(&old_disp, src + off + instr.raw.disp.offset, 4);
                // disp is relative to insn end, but the start-to-start delta is
                // identical since the length is unchanged.
                int64_t delta = static_cast<int64_t>(old_start.get() + off) -
                                static_cast<int64_t>(region_base + insn_start);
                int64_t nd = static_cast<int64_t>(old_disp) + delta;
                if (nd < INT32_MIN || nd > INT32_MAX) return std::nullopt;
                int32_t nd32 = static_cast<int32_t>(nd);
                std::memcpy(out.data() + insn_start + instr.raw.disp.offset, &nd32, 4);
            }
            off += instr.length;
        }
        return instruction_offsets;
    }
}

namespace breakpoint_plugin {

    // ---- analysis ---------------------------------------------------------

    std::vector<FunctionAnalysis> analyze_module(HANDLE process, uintptr_t image_base) {
        std::vector<FunctionAnalysis> result;

        ZydisDecoder decoder {};
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

        for (const FunctionRange& fn : get_all_functions(image_base)) {
            size_t size = fn.end.get() - fn.start.get();
            std::vector<uint8_t> bytes(size);
            SIZE_T read = 0;
            if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(fn.start.get()),
                                   bytes.data(), size, &read) || read != size)
                continue;

            FunctionAnalysis analysis{fn, {}, {}};

            size_t offset = 0;
            while (offset < size) {
                ZydisDecodedInstruction instr {};
                ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT] {};
                if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(
                        &decoder, bytes.data() + offset, size - offset, &instr, ops)))
                    break; // desync -- bail on this function

                OriginalAddress addr {fn.start.get() + offset};
                OriginalAddress target {0};
                BranchKind kind{};
                if (classify(instr, ops, addr, target, kind)) {
                    bool back_edge = target.get() != 0 && target >= fn.start &&
                                     target < addr && (kind == BranchKind::Conditional || kind == BranchKind::UnconditionalJmp);
                    analysis.branches.push_back({
                        OriginalAddress {addr}, 
                        OriginalAddress {addr.get() + instr.length},
                         target, 
                         instr.length, 
                         kind, 
                         back_edge
                    });
                    if (back_edge) {
                        analysis.loops.push_back({target, addr});
                    }
                }
                offset += instr.length;
            }
            result.push_back(std::move(analysis));
        }
        return result;
    }

    std::vector<BasicBlock> discover_blocks(const FunctionAnalysis& fn) {
        std::set<OriginalAddress> leaders;
        leaders.insert(fn.info.start);                 // function entry is always a leader

        for (const BranchSite& b : fn.branches) {
            // Calls are not block splits: control returns to the fallthrough, so
            // leader..call..next is still straight-line and runs exactly once per
            // block entry. A `ret`'s fallthrough is unreachable unless something
            // branches to it, in which case the target rule below already covers it.
            const bool splits = b.kind != BranchKind::Call &&
                                b.kind != BranchKind::IndirectCall &&
                                b.kind != BranchKind::Return;
            if (splits && b.next_address < fn.info.end) // instruction after a branch
                leaders.insert(b.next_address);
            if (b.target >= fn.info.start && b.target < fn.info.end) // in-function target
                leaders.insert(b.target);
        }

        std::vector<OriginalAddress> sorted(leaders.begin(), leaders.end());
        std::vector<BasicBlock> blocks;
        for (size_t i = 0; i < sorted.size(); ++i) {
            OriginalAddress start = sorted[i];
            OriginalAddress end   = (i + 1 < sorted.size()) ? sorted[i + 1] : fn.info.end;
            blocks.push_back({start, end});
        }
        return blocks;
    }

    // ---- instrumentation --------------------------------------------------

    bool create_instrumentation_region(const HANDLE process, const uint32_t tls_counter_offset, const uint32_t tls_target_offset, const uintptr_t image_base, const uint64_t region_size) {
        process_ = process;
        tls_counter_offset_ = tls_counter_offset;
        tls_target_offset_ = tls_target_offset;
        ZydisDecoderInit(&decoder_, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
        // Must sit within ±2GB of the instrumented module so E9 rel32 detours reach it.
        region_base_ = alloc_near(process_, image_base, region_size);
        region_size_ = region_size;
        return region_base_ != 0;
    }

    uintptr_t instrument_block(OriginalAddress block_start, OriginalAddress block_end, const uintptr_t image_base) {
        if (!region_base_) return 0;

        // Read the block's leader bytes. Blocks are short; 32 bytes covers the
        // >= 5 bytes an E9 detour needs in practice.
        uint8_t buf[32] = {};
        size_t want = std::min<size_t>(sizeof(buf), block_end.get() - block_start.get());
        SIZE_T got = 0;
        if (!ReadProcessMemory(process_, reinterpret_cast<LPCVOID>(block_start.get()), buf, want, &got)
            || got != want) {
            printf("ReadProcessMemory Failed 0x%llx want=%zu got=%llu err=%lu\n", block_start.get(), want, got, GetLastError());
            skipped_address_relocation_.push_back(block_start);
            return 0;
        }

        // Grow the patch to an instruction boundary >= 5 bytes, WITHOUT
        // displacing the block's terminating branch -- per-block instrumentation
        // leaves every branch in place. If we'd hit the branch (or block end)
        // first, the block is too small to patch this way.
        // TODO: fall back to a 1-byte int3 detour for such tiny blocks?
        // TODO: if 5 no-ops in a row then don't displace those instructions (track but then drop it most likely)
        size_t patch_len = 0;
        std::vector<std::pair<OriginalAddress, uint64_t>> displacing_instr_addr;
        while (patch_len < 5) {
            if (block_start.get() + patch_len >= block_end) return 0;   // block too small
            ZydisDecodedInstruction instr {};
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(
                    &decoder_, nullptr, buf + patch_len, want - patch_len, &instr)))
                    {
                        printf("  [decode fail] at +%zu\n", patch_len);
                        skipped_address_relocation_.push_back(block_start);
                        return 0;
                    }
            auto c = instr.meta.category;
            // TODO: can we improve this to not track calls?  rip relative calls would have to be fixed to absolute if too far when moved
            // we probably also have to fix the stack pointer so calls and returns aren't corrupted?
            if (c == ZYDIS_CATEGORY_COND_BR || c == ZYDIS_CATEGORY_UNCOND_BR ||
                c == ZYDIS_CATEGORY_CALL   || c == ZYDIS_CATEGORY_RET)
                {
                    printf("  [terminator-first] cat=%d at +%zu\n", (int)c, patch_len); 
                    skipped_address_relocation_.push_back(block_start);
                    return 0;
                }
            displacing_instr_addr.push_back({OriginalAddress {block_start.get() + patch_len}, instr.length});
            patch_len += instr.length;
        }

        // TODO: is the code_mark logic right?  I feel there's something weird here with region_base_ for remap addrs
        const size_t code_mark = code_.size();          // roll-back point on failure
        uintptr_t stub_addr = region_base_ + code_mark;

        // TODO: add each instruction to the instruction_address_to_original_ map so that we cnan track if we're in the "prologue" or "epilogue" of the 
        // instruction relocation

        // 1) global per-thread counter bump, flags preserved.
        emit_tls_increment(code_, tls_counter_offset_, tls_target_offset_);

        // 2) the displaced leader instructions, RIP-relative operands fixed up.
        auto instruction_offsets = relocate_leader(code_, decoder_, buf, patch_len, block_start, region_base_);
        if (!instruction_offsets.has_value()) {
            code_.resize(code_mark);
            skipped_address_relocation_.push_back(block_start);
            return 0;
        }
        // 3) jump back into the block, just past what we displaced.
        const uint64_t jmp_offset = code_.size(); 
        emit_abs_jmp(code_, block_start.get() + patch_len);

        // 4) overwrite the leader with E9 -> stub, NOP-pad any slack.
        std::vector<uint8_t> patch;
        if (!emit_rel_jmp(patch, block_start, stub_addr)) {
            code_.resize(code_mark);                     // stub unreachable; drop it
            skipped_address_relocation_.push_back(block_start);
            return 0;
        }
        patch.resize(patch_len, 0x90);
        patches_.emplace_back(block_start, std::move(patch));

        for (const auto [offset, bytes] : tls_instruction_offsets_) {
            TrueAddress new_addr {region_base_ + offset + code_mark};
            instruction_address_to_original_[new_addr] = displacing_instr_addr[0].first;
            // Only count instructions from (push rax, pop rax]
            if (offset >= 1) {
                prologue_rsp_fix_instructions.insert({new_addr, bytes});
            }
        }

        assert(displacing_instr_addr.size() == instruction_offsets->size());
        // remap the original address to the displaced addr for breakpoints
        for (uint32_t i=0; i < displacing_instr_addr.size(); i++) {
            auto [original_addr, instr_size] = displacing_instr_addr[i];
            TrueAddress new_addr {region_base_ + instruction_offsets->at(i)};
            instruction_remap_[original_addr] = {new_addr, instr_size};
            instruction_address_to_original_[new_addr] = original_addr;
        }

        TrueAddress new_addr {region_base_ + jmp_offset};
        instruction_address_to_original_[new_addr] = OriginalAddress {block_start.get() + patch_len};
        return stub_addr;
    }

    uintptr_t commit_instrumentation_region() {
        if (!region_base_) return 0;

        if (!WriteProcessMemory(process_, reinterpret_cast<void*>(region_base_),
                                code_.data(), code_.size(), nullptr))
            return 0;
        FlushInstructionCache(process_, reinterpret_cast<void*>(region_base_), code_.size());

        for (auto& [addr, bytes] : patches_) {
            DWORD old_protect = 0;
            VirtualProtectEx(process_, reinterpret_cast<void*>(addr.get()), bytes.size(),
                             PAGE_EXECUTE_READWRITE, &old_protect);
            WriteProcessMemory(process_, reinterpret_cast<void*>(addr.get()),
                               bytes.data(), bytes.size(), nullptr);
            VirtualProtectEx(process_, reinterpret_cast<void*>(addr.get()), bytes.size(),
                             old_protect, &old_protect);
            FlushInstructionCache(process_, reinterpret_cast<void*>(addr.get()), bytes.size());
        }
        return region_base_;
    }

    void destroy() {
        if (region_base_)
            VirtualFreeEx(process_, reinterpret_cast<void*>(region_base_), 0, MEM_RELEASE);
        region_base_ = 0;
    }

    std::pair<TrueAddress, uint64_t> get_true_instruction_address(const OriginalAddress original_address) {
        if (instruction_remap_.contains(original_address)) {
            return instruction_remap_[original_address];
        }
        return {TrueAddress {original_address.get()}, 0};
    }

    OriginalAddress get_original_instruction_address(const TrueAddress true_address) {
        if (instruction_address_to_original_.contains(true_address)) {
            return instruction_address_to_original_[true_address];
        }
        return OriginalAddress {true_address.get()};
    }

    uint16_t address_rsp_fix_bytes(const TrueAddress true_address) {
        if (!prologue_rsp_fix_instructions.contains(true_address)) return 0;

        return prologue_rsp_fix_instructions[true_address];
    }

    std::optional<std::vector<uint8_t>> get_original_bytes(const TrueAddress true_address) {
        // theoretical max buffer size would be 19 (4 + 15) 4 because we need 5 bytes for the jump and 15 is the max instruction size.  
        // allocate 32 bytes just to be safe
        std::vector<uint8_t> original_buffer(32);

        const OriginalAddress original_address = get_original_instruction_address(true_address);
        if (original_address.get() == true_address.get()) {
            return std::nullopt;
        }
    
        uint8_t instruction_count = 0;
        while (instruction_count < 5) {
            const auto next_true_address = instruction_remap_[original_address];
            instruction_count += next_true_address.second;
        }

        const uintptr_t first_true_instruction = instruction_remap_[original_address].first.get();
        ReadProcessMemory(process_, reinterpret_cast<void *>(first_true_instruction), original_buffer.data(), instruction_count, nullptr);
        original_buffer.resize(instruction_count);
        return original_buffer;
    }
}
#include "trampoline.h"
#include "thread_utils.h"
#include <vector>

namespace {
    std::vector<chronoporia::InstructionRegion> trampoline_region;
    std::map<uintptr_t, uintptr_t> address_to_trampoline_location;
    uintptr_t trampoline_address;
    uint64_t trampoline_size;
}

namespace chronoporia {

void CreateTrampolineRegion(uint64_t region_size) {
    trampoline_address = reinterpret_cast<uintptr_t>(
        VirtualAllocEx(
            globals::process_handle, nullptr, region_size,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE
        )
    );
    trampoline_size = region_size;
}

// We need to keep track of the final page adress so we don't copy over that or arm in our snapshot and page guard calls 
// TODO: later we can get fancy if we want to have regions of 8 bytes and regions of 16 bytes using allocators to manage where they should fit to pack them tightly.
// Could also group related calls together (calls called frequently or frequently in respect to another) and pack those into a 64 byte cache line

uintptr_t CreateTrampoline(const uintptr_t address) {
    InstructionRegion buffer;
    buffer.fill(0x00);

    ReadProcessMemory(globals::process_handle, reinterpret_cast<void *>(address), &buffer, sizeof(InstructionRegion), nullptr);

    ZydisDecoder decoder;
    ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

    ZydisDecodedInstruction instr;

    // Just decode the first instruction
    if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(
            &decoder, nullptr, &buffer, sizeof(InstructionRegion), &instr)))
        return reinterpret_cast<uintptr_t>(nullptr);

    uint64_t length = instr.length;
    uintptr_t return_address = address + length;
    uint8_t jmp_buffer[14] = {
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, // JMP [RIP+0]
    };
    // add the dynamic return address
    memcpy(jmp_buffer+6, &return_address, sizeof(uintptr_t));
    // overwrite the unneeded bytes with the jmp instruction
    std::copy(jmp_buffer, jmp_buffer + 14, buffer.begin() + length);

    uintptr_t new_trampoline_address = trampoline_address + trampoline_region.size() * sizeof(InstructionRegion);
    address_to_trampoline_location[address] = new_trampoline_address;
    
    //add to region after getting the new address so the new address is correct
    trampoline_region.push_back(std::move(buffer));
    return new_trampoline_address;
}

void FinalizeTrampolineRegion() {
    uint64_t region_size = trampoline_region.size() * sizeof(InstructionRegion);
    WriteProcessMemory(globals::process_handle, reinterpret_cast<void *>(trampoline_address), 
        trampoline_region.data(), region_size, nullptr);
    FlushInstructionCache(globals::process_handle, reinterpret_cast<void *>(trampoline_address), region_size);
}

void DestroyTrampolineRegion() {
    VirtualFreeEx(globals::process_handle, reinterpret_cast<void *>(trampoline_address), trampoline_size, MEM_RELEASE);
}

uintptr_t GetTrampolineAddress(const uintptr_t address) {
    return address_to_trampoline_location[address];
}

CONTEXT RedirectToTrampoline(const uintptr_t address, DWORD thread_id) {
    CONTEXT ctx;
    if (address_to_trampoline_location.contains(address)) {
        uintptr_t bp_trampoline_address = address_to_trampoline_location[address];
        ctx = SetRipAddress(thread_id, bp_trampoline_address);
    }
    return ctx;
}

}
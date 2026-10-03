#include "instruction_utils.h"
#include "Zycore/Types.h"
#include "Zydis/Decoder.h"
#include "Zydis/Formatter.h"
#include "globals.h"
#include <memoryapi.h>
#include <vector>


namespace chronoporia {
    // TODO: if breakpoint then get the actual value
    void print_instruction_at_address(const uintptr_t address) {
        ZydisDecoder decoder;
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);

        ZydisFormatter formatter;
        ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL);

        ZydisDecodedInstruction insn;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

        // get enough bytes for the full instruction
        std::vector<uint8_t> buffer(32);
        ReadProcessMemory(globals::process_handle, reinterpret_cast<void *>(address), buffer.data(), 32, nullptr);

        if (ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder, buffer.data(), 32, &insn, operands))) {
            char buffer[256];
            ZydisFormatterFormatInstruction(&formatter, &insn, operands, insn.operand_count_visible, buffer, sizeof(buffer), ZYDIS_RUNTIME_ADDRESS_NONE, ZYAN_NULL);
            printf("%s\n", buffer);
        }
    }
}
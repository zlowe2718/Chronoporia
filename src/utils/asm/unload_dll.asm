format MS64 COFF

section '.text' code readable executable

public _start
public shellcode_end

; ShellcodeParams layout:
;   +0   QWORD pfnFreeLibrary
;   +8  UnloadEntry[8], each 8 bytes:
;          +0  QWORD  hModule

MAX_ENTRIES     equ 8
sizeof_entry    equ 8
sizeof_header   equ 8                          
sizeof_params   equ sizeof_header + MAX_ENTRIES * sizeof_entry   

_start:
    sub rsp, 0x28
    call get_rip
get_rip:
    pop rax                     ; call pop trick to get dynamically supplied ShellcodeParams
    sub rax, 9                  ; 4 bytes for sub, 5 bytes for call
    sub rax, sizeof_params
    mov r12, rax                ; r12 = &ShellcodeParams

    mov r13, [r12]              ; r13 = pfnFreeLibrary

    xor rbx, rbx               ; index = 0

unload_next:
    cmp rbx, MAX_ENTRIES
    jge done

    imul rax, rbx, sizeof_entry
    lea rax, [r12 + rax + sizeof_header]   ; &entries[rbx]

    mov r14, [rax]              ; hModule
    test r14, r14
    jz done                 ; null handle = end sentinel

repeat_free:
    mov rcx, r14
    call r13                   ; FreeLibrary(hModule)
    test eax, eax
    jnz repeat_free            ; returned nonzero = still loaded, keep going

next_entry:
    inc rbx
    jmp unload_next

done:
    add rsp, 0x28
    xor rax, rax
    ret

shellcode_end:
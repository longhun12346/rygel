; SPDX-License-Identifier: MIT
; SPDX-FileCopyrightText: 2025 Niels Martignène <niels.martignene@protonmail.com>

; Forward
; ----------------------------

public ForwardCallG
public ForwardCallF
public ForwardCallD
public ForwardCallRG
public ForwardCallRF
public ForwardCallRD

.model flat, C
.code

; Copy the marshalled arguments (size bytes at sp) to a freshly reserved, 16-byte aligned area
; of the real thread stack, so that native code runs on the normal stack.
; ESP is aligned and the size rounded up to 16 bytes before probing, and every page is touched in order
; (including the last one), to let the guard page mechanism grow the stack without skipping a page.
; Small areas are copied with a loop (rep movsd has a high startup cost), large ones with rep movsd.
; EBP keeps the frame, ESI and EDI (non-volatile) are used for the copy and restored in the epilogue.
; The function pointer ends up in EAX, to save it through argument forwarding.
prologue macro
    local probe, done, small, copy, copied
    endbr32
    push ebp
    mov ebp, esp
    push esi
    push edi
    and esp, -16
    mov ecx, dword ptr [ebp+16]
    mov esi, dword ptr [ebp+12]
    lea eax, dword ptr [ecx+15]
    and eax, -16
probe:
    cmp eax, 4096
    jb done
    sub esp, 4096
    test dword ptr [esp], eax
    sub eax, 4096
    jmp probe
done:
    sub esp, eax
    test dword ptr [esp], eax
    mov edi, esp
    cmp ecx, 512
    jb small
    shr ecx, 2
    rep movsd
    jmp copied
small:
copy:
    sub ecx, 4
    jb copied
    mov edx, dword ptr [esi+ecx]
    mov dword ptr [edi+ecx], edx
    jmp copy
copied:
    mov eax, dword ptr [ebp+8]
endm

fastcall macro
    mov ecx, dword ptr [esp+0]
    mov edx, dword ptr [esp+4]
    add esp, 16
endm

; Call native function.
; Once done, restore normal stack pointer and return.
; The return value is passed back untouched.
epilogue macro
    call eax
    lea esp, dword ptr [ebp-8]
    pop edi
    pop esi
    pop ebp
    ret
endm

ForwardCallG proc
    prologue
    epilogue
ForwardCallG endp

ForwardCallF proc
    prologue
    epilogue
ForwardCallF endp

ForwardCallD proc
    prologue
    epilogue
ForwardCallD endp

ForwardCallRG proc
    prologue
    fastcall
    epilogue
ForwardCallRG endp

ForwardCallRF proc
    prologue
    fastcall
    epilogue
ForwardCallRF endp

ForwardCallRD proc
    prologue
    fastcall
    epilogue
ForwardCallRD endp

; Callbacks
; ----------------------------

extern RelayCallback : PROC

; Call the C function RelayCallback with the following arguments:
; static trampoline ID, the current stack pointer, a pointer to the stack arguments of this call,
; and a pointer to a struct that will contain the result registers.
; After the call, simply load these registers from the output struct.
; Depending on ABI, call convention and return value size, we need to issue ret <something>. Since ret
; only takes an immediate value, and I prefer not to branch, the return address is moved instead according
; to BackRegisters::ret_pop before ret is issued.
; We need to branch at the end to avoid x87 stack imbalance.
trampoline macro ID
    endbr32
    mov eax, ID
    jmp near ptr RelayTrampoline
endm

RelayTrampoline proc
    sub esp, 44
    mov dword ptr [esp+0], eax
    mov dword ptr [esp+4], esp
    mov dword ptr [esp+32], 0
    mov dword ptr [esp+36], 0
    call RelayCallback
    mov edx, dword ptr [esp+44]
    mov ecx, dword ptr [esp+36]
    mov dword ptr [esp+ecx+44], edx
    cmp dword ptr [esp+32], 1
    je l1
    cmp dword ptr [esp+32], 2
    je l2
l0:
    mov eax, dword ptr [esp+16]
    mov edx, dword ptr [esp+20]
    lea esp, dword ptr [esp+ecx+44]
    ret
l1:
    fld dword ptr [esp+24]
    lea esp, dword ptr [esp+ecx+44]
    ret
l2:
    fld qword ptr [esp+24]
    lea esp, dword ptr [esp+ecx+44]
    ret
RelayTrampoline endp

align 16
FindTrampolineStart proc
    call GetEIP
    add eax, 16
    and eax, 0FFFFFFF0h
    ret
FindTrampolineStart endp
align 16

include masm32.inc

FindTrampolineEnd proc
    call GetEIP
    ret
FindTrampolineEnd endp

GetEIP proc
    mov eax, dword ptr [esp]
    ret
GetEIP endp

end

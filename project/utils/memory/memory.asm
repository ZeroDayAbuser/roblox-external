.code

; quicker than NtReadVirtualMemory & NtWriteVirtualMemory marginally.

Clar_ReadVirtualMemory PROC
	mov r10, rcx
	mov eax, 63
	syscall
	ret
Clar_ReadVirtualMemory ENDP

Clar_WriteVirtualMemory PROC
	mov r10, rcx
	mov eax, 58
	syscall
	ret
Clar_WriteVirtualMemory ENDP

END
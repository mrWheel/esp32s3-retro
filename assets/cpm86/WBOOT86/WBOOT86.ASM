; WBOOT86: CP/M-86 warm-boot acceptance program (phase 86-09).
; Prints @BEGIN WBOOT, then calls BDOS function 0 (warm boot). The BIOS WBOOT
; must re-enter the CCP (prompt A> again) and must NOT return to the host
; "Select system" menu. tools/cpm86/cpm86HardwareTest.py checks both.
bits 16
cpu 8086
org 0
        times 100h db 0
start:
        mov ax,cs
        mov ds,ax
        mov es,ax
        cli
        mov ss,ax
        mov sp,stackTop
        sti
        cld
        mov dx,begin
        call printString
        ; BDOS function 0 = warm boot (the CCP re-entry path).
        mov cl,0
        int 0e0h
        ; WBOOT never returns; leave cleanly if the BIOS ever changes.
finish:
        xor cx,cx
        xor dx,dx
        int 0e0h
        retf
; Register-preserving string print through BDOS function 9 (DX->'$' string),
; same wrapper as TIMER86/BENCH86.
printString:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        push ds
        push es
        mov cl,9
        int 0e0h
        pop es
        pop ds
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        cld
        ret
begin db 13,10,'@BEGIN WBOOT',13,10,'$'
stackSpace times 512 db 0
stackTop:
align 16

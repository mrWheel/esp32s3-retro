; BAD86: CP/M-86 fault-recovery acceptance program (phase 86-09).
; Prints @BEGIN BAD86, then executes the invalid opcode FF /7 (tiny8086
; rejects FF /2../7). The firmware must stop the guest and return to the host
; "Select system" menu WITHOUT a host reset. tools/cpm86/cpm86HardwareTest.py
; checks the menu return and a clean relaunch.
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
        ; Invalid opcode: FF /7 (the print above already flushed).
        db 0ffh,3fh
        ; Never reached: the core raises the invalid-opcode fault.
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
begin db 13,10,'@BEGIN BAD86',13,10,'$'
stackSpace times 512 db 0
stackTop:
align 16

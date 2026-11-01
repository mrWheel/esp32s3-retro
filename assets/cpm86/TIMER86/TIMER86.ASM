; TIMER86: RETRO86 millisecond timer acceptance program (CP/M-86 phase 86-07).
; Reads monotonic milliseconds from the firmware RETRO86_TIMER_B1 byte IN ports
; (F0h capability B1h, F1h samples and returns the low byte, F2h-F4h the rest of
; the little-endian snapshot). Prints @BEGIN TIMER, samples t0, waits for one
; console key from the host script, samples t1 and prints DELTA=<t1-t0> MS.
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
        in al,0f0h
        cmp al,0b1h
        je timerReady
        mov dx,noTimer
        call printString
        jmp finish
timerReady:
        mov dx,begin
        call printString
        call readTime
        mov [startLow],ax
        mov [startHigh],dx
        mov cl,1
        int 0e0h
        call readTime
        sub ax,[startLow]
        sbb dx,[startHigh]
        mov dx,deltaText
        call printString
        call printUnsigned32
        mov dx,msText
        call printString
        mov dx,done
        call printString
finish:
        xor cx,cx
        xor dx,dx
        int 0e0h
        retf
; Samples one 32-bit millisecond value into DX:AX (little-endian ports).
readTime:
        in al,0f1h
        mov bl,al
        in al,0f2h
        mov bh,al
        in al,0f3h
        mov dl,al
        in al,0f4h
        mov dh,al
        mov ax,bx
        ret
; Full 32-bit decimal. String output preserves registers; the two word
; divisions avoid DIV overflow on large input values.
printUnsigned32:
        mov di,numberEnd
        mov bx,10
numberDigit:
        mov si,ax
        mov ax,dx
        xor dx,dx
        div bx
        mov bp,ax
        mov ax,si
        div bx
        add dl,'0'
        dec di
        mov [di],dl
        mov dx,bp
        mov cx,ax
        or cx,dx
        jnz numberDigit
        mov dx,di
        call printString
        ret
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
begin db 13,10,'@BEGIN TIMER',13,10,'$'
deltaText db 'DELTA=$'
msText db ' MS',13,10,'$'
done db '@DONE PASS',13,10,'$'
noTimer db 'Timer unavailable. Update firmware.',13,10,'$'
numberBuffer times 10 db 0
numberEnd db '$'
startLow dw 0
startHigh dw 0
stackSpace times 512 db 0
stackTop:
align 16

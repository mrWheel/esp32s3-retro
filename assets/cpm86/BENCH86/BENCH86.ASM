; BENCH86 v2: NASM, original 8086 instruction set, CP/M-86 8080 model.
; RETRO86 timer v1: F0 capability, F1..F4 latched milliseconds. M skips all ports.
bits 16
cpu 8086
%include "reference.inc"
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
        mov byte [scale],1
        xor cx,cx
        mov cl,[80h]
        mov si,81h
        mov byte [manual],0
        call skipSpaces
        jcxz argumentsDone
        lodsb
        dec cx
        cmp al,'M'
        je manualArgument
        cmp al,'m'
        je manualArgument
        cmp al,'1'
        jb badArguments
        cmp al,'9'
        ja badArguments
        sub al,'0'
        mov [scale],al
        jcxz argumentsDone
        cmp byte [si],' '
        jne badArguments
        call skipSpaces
        jcxz argumentsDone
        lodsb
        dec cx
        cmp al,'M'
        je manualArgument
        cmp al,'m'
        jne badArguments
manualArgument:
        mov byte [manual],1
        call skipSpaces
        jcxz argumentsDone
badArguments:
        mov dx,usage
        call printString
        jmp finish
skipSpaces:
        jcxz spacesDone
        cmp byte [si],' '
        jne spacesDone
        inc si
        dec cx
        jmp skipSpaces
spacesDone:
        ret
argumentsDone:
        mov al,[scale]
        add al,'0'
        mov [scaleText],al
        mov dx,banner
        call printString
        cmp byte [manual],1
        je timerReady
        in al,0f0h
        cmp al,0b1h
        je timerReady
        mov dx,noTimer
        call printString
        jmp finish
timerReady:
        jmp integerStart
integerFailed:
        mov dx,integerFail
        call printString
        jmp finish
integerStart:
        mov dx,integerBegin
        call printString
        xor ax,ax
        mov al,[scale]
        mov bx,32
        mul bx
        mov bp,ax
        call startTimer
integerBlock:
        mov ax,1234h
        mov bx,5678h
        mov cx,32768
integerLoop:
        add ax,bx
        xor ax,0a55ah
        rol ax,1
        add bx,17
        loop integerLoop
        cmp ax,4ed8h
        jne integerFailed
        cmp bx,0d678h
        jne integerFailed
        call dotCheck
        dec bp
        jnz integerBlock
        call stopTimer
        mov dx,integerPass
        call printString
        mov ax,integerModelMs
        call reportTime
        mov dx,sieveBegin
        call printString
        xor ax,ax
        mov al,[scale]
        mov bx,20
        mul bx
        mov bp,ax
        call startTimer
sieveRound:
        mov di,flags
        mov cx,8192
        mov al,1
        rep stosb
        mov bx,2
sievePrime:
        cmp byte [flags+bx],0
        je nextPrime
        mov ax,bx
        mul bx
        mov si,ax
markComposite:
        mov byte [flags+si],0
        add si,bx
        cmp si,8192
        jb markComposite
nextPrime:
        inc bx
        cmp bx,91
        jb sievePrime
        xor dx,dx
        mov si,2
countPrime:
        cmp byte [flags+si],0
        je nextCount
        inc dx
nextCount:
        inc si
        cmp si,8192
        jb countPrime
        cmp dx,1028
        jne sieveFailed
        call dotCheck
        dec bp
        jnz sieveRound
        call stopTimer
        mov dx,sievePass
        call printString
        mov ax,sieveModelMs
        call reportTime
        mov dx,done
        call printString
        jmp finish
sieveFailed:
        mov dx,sieveFail
        call printString
finish:
        xor cx,cx
        xor dx,dx
        int 0e0h
        retf
; Timer routines preserve workload registers. Unsigned subtraction permits
; a wrap at 2^32 ms, provided a single test takes less than 49.7 days.
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
startTimer:
        cmp byte [manual],1
        je startTimerDone
        push ax
        push bx
        push dx
        call readTime
        mov [startLow],ax
        mov [startHigh],dx
        mov [dotLow],ax
        mov [dotHigh],dx
        mov byte [dotsPrinted],0
        pop dx
        pop bx
        pop ax
startTimerDone:
        ret
stopTimer:
        cmp byte [manual],1
        je stopTimerDone
        push ax
        push bx
        push dx
        call readTime
        sub ax,[startLow]
        sbb dx,[startHigh]
        mov [elapsedLow],ax
        mov [elapsedHigh],dx
        pop dx
        pop bx
        pop ax
        call endDots
stopTimerDone:
        ret
; Prints one '.' when at least 5000 ms passed since the previous dot.
; Preserves all registers; does nothing in manual mode.
dotCheck:
        cmp byte [manual],1
        je dotDone
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        push ds
        push es
        call readTime
        sub ax,[dotLow]
        sbb dx,[dotHigh]
        jnz dotPrint
        cmp ax,5000
        jb dotRestore
dotPrint:
        call readTime
        mov [dotLow],ax
        mov [dotHigh],dx
        mov byte [dotsPrinted],1
        mov cl,2
        mov dl,'.'
        int 0e0h
dotRestore:
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
dotDone:
        ret
endDots:
        cmp byte [dotsPrinted],0
        je endDotsDone
        mov byte [dotsPrinted],0
        push dx
        mov dx,crlf
        call printString
        pop dx
endDotsDone:
        ret
; AX = scale-1 model milliseconds at 5 MHz (rounded).
reportTime:
        xor bx,bx
        mov bl,[scale]
        mul bx
        mov [modelMs],ax
        cmp byte [manual],1
        jne timedReport
        mov dx,manualText
        call printString
        ret
timedReport:
        mov dx,elapsedText
        call printString
        mov ax,[elapsedLow]
        mov dx,[elapsedHigh]
        call printUnsigned32
        mov dx,msText
        call printString
        mov dx,referenceText
        call printString
        xor dx,dx
        mov ax,[modelMs]
        call printUnsigned32
        mov dx,ref8Text
        call printString
        mov ax,[modelMs]
        mov bx,5
        mul bx
        add ax,4
        adc dx,0
        mov bx,8
        div bx
        xor dx,dx
        call printUnsigned32
        mov dx,ref10Text
        call printString
        mov ax,[modelMs]
        xor dx,dx
        add ax,1
        adc dx,0
        mov bx,2
        div bx
        xor dx,dx
        call printUnsigned32
        mov dx,msText
        call printString
        mov ax,[elapsedLow]
        or ax,[elapsedHigh]
        jnz scoreReport
        mov dx,tooShortText
        call printString
        ret
scoreReport:
        mov dx,scoreText
        call printString
        mov ax,[modelMs]
        mov bx,100
        mul bx
        call divideByElapsed
        call printUnsigned32
        mov dx,scoreEndText
        call printString
        cmp word [elapsedHigh],0
        jne reportDone
        cmp word [elapsedLow],1000
        jae reportDone
        mov dx,shortText
        call printString
reportDone:
        ret
; DX:AX / elapsed32 -> DX:AX. Full-width unsigned restoring division.
divideByElapsed:
        mov [quotientLow],ax
        mov [quotientHigh],dx
        xor bx,bx
        xor si,si
        mov cx,32
divideBit:
        shl word [quotientLow],1
        rcl word [quotientHigh],1
        rcl bx,1
        rcl si,1
        jc subtractDivisor
        cmp si,[elapsedHigh]
        jb nextDivideBit
        ja subtractDivisor
        cmp bx,[elapsedLow]
        jb nextDivideBit
subtractDivisor:
        sub bx,[elapsedLow]
        sbb si,[elapsedHigh]
        inc word [quotientLow]
nextDivideBit:
        loop divideBit
        mov ax,[quotientLow]
        mov dx,[quotientHigh]
        ret
; Full 32-bit decimal, including values above 65535. String output preserves
; registers; the two word divisions avoid DIV overflow on large input values.
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
crlf db 13,10,'$'
banner db 13,10,'BENCH86 v2 SCALE='
scaleText db '1',13,10,'$'
integerBegin db '@BEGIN INTEGER',13,10,'$'
integerPass db '@END INTEGER PASS AX=4ED8 BX=D678',13,10,'$'
integerFail db '@END INTEGER FAIL',13,10,'$'
sieveBegin db '@BEGIN SIEVE',13,10,'$'
sievePass db '@END SIEVE PASS PRIMES=1028',13,10,'$'
sieveFail db '@END SIEVE FAIL',13,10,'$'
done db '@DONE PASS',13,10,'$'
usage db 'Usage: BENCH86 [1-9] [M]',13,10,'$'
noTimer db 'Timer unavailable. Update firmware or use BENCH86 M.',13,10,'$'
elapsedText db '  Elapsed: $'
msText db ' ms',13,10,'$'
referenceText db '  8086 MODEL ONLY (not measured): 5MHz=$'
ref8Text db ' ms, 8MHz=$'
ref10Text db ' ms, 10MHz=$'
scoreText db '  Index vs 5MHz MODEL: $'
scoreEndText db ' (100=model, higher=faster; theoretical)',13,10,'$'
manualText db '  Manual mode: time BEGIN to END externally.',13,10,'$'
tooShortText db '  Index unavailable: elapsed <1 ms. Increase scale.',13,10,'$'
shortText db '  Short run: increase scale for better timing accuracy.',13,10,'$'
numberBuffer times 10 db 0
numberEnd db '$'
manual db 0
dotsPrinted db 0
dotLow dw 0
dotHigh dw 0
startLow dw 0
startHigh dw 0
elapsedLow dw 0
elapsedHigh dw 0
modelMs dw 0
quotientLow dw 0
quotientHigh dw 0
scale db 1
align 2
flags times 8192 db 0
stackSpace times 512 db 0
stackTop:
align 16

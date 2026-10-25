; BENCH2ASM v2: 6502 machine-code benchmark for Apple II / ProDOS (BRUN BENCH2ASM).
; Same workloads and report layout as BENCH80/BENCH86, using APPLE2_TIMER_C1 at $C0A0-$C0A4.
; Build: python3 build.py (ca65 + ld65, load address $6000).
; Zero page use is limited to the documented free locations $06-$09 and $1A-$1F.

.include "reference.inc"

COUT    = $FDED
RDKEY   = $FD0C
timerCap = $C0A0
timerLow = $C0A1
flags   = $4000
flagsEndPage = $60

axl     = $1A
axh     = $1B
bxl     = $1C
bxh     = $1D
cntl    = $1E
cnth    = $1F
ptr     = $06
textPtr = $08

.macro sub32 dest, minuend, subtrahend
        sec
        .repeat 4, i
        lda minuend+i
        sbc subtrahend+i
        sta dest+i
        .endrepeat
.endmacro

.macro add32 dest, source
        clc
        .repeat 4, i
        lda dest+i
        adc source+i
        sta dest+i
        .endrepeat
.endmacro

.macro printText txt
        lda #<txt
        ldy #>txt
        jsr printString
.endmacro

.segment "CODE"
start:
        lda timerCap
        cmp #$C1
        beq askScale
        printText noTimer
        rts
askScale:
        printText scalePrompt
        jsr RDKEY
        and #$7F
        cmp #'1'
        bcc askScale
        cmp #'9'+1
        bcs askScale
        pha
        ora #$80
        jsr COUT
        lda #$8D
        jsr COUT
        pla
        sec
        sbc #'0'
        sta scale
        clc
        adc #'0'
        sta scaleText
        printText banner

; INTEGER: 2 blocks of 32768 iterations per scale step.
        printText integerBegin
        lda scale
        asl
        sta blocks
        jsr startTimer
integerBlock:
        lda #$34
        sta axl
        lda #$12
        sta axh
        lda #$78
        sta bxl
        lda #$56
        sta bxh
        lda #$00
        sta cntl
        lda #$80
        sta cnth
integerLoop:
        clc
        lda axl
        adc bxl
        sta axl
        lda axh
        adc bxh
        sta axh
        lda axl
        eor #$5A
        sta axl
        lda axh
        eor #$A5
        sta axh
        asl axl
        rol axh
        bcc integerNoCarry
        inc axl
integerNoCarry:
        clc
        lda bxl
        adc #17
        sta bxl
        bcc integerNoHigh
        inc bxh
integerNoHigh:
        lda cntl
        bne integerLow
        dec cnth
integerLow:
        dec cntl
        lda cntl
        ora cnth
        bne integerLoop
        lda axh
        cmp #$4E
        bne integerFailed
        lda axl
        cmp #$D8
        bne integerFailed
        lda bxh
        cmp #$D6
        bne integerFailed
        lda bxl
        cmp #$78
        bne integerFailed
        jsr dotCheck
        dec blocks
        beq integerFinished
        jmp integerBlock
integerFinished:
        jsr stopTimer
        jsr endDots
        printText integerPass
        lda #<integerModelMs
        sta modelBase
        lda #>integerModelMs
        sta modelBase+1
        jsr reportTime
        jmp sieveStart
integerFailed:
        jsr endDots
        printText integerFail
        rts

; SIEVE: 6 rounds per scale step over 8192 flags at $4000.
sieveStart:
        printText sieveBegin
        lda scale
        asl
        sta blocks
        asl
        clc
        adc blocks
        sta blocks
        jsr startTimer
sieveRound:
        lda #<flags
        sta ptr
        lda #.hibyte(flags)
        sta ptr+1
        ldx #(flagsEndPage-.hibyte(flags))
        ldy #0
        lda #1
fillLoop:
        sta (ptr),y
        iny
        bne fillLoop
        inc ptr+1
        dex
        bne fillLoop
        lda #2
        sta prime
        lda #4
        sta squareLow
        lda #0
        sta squareHigh
primeLoop:
        ldy prime
        lda flags,y
        beq nextPrime
        lda squareLow
        sta ptr
        lda squareHigh
        clc
        adc #.hibyte(flags)
        sta ptr+1
        ldy #0
markLoop:
        lda #0
        sta (ptr),y
        clc
        lda ptr
        adc prime
        sta ptr
        bcc markNoCarry
        inc ptr+1
markNoCarry:
        lda ptr+1
        cmp #flagsEndPage
        bcc markLoop
nextPrime:
        clc
        lda squareLow
        adc prime
        sta squareLow
        lda squareHigh
        adc #0
        sta squareHigh
        clc
        lda squareLow
        adc prime
        sta squareLow
        lda squareHigh
        adc #0
        sta squareHigh
        inc squareLow
        bne squareDone
        inc squareHigh
squareDone:
        inc prime
        lda prime
        cmp #91
        bcc primeLoop
        lda #0
        sta primesLow
        sta primesHigh
        sta ptr
        lda #.hibyte(flags)
        sta ptr+1
        ldy #2
countLoop:
        lda (ptr),y
        beq countNext
        inc primesLow
        bne countNext
        inc primesHigh
countNext:
        iny
        bne countLoop
        inc ptr+1
        lda ptr+1
        cmp #flagsEndPage
        bne countLoop
        lda primesHigh
        cmp #.hibyte(1028)
        bne sieveFailed
        lda primesLow
        cmp #.lobyte(1028)
        bne sieveFailed
        jsr dotCheck
        dec blocks
        beq sieveFinished
        jmp sieveRound
sieveFinished:
        jsr stopTimer
        jsr endDots
        printText sievePass
        lda #<sieveModelMs
        sta modelBase
        lda #>sieveModelMs
        sta modelBase+1
        jsr reportTime
        printText done
        rts
sieveFailed:
        jsr endDots
        printText sieveFail
        rts

; Reads the timer into startTime and restarts the 5-second progress dots.
startTimer:
        jsr readNow
        ldx #3
:       lda dotNow,x
        sta startTime,x
        sta dotLast,x
        dex
        bpl :-
        lda #0
        sta dotsPrinted
        rts

; $C0A1 latches the snapshot, so it is read first.
readNow:
        lda timerLow
        sta dotNow
        lda timerLow+1
        sta dotNow+1
        lda timerLow+2
        sta dotNow+2
        lda timerLow+3
        sta dotNow+3
        rts

stopTimer:
        jsr readNow
        sub32 elapsed, dotNow, startTime
        rts

; Prints one '.' whenever at least 5000 ms passed since the previous dot.
dotCheck:
        jsr readNow
        sub32 dotDiff, dotNow, dotLast
        lda dotDiff+2
        ora dotDiff+3
        bne printDot
        lda dotDiff+1
        cmp #.hibyte(5000)
        bcc dotDone
        bne printDot
        lda dotDiff
        cmp #.lobyte(5000)
        bcc dotDone
printDot:
        ldx #3
:       lda dotNow,x
        sta dotLast,x
        dex
        bpl :-
        lda #'.'+$80
        jsr COUT
        lda #1
        sta dotsPrinted
dotDone:
        rts

endDots:
        lda dotsPrinted
        beq :+
        lda #0
        sta dotsPrinted
        lda #$8D
        jsr COUT
:       rts

; modelBase = scale-1 model milliseconds at 1.023 MHz.
reportTime:
        lda #0
        sta modelBase+2
        sta modelBase+3
        sta modelMs
        sta modelMs+1
        sta modelMs+2
        sta modelMs+3
        ldx scale
scaleLoop:
        add32 modelMs, modelBase
        dex
        bne scaleLoop
        printText elapsedText
        ldx #<elapsed
        ldy #>elapsed
        jsr printNumber
        printText msText
        printText referenceText
        ldx #<modelMs
        ldy #>modelMs
        jsr printNumber
        printText msText
        lda elapsed
        ora elapsed+1
        ora elapsed+2
        ora elapsed+3
        bne scoreReport
        printText tooShortText
        rts
scoreReport:
        printText scoreText
        lda #0
        ldx #3
:       sta scoreTotal,x
        dex
        bpl :-
        ldx #100
scoreMultiply:
        stx multiplyCount
        add32 scoreTotal, modelMs
        ldx multiplyCount
        dex
        bne scoreMultiply
        jsr divideByElapsed
        ldx #<scoreTotal
        ldy #>scoreTotal
        jsr printNumber
        printText scoreEndText
        lda elapsed+2
        ora elapsed+3
        bne reportDone
        lda elapsed+1
        cmp #.hibyte(1000)
        bcc shortRun
        bne reportDone
        lda elapsed
        cmp #.lobyte(1000)
        bcs reportDone
shortRun:
        printText shortText
reportDone:
        rts

; scoreTotal = scoreTotal / elapsed (unsigned 32-bit restoring division).
divideByElapsed:
        lda #0
        ldx #3
:       sta remainder,x
        dex
        bpl :-
        ldx #32
divideBit:
        asl scoreTotal
        rol scoreTotal+1
        rol scoreTotal+2
        rol scoreTotal+3
        rol remainder
        rol remainder+1
        rol remainder+2
        rol remainder+3
        bcs divideSubtract
        sec
        lda remainder
        sbc elapsed
        lda remainder+1
        sbc elapsed+1
        lda remainder+2
        sbc elapsed+2
        lda remainder+3
        sbc elapsed+3
        bcc divideNext
divideSubtract:
        sub32 remainder, remainder, elapsed
        inc scoreTotal
divideNext:
        dex
        bne divideBit
        rts

; X/Y = low/high address of a 32-bit value; prints it as unsigned decimal.
printNumber:
        stx textPtr
        sty textPtr+1
        ldy #3
:       lda (textPtr),y
        sta numberWork,y
        dey
        bpl :-
        ldy #10
digitLoop:
        lda #0
        ldx #32
divideTen:
        asl numberWork
        rol numberWork+1
        rol numberWork+2
        rol numberWork+3
        rol
        cmp #10
        bcc noSubtract
        sbc #10
        inc numberWork
noSubtract:
        dex
        bne divideTen
        ora #'0'+$80
        dey
        sta digits,y
        lda numberWork
        ora numberWork+1
        ora numberWork+2
        ora numberWork+3
        bne digitLoop
printDigits:
        lda digits,y
        jsr COUT
        iny
        cpy #10
        bcc printDigits
        rts

; A/Y = low/high address of a zero-terminated ASCII text.
printString:
        sta textPtr
        sty textPtr+1
        ldy #0
stringLoop:
        lda (textPtr),y
        beq stringEnd
        cmp #'a'
        bcc stringOut
        cmp #'z'+1
        bcs stringOut
        and #$DF
stringOut:
        ora #$80
        jsr COUT
        iny
        bne stringLoop
        inc textPtr+1
        bne stringLoop
stringEnd:
        rts

.segment "RODATA"
scalePrompt:  .byte "SCALE (1-9)? ",0
banner:       .byte 13,"BENCH2ASM v2 SCALE="
scaleText:    .byte "1",13,0
integerBegin: .byte "@BEGIN INTEGER",13,0
integerPass:  .byte "@END INTEGER PASS AX=4ED8 BX=D678",13,0
integerFail:  .byte "@END INTEGER FAIL",13,0
sieveBegin:   .byte "@BEGIN SIEVE",13,0
sievePass:    .byte "@END SIEVE PASS PRIMES=1028",13,0
sieveFail:    .byte "@END SIEVE FAIL",13,0
done:         .byte "@DONE PASS",13,0
noTimer:      .byte "Timer unavailable. Update firmware.",13,0
elapsedText:  .byte "  Elapsed: ",0
msText:       .byte " ms",13,0
referenceText: .byte "  6502 MODEL ONLY (not measured): 1.023MHz=",0
scoreText:    .byte "  Index vs 1.023MHz MODEL: ",0
scoreEndText: .byte " (100=model, higher=faster; theoretical)",13,0
tooShortText: .byte "  Index unavailable: elapsed <1 ms. Increase scale.",13,0
shortText:    .byte "  Short run: increase scale for better timing accuracy.",13,0

.segment "BSS"
scale:        .res 1
blocks:       .res 1
dotsPrinted:  .res 1
multiplyCount: .res 1
prime:        .res 1
squareLow:    .res 1
squareHigh:   .res 1
primesLow:    .res 1
primesHigh:   .res 1
startTime:    .res 4
elapsed:      .res 4
dotLast:      .res 4
dotNow:       .res 4
dotDiff:      .res 4
modelBase:    .res 4
modelMs:      .res 4
scoreTotal:   .res 4
remainder:    .res 4
numberWork:   .res 4
digits:       .res 10

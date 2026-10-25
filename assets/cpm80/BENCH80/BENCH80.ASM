; BENCH80 v2: CP/M-80 benchmark (Z80 instructions), same report layout as BENCH86.
; Uses RETRO80_TIMER_B1: IN F0 capability (B1), IN F1 latches a 32-bit
; millisecond count and returns bits 0-7, IN F2/F3/F4 return bits 8-31.
; Usage: BENCH80 [1-9]   (scale, default 1)
; Build: python3 build.py (z80asm; runs reference_model.py first).
        include "reference.inc"
        org 0100h
flags:  equ 4000h
flagsEnd: equ 6000h
stackTop: equ 4000h
start:
        ld sp,stackTop
        ld a,1
        ld (scale),a
        ld hl,0081h
        ld a,(0080h)
        or a
        jr z,argumentsDone
        ld b,a
skipSpaces:
        ld a,(hl)
        cp ' '
        jr nz,haveArgument
        inc hl
        djnz skipSpaces
        jr argumentsDone
haveArgument:
        cp '1'
        jr c,badArguments
        cp '9'+1
        jr nc,badArguments
        sub '0'
        ld (scale),a
        jr argumentsDone
badArguments:
        ld de,usage
        call printString
        jp 0
argumentsDone:
        ld a,(scale)
        add a,'0'
        ld (scaleText),a
        ld de,banner
        call printString
        in a,(0F0h)
        cp 0B1h
        jr z,timerReady
        ld de,noTimer
        call printString
        jp 0

timerReady:
        ld de,integerBegin
        call printString
        ld a,(scale)
        add a,a
        add a,a
        ld (blocks),a
        call startTimer
integerBlock:
        ld hl,1234h
        ld de,5678h
        ld bc,32768
integerLoop:
        add hl,de
        ld a,l
        xor 05Ah
        ld l,a
        ld a,h
        xor 0A5h
        ld h,a
        add hl,hl
        jr nc,integerNoCarry
        inc l
integerNoCarry:
        ld a,e
        add a,17
        ld e,a
        jr nc,integerNoHigh
        inc d
integerNoHigh:
        dec bc
        ld a,b
        or c
        jp nz,integerLoop
        ld a,h
        cp 04Eh
        jr nz,integerFailed
        ld a,l
        cp 0D8h
        jr nz,integerFailed
        ld a,d
        cp 0D6h
        jr nz,integerFailed
        ld a,e
        cp 078h
        jr nz,integerFailed
        call dotCheck
        ld hl,blocks
        dec (hl)
        jp nz,integerBlock
        call stopTimer
        call endDots
        ld de,integerPass
        call printString
        ld hl,integerModelMs
        call reportTime
        jr sieveStart
integerFailed:
        call endDots
        ld de,integerFail
        call printString
        jp 0

sieveStart:
        ld de,sieveBegin
        call printString
        ld a,(scale)
        add a,a
        add a,a
        add a,a
        ld b,a
        ld a,(scale)
        add a,a
        add a,b
        ld (blocks),a
        call startTimer
sieveRound:
        ld hl,flags
        ld de,flags+1
        ld bc,8191
        ld (hl),1
        ldir
        ld de,2
        ld bc,4
sievePrime:
        ld hl,flags
        add hl,de
        ld a,(hl)
        or a
        jr z,nextPrime
        ld hl,flags
        add hl,bc
markComposite:
        ld (hl),0
        add hl,de
        ld a,h
        cp flagsEnd/256
        jr c,markComposite
nextPrime:
        ld h,b
        ld l,c
        add hl,de
        add hl,de
        inc hl
        ld b,h
        ld c,l
        inc de
        ld a,e
        cp 91
        jr c,sievePrime
        ld hl,flags+2
        ld de,0
        ld bc,8190
countPrime:
        ld a,(hl)
        or a
        jr z,nextCount
        inc de
nextCount:
        inc hl
        dec bc
        ld a,b
        or c
        jp nz,countPrime
        ld a,d
        cp 04h
        jr nz,sieveFailed
        ld a,e
        cp 04h
        jr nz,sieveFailed
        call dotCheck
        ld hl,blocks
        dec (hl)
        jp nz,sieveRound
        call stopTimer
        call endDots
        ld de,sievePass
        call printString
        ld hl,sieveModelMs
        call reportTime
        ld de,done
        call printString
        jp 0
sieveFailed:
        call endDots
        ld de,sieveFail
        call printString
        jp 0

; Reads the timer into startTime and restarts the 5-second progress dots.
startTimer:
        ld hl,startTime
        call readTimer
        ld hl,(startTime)
        ld (dotLast),hl
        ld hl,(startTime+2)
        ld (dotLast+2),hl
        xor a
        ld (dotsPrinted),a
        ret

stopTimer:
        ld hl,endTime
        call readTimer
        ld hl,startTime
        ld de,endTime
        ld bc,elapsed
        jp subtract32

; HL = four-byte destination; ports F1..F4 are read in order.
readTimer:
        in a,(0F1h)
        ld (hl),a
        inc hl
        in a,(0F2h)
        ld (hl),a
        inc hl
        in a,(0F3h)
        ld (hl),a
        inc hl
        in a,(0F4h)
        ld (hl),a
        ret

; (BC) = (DE) - (HL), little-endian 32-bit; carry set on borrow.
subtract32:
        or a
        ld a,(de)
        sbc a,(hl)
        ld (bc),a
        inc hl
        inc de
        inc bc
        ld a,(de)
        sbc a,(hl)
        ld (bc),a
        inc hl
        inc de
        inc bc
        ld a,(de)
        sbc a,(hl)
        ld (bc),a
        inc hl
        inc de
        inc bc
        ld a,(de)
        sbc a,(hl)
        ld (bc),a
        ret

; (DE) = (DE) + (HL), little-endian 32-bit. Clobbers A, B, DE, HL.
add32:
        or a
        ld b,4
addLoop:
        ld a,(de)
        adc a,(hl)
        ld (de),a
        inc hl
        inc de
        djnz addLoop
        ret

; Prints one '.' whenever at least 5000 ms passed since the previous dot.
; All registers are preserved; called once per workload block, not per loop.
dotCheck:
        push af
        push bc
        push de
        push hl
        ld hl,dotNow
        call readTimer
        ld hl,dotLast
        ld de,dotNow
        ld bc,dotDiff
        call subtract32
        ld hl,dotDiff+2
        ld a,(hl)
        inc hl
        or (hl)
        jr nz,printDot
        ld hl,(dotDiff)
        ld de,5000
        or a
        sbc hl,de
        jr c,dotDone
printDot:
        ld hl,(dotNow)
        ld (dotLast),hl
        ld hl,(dotNow+2)
        ld (dotLast+2),hl
        ld e,'.'
        ld c,2
        call 5
        ld a,1
        ld (dotsPrinted),a
dotDone:
        pop hl
        pop de
        pop bc
        pop af
        ret

; Ends the dot line when dots were printed.
endDots:
        ld a,(dotsPrinted)
        or a
        ret z
        xor a
        ld (dotsPrinted),a
        ld de,crlf
        jp printString

; HL = scale-1 model milliseconds at 1 MHz (16-bit value).
reportTime:
        ld (modelBase),hl
        ld hl,0
        ld (modelBase+2),hl
        ld (modelMs),hl
        ld (modelMs+2),hl
        ld a,(scale)
        ld c,a
scaleLoop:
        ld hl,modelBase
        ld de,modelMs
        call add32
        dec c
        jr nz,scaleLoop
        ld de,elapsedText
        call printString
        ld hl,elapsed
        call printNumber
        ld de,msText
        call printString
        ld de,referenceText
        call printString
        ld hl,modelMs
        call printNumber
        ld de,ref2Text
        call printString
        ld hl,modelMs
        ld b,1
        call printShifted
        ld de,ref4Text
        call printString
        ld hl,modelMs
        ld b,2
        call printShifted
        ld de,msText
        call printString
        ld hl,(elapsed)
        ld a,h
        or l
        ld hl,(elapsed+2)
        or h
        or l
        jr nz,scoreReport
        ld de,tooShortText
        jp printString
scoreReport:
        ld de,scoreText
        call printString
        ld hl,0
        ld (scoreTotal),hl
        ld (scoreTotal+2),hl
        ld b,100
scoreMultiply:
        push bc
        ld hl,modelMs
        ld de,scoreTotal
        call add32
        pop bc
        djnz scoreMultiply
        call divideByElapsed
        ld hl,scoreTotal
        call printNumber
        ld de,scoreEndText
        call printString
        ld hl,(elapsed+2)
        ld a,h
        or l
        ret nz
        ld hl,(elapsed)
        ld de,1000
        or a
        sbc hl,de
        ret nc
        ld de,shortText
        jp printString

; Prints (value at HL + 2^(B-1)) >> B for B = 1 or 2.
printShifted:
        ld a,b
        ld (shiftCount),a
        ld (shiftAdd),a
        xor a
        ld (shiftAdd+1),a
        ld (shiftAdd+2),a
        ld (shiftAdd+3),a
        ld de,shiftWork
        ld bc,4
        ldir
        ld hl,shiftAdd
        ld de,shiftWork
        call add32
shiftLoop:
        or a
        ld hl,shiftWork+3
        rr (hl)
        dec hl
        rr (hl)
        dec hl
        rr (hl)
        dec hl
        rr (hl)
        ld hl,shiftCount
        dec (hl)
        jr nz,shiftLoop
        ld hl,shiftWork
        jp printNumber

; scoreTotal = scoreTotal / elapsed (unsigned 32-bit restoring division).
divideByElapsed:
        ld hl,0
        ld (remainder),hl
        ld (remainder+2),hl
        ld b,32
divideBit:
        push bc
        ld hl,scoreTotal
        or a
        rl (hl)
        inc hl
        rl (hl)
        inc hl
        rl (hl)
        inc hl
        rl (hl)
        ld hl,remainder
        rl (hl)
        inc hl
        rl (hl)
        inc hl
        rl (hl)
        inc hl
        rl (hl)
        jr c,divideSubtract
        ld hl,elapsed
        ld de,remainder
        ld bc,compareWork
        call subtract32
        jr c,divideNext
        jr divideCommit
divideSubtract:
        ld hl,elapsed
        ld de,remainder
        ld bc,compareWork
        call subtract32
divideCommit:
        ld hl,compareWork
        ld de,remainder
        ld bc,4
        ldir
        ld hl,scoreTotal
        set 0,(hl)
divideNext:
        pop bc
        djnz divideBit
        ret

; HL = address of a little-endian 32-bit value; prints it as unsigned decimal.
printNumber:
        ld de,numberWork
        ld bc,4
        ldir
        ld hl,digitsEnd
digitLoop:
        push hl
        ld b,32
        xor a
divideLoop:
        ld hl,numberWork
        or a
        rl (hl)
        inc hl
        rl (hl)
        inc hl
        rl (hl)
        inc hl
        rl (hl)
        rla
        cp 10
        jr c,noSubtract
        sub 10
        ld hl,numberWork
        set 0,(hl)
noSubtract:
        djnz divideLoop
        pop hl
        add a,'0'
        dec hl
        ld (hl),a
        push hl
        ld hl,numberWork
        ld a,(hl)
        inc hl
        or (hl)
        inc hl
        or (hl)
        inc hl
        or (hl)
        pop hl
        jr nz,digitLoop
        ex de,hl
        jp printString

; DE = '$'-terminated text; BC, DE and HL are preserved.
printString:
        push bc
        push de
        push hl
        ld c,9
        call 5
        pop hl
        pop de
        pop bc
        ret

banner:
        defm 13,10,"BENCH80 v2 SCALE="
scaleText:
        defm "1",13,10,"$"
integerBegin:
        defm "@BEGIN INTEGER",13,10,"$"
integerPass:
        defm "@END INTEGER PASS AX=4ED8 BX=D678",13,10,"$"
integerFail:
        defm "@END INTEGER FAIL",13,10,"$"
sieveBegin:
        defm "@BEGIN SIEVE",13,10,"$"
sievePass:
        defm "@END SIEVE PASS PRIMES=1028",13,10,"$"
sieveFail:
        defm "@END SIEVE FAIL",13,10,"$"
done:
        defm "@DONE PASS",13,10,"$"
usage:
        defm "Usage: BENCH80 [1-9]",13,10,"$"
noTimer:
        defm "Timer unavailable. Update firmware.",13,10,"$"
elapsedText:
        defm "  Elapsed: $"
msText:
        defm " ms",13,10,"$"
referenceText:
        defm "  Z80 MODEL ONLY (not measured): 1MHz=$"
ref2Text:
        defm " ms, 2MHz=$"
ref4Text:
        defm " ms, 4MHz=$"
scoreText:
        defm "  Index vs 1MHz MODEL: $"
scoreEndText:
        defm " (100=model, higher=faster; theoretical)",13,10,"$"
tooShortText:
        defm "  Index unavailable: elapsed <1 ms. Increase scale.",13,10,"$"
shortText:
        defm "  Short run: increase scale for better timing accuracy.",13,10,"$"
crlf:
        defm 13,10,"$"
scale:
        defs 1
blocks:
        defs 1
dotsPrinted:
        defs 1
shiftCount:
        defs 1
startTime:
        defs 4
endTime:
        defs 4
elapsed:
        defs 4
dotLast:
        defs 4
dotNow:
        defs 4
dotDiff:
        defs 4
modelBase:
        defs 4
modelMs:
        defs 4
scoreTotal:
        defs 4
remainder:
        defs 4
compareWork:
        defs 4
shiftAdd:
        defs 4
shiftWork:
        defs 4
numberWork:
        defs 4
digits:
        defs 10
digitsEnd:
        defm "$"

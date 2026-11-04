; SYSVIEW 1.0 - CP/M-80 RETRO80 emulator information viewer (VT100/ANSI edition).
; Probes the RETRO80-SYSINFO/1 interface: IN F8 returns A6h when supported,
; OUT F8,1 builds a fresh host snapshot and IN F9 drains it byte by byte
; (the same port layout as the RETRO86 SYSVIEW). BDOS reports the CP/M
; version, drive and user area; the host stream reports firmware and disks.
; Keys: R = refresh; Q or Esc = exit to CP/M.
; Build: python3 build.py (z80asm; flat binary loaded at 0100h).
        org 0100h
stackTop: equ 0C000h
start:
        ld sp,stackTop
refresh:
        xor a
        ld (lines),a
        ld de,clear
        call puts
        ld de,title
        call puts
        ; Probe before any write. Older RETRO80 firmware returns FF on this port.
        in a,(0F8h)
        cp 0A6h
        jr z,supported
        ld de,missing
        call puts
        jp finished
supported:
        ld a,1
        out (0F8h),a
        ; Check the protocol header before interpreting a response.
        ld de,signature
checkHeader:
        ld a,(de)
        or a
        jr z,headerOk
        ld b,a
        inc de
        in a,(0F9h)
        cp b
        jr nz,protocolBad
        jp checkHeader
headerOk:
        ld de,osLabel
        call puts
        ld c,12
        call 5
        call hexword
        ld de,driveLabel
        call puts
        ; BDOS function 25 returns the current drive number in A (not the base-page byte,
        ; which holds the BDOS jump opcode on this system).
        ld c,25
        call 5
        add a,'A'
        call output
        ld de,userLabel
        call puts
        ld c,32
        ld e,0FFh
        call 5
        call decimal
        ld de,newline
        call puts
        ; Bound a faulty stream even if firmware never returns a terminator.
        ld hl,4096
        ld (remaining),hl
readStream:
        in a,(0F9h)
        or a
        jp z,finished
        call output
        ld hl,remaining
        dec hl
        ld (remaining),hl
        ld a,h
        or l
        jp nz,readStream
protocolBad:
        ld de,invalid
        call puts
finished:
        ld de,prompt
        call puts
waitFinal:
        call key
        cp 27
        jp z,exit
        cp 'q'
        jp z,exit
        cp 'Q'
        jp z,exit
        cp 'r'
        jp z,refresh
        cp 'R'
        jp nz,waitFinal
        jp refresh
exit:
        ld de,restore
        call rawputs
        ld c,0
        jp 0
; Waits for one console key and returns it in A (BDOS function 6 status).
key:
        ld c,6
        ld e,0FFh
        call 5
        or a
        jr z,key
        ret
; Prints the character in A and counts printed lines; every 22nd line pauses:
; the line above stays blank and the continue prompt sits on line 24 (bottom).
output:
        push af
        push bc
        push de
        push hl
        call raw
        pop hl
        pop de
        pop bc
        pop af
        cp 10
        ret nz
        ld hl,lines
        inc (hl)
        ld a,(hl)
        cp 22
        ret c
        push af
        push bc
        push de
        push hl
        ld de,more
        call rawputs
moreWait:
        call key
        cp 'q'
        jp z,exit
        cp 'Q'
        jp z,exit
        ld de,erase
        call rawputs
        xor a
        ld (lines),a
        pop hl
        pop de
        pop bc
        pop af
        ret
; Prints the character in A through BDOS function 6 (direct console output);
; all registers are preserved.
raw:
        push af
        push bc
        push de
        push hl
        ld e,a
        ld c,6
        call 5
        pop hl
        pop de
        pop bc
        pop af
        ret
; Prints the NUL-terminated string at DE, counting lines through output.
puts:
        push af
        push bc
        push de
        push hl
putsLoop:
        ld a,(de)
        or a
        jr z,putsEnd
        inc de
        call output
        jp putsLoop
putsEnd:
        pop hl
        pop de
        pop bc
        pop af
        ret
; Prints the NUL-terminated string at DE without line counting (used for the
; continue prompt, its erase line and the exit restore sequence).
rawputs:
        push af
        push bc
        push de
        push hl
rawputsLoop:
        ld a,(de)
        or a
        jr z,rawputsEnd
        inc de
        call raw
        jp rawputsLoop
rawputsEnd:
        pop hl
        pop de
        pop bc
        pop af
        ret
; Prints HL as four hexadecimal digits followed by h.
hexword:
        push af
        push bc
        push de
        push hl
        ld a,h
        call hexNibbleHigh
        ld a,h
        call hexNibbleLow
        ld a,l
        call hexNibbleHigh
        ld a,l
        call hexNibbleLow
        ld a,'h'
        call output
        pop hl
        pop de
        pop bc
        pop af
        ret
hexNibbleHigh:
        rrca
        rrca
        rrca
        rrca
hexNibbleLow:
        and 0Fh
        add a,'0'
        cp '9'+1
        jr c,hexEmit
        add a,7
hexEmit:
        jp output
; Prints the value in A as unsigned decimal without leading zeros.
decimal:
        push af
        push bc
        push de
        push hl
        ld d,0
        ld e,0
dec100: cp 100
        jr c,dec10
        sub 100
        inc d
        jp dec100
dec10:  cp 10
        jr c,decOut
        sub 10
        inc e
        jp dec10
decOut: ld (decOnes),a
        ld a,d
        or a
        jr z,decTens
        add a,'0'
        call output
decTens:
        ld a,d
        or a
        jr nz,decTensPrint
        ld a,e
        or a
        jr z,decPrintOnes
decTensPrint:
        ld a,e
        add a,'0'
        call output
decPrintOnes:
        ld a,(decOnes)
        add a,'0'
        call output
        pop hl
        pop de
        pop bc
        pop af
        ret
lines:  defs 1
remaining: defs 2
decOnes: defs 1
clear:  defm 27,"[0m",27,"[2J",27,"[H",0
title:  defm "SYSVIEW 1.0 - RETRO80 emulator information",13,10
        defm "------------------------------------------------------------",13,10,0
signature: defm "RETRO80-SYSINFO/1",13,10,0
osLabel: defm "CP/M BDOS version:  ",0
driveLabel: defm 13,10,"Current CP/M drive: ",0
userLabel: defm "    User area: ",0
newline: defm 13,10,13,10,0
missing: defm "Emulator information interface is not available.",13,10
        defm "Install the matching RETRO80 firmware update first.",13,10
        defm "No BIOS data guesses or CPU FLAGS-based detection are used.",13,10,0
invalid: defm 13,10,"Invalid or unsupported emulator information response.",13,10,0
prompt: defm 13,10,"R = refresh    Q or Esc = exit to CP/M",13,10,0
; The continue prompt blanks line 23 with CR LF and prints on line 24; the
; erase sequence clears that prompt line and walks the cursor back to line 23.
more:   defm 13,10,"Type any key to continue or Q to exit",0
erase:  defm 13,27,"[2K",27,"[A",0
restore: defm 27,"[0m",27,"[?25h",13,10,0
        end start

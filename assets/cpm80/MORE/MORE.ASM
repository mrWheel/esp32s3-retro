; MORE.COM - file pager for CP/M-80, comparable to Unix more and less
;
; By Willem Aandewiel
;
; 8080 source for the standard Digital Research CP/M ASM.COM and LOAD.COM.
; Build it inside CP/M-80 (the source must keep its CR+LF line endings):
;   A>ASM MORE
;   A>LOAD MORE
; ASM writes MORE.HEX and MORE.PRN, LOAD turns MORE.HEX into MORE.COM.
; The COM image starts at 0100h; the work drive needs about 40 KB free.
;
; Usage: MORE [d:]filename.ext      (type H inside the program for the keys)
;
; The terminal must understand VT100/ANSI sequences, 80 columns by 24 rows.
; Row 24 is used for the status prompt, rows 1-23 show the file.
; Change screenCols and pageRows below for another screen size.
;
; Files are read with BDOS random reads, so the whole file is never kept in
; memory. A ring of the start positions of the last 256 screen rows allows
; fast scrolling backwards; further back the file is rescanned from the top.
; Limits: 65534 screen rows per file; search never matches across a row end.
; Text rules: LF ends a row, CR is ignored, CTRL-Z ends the file, bit 7 is
; ignored, tabs use 8-column stops, other control characters show as ^X and
; rows longer than 80 columns wrap onto the next screen row.

        org 0100h

bdos equ 0005h
fcbDefault equ 005ch
screenCols equ 80
pageRows equ 23
halfRows equ 11
patMax equ 40
keyWait equ 2000

start:
        lxi sp,stackTop
        lda fcbDefault+1
        cpi ' '
        jz showUsage
        lxi h,fcbDefault+1
        mvi b,11
checkWild:
        mov a,m
        cpi '?'
        jz showUsage
        inx h
        dcr b
        jnz checkWild
        lxi h,fcbDefault
        lxi d,fcb
        mvi b,12
copyFcb:
        mov a,m
        stax d
        inx h
        inx d
        dcr b
        jnz copyFcb
        mvi b,24
        xra a
clearFcb:
        stax d
        inx d
        dcr b
        jnz clearFcb
        mvi c,15
        lxi d,fcb
        call bdos
        cpi 0ffh
        jz openFailed
        mvi c,35
        lxi d,fcb
        call bdos
        lhld fcb+33
        shld totalRecs
        lxi h,0
        shld curRecord
        shld nextRow
        shld maxKnown
        xra a
        sta curOffset
        sta atEof
        sta patLen
        lxi h,0ffffh
        shld bufRecord
        mvi c,26
        lxi d,fileBuf
        call bdos
        mvi a,pageRows
        call printRows
        call checkEof
        lda atEof
        ora a
        jnz quitProgram
        jmp mainLoop

showUsage:
        lxi h,strUsage
        call putStr
        jmp 0000h

openFailed:
        lxi h,strNotFound
        call putStr
        jmp 0000h

quitProgram:
        call eraseLine
        jmp 0000h

mainLoop:
        call showPrompt
keyLoop:
        call getKey
        cpi ' '
        jz cmdPageFwd
        cpi 'f'
        jz cmdPageFwd
        cpi 'z'
        jz cmdPageFwd
        cpi 06h
        jz cmdPageFwd
        cpi 0dh
        jz cmdLineFwd
        cpi 'j'
        jz cmdLineFwd
        cpi 'e'
        jz cmdLineFwd
        cpi 0eh
        jz cmdLineFwd
        cpi 'd'
        jz cmdHalfFwd
        cpi 04h
        jz cmdHalfFwd
        cpi 'b'
        jz cmdPageBack
        cpi 02h
        jz cmdPageBack
        cpi 'k'
        jz cmdLineBack
        cpi 'y'
        jz cmdLineBack
        cpi 'u'
        jz cmdHalfBack
        cpi 15h
        jz cmdHalfBack
        cpi 'g'
        jz cmdTop
        cpi '<'
        jz cmdTop
        cpi 'G'
        jz cmdBottom
        cpi '>'
        jz cmdBottom
        cpi '/'
        jz cmdSearch
        cpi 'n'
        jz doSearch
        cpi 'r'
        jz cmdRedraw
        cpi 0ch
        jz cmdRedraw
        cpi 'h'
        jz cmdHelp
        cpi 'H'
        jz cmdHelp
        cpi '?'
        jz cmdHelp
        cpi 'q'
        jz quitProgram
        cpi 'Q'
        jz quitProgram
        cpi 03h
        jz quitProgram
        jmp keyLoop

cmdPageFwd:
        mvi a,pageRows
        jmp doForward
cmdLineFwd:
        mvi a,1
        jmp doForward
cmdHalfFwd:
        mvi a,halfRows
doForward:
        mov b,a
        lda atEof
        ora a
        jnz quitProgram
        call eraseLine
        mov a,b
        call printRows
        call checkEof
        jmp mainLoop

cmdPageBack:
        mvi a,pageRows
        jmp doBack
cmdLineBack:
        mvi a,1
        jmp doBack
cmdHalfBack:
        mvi a,halfRows
doBack:
        mov e,a
        mvi d,0
        lhld nextRow
        mov a,h
        ora a
        jnz backCompute
        mov a,l
        cpi pageRows+1
        jc keyLoop
backCompute:
        mov a,l
        sub e
        mov l,a
        mov a,h
        sbb d
        mov h,a
        jc backClamp
        ora a
        jnz backGo
        mov a,l
        cpi pageRows
        jnc backGo
backClamp:
        lxi h,pageRows
backGo:
        call redrawScreen
        jmp mainLoop

cmdTop:
        lxi h,pageRows
        call redrawScreen
        jmp mainLoop

cmdBottom:
        call getRow
        jnc cmdBottom
        lhld nextRow
        call redrawScreen
        jmp mainLoop

cmdRedraw:
        lhld nextRow
        call redrawScreen
        jmp mainLoop

cmdHelp:
        lxi h,strClear
        call putStr
        lxi h,strHelp
        call putStr
        call waitKey
        lhld nextRow
        call redrawScreen
        jmp mainLoop

cmdSearch:
        call eraseLine
        mvi a,'/'
        call putChar
        call readLine
        jc mainLoop
        lda lineLen
        ora a
        jz doSearch
        sta patLen
        mov c,a
        lxi h,lineBuf
        lxi d,patBuf
copyPattern:
        mov a,m
        stax d
        inx h
        inx d
        dcr c
        jnz copyPattern
doSearch:
        lda patLen
        ora a
        jnz searchStart
        call eraseLine
        lxi h,strNoPattern
        call putStr
        jmp keyLoop
searchStart:
        lhld curRecord
        shld keepRecord
        lda curOffset
        sta keepOffset
        lhld nextRow
        shld keepRow
        lxi d,pageRows-1
        mov a,l
        sub e
        mov l,a
        mov a,h
        sbb d
        mov h,a
        call seekRow
searchLoop:
        call getRow
        jc searchMiss
        call matchRow
        jnc searchLoop
        lhld nextRow
        lxi d,pageRows-1
        dad d
        call redrawScreen
        jmp mainLoop
searchMiss:
        lhld keepRecord
        shld curRecord
        lda keepOffset
        sta curOffset
        lhld keepRow
        shld nextRow
        call eraseLine
        lxi h,strPatMiss
        call putStr
        jmp keyLoop

;
; matchRow: carry set when patBuf occurs in rowBuf (not case sensitive).
;
matchRow:
        lda patLen
        ora a
        jz matchNo
        mov c,a
        lda rowLen
        sub c
        jc matchNo
        inr a
        sta matchCount
        lxi h,rowBuf
matchStart:
        push h
        lxi d,patBuf
        lda patLen
        mov c,a
matchCompare:
        mov a,m
        call toUpper
        xchg
        cmp m
        xchg
        jnz matchNext
        inx h
        inx d
        dcr c
        jnz matchCompare
        pop h
        stc
        ret
matchNext:
        pop h
        inx h
        lda matchCount
        dcr a
        sta matchCount
        jnz matchStart
matchNo:
        xra a
        ret

;
; toUpper: A = character, returns it in upper case. Changes only A and flags.
;
toUpper:
        cpi 'a'
        rc
        cpi 'z'+1
        rnc
        sui 20h
        ret

;
; readLine: edit a line at the cursor into lineBuf (upper case), lineLen
; holds its length. Carry set when cancelled with ESC or CTRL-C.
;
readLine:
        xra a
        sta lineLen
readLineLoop:
        call waitKey
        cpi 0dh
        jz readLineDone
        cpi 0ah
        jz readLineDone
        cpi 1bh
        jz readLineCancel
        cpi 03h
        jz readLineCancel
        cpi 08h
        jz readLineBack
        cpi 7fh
        jz readLineBack
        cpi 20h
        jc readLineLoop
        mov b,a
        lda lineLen
        cpi patMax
        jnc readLineLoop
        mov e,a
        mvi d,0
        lxi h,lineBuf
        dad d
        mov a,b
        call putChar
        mov a,b
        call toUpper
        mov m,a
        lda lineLen
        inr a
        sta lineLen
        jmp readLineLoop
readLineBack:
        lda lineLen
        ora a
        jz readLineLoop
        dcr a
        sta lineLen
        mvi a,8
        call putChar
        mvi a,' '
        call putChar
        mvi a,8
        call putChar
        jmp readLineLoop
readLineDone:
        xra a
        ret
readLineCancel:
        stc
        ret

;
; Keyboard input. rawKey returns 0 when no key is waiting, waitKey waits for
; a key, getKey also decodes the VT100/ANSI cursor keys into command letters
; and ignores a lone ESC. timedKey waits a short while for the rest of an
; escape sequence and returns 0 on timeout.
;
rawKey:
        mvi c,6
        mvi e,0ffh
        call bdos
        ani 7fh
        ret

waitKey:
        call rawKey
        ora a
        jz waitKey
        ret

timedKey:
        lxi h,keyWait
        shld keyTimeout
timedKeyLoop:
        call rawKey
        ora a
        rnz
        lhld keyTimeout
        dcx h
        shld keyTimeout
        mov a,h
        ora l
        jnz timedKeyLoop
        ret

getKey:
        call waitKey
        cpi 1bh
        rnz
        call timedKey
        cpi '['
        jz getKeySeq
        cpi 'O'
        jz getKeySeq
        jmp getKey
getKeySeq:
        call timedKey
        cpi 'A'
        jz getKeyUp
        cpi 'B'
        jz getKeyDown
        cpi 'H'
        jz getKeyHome
        cpi 'F'
        jz getKeyEnd
        cpi '1'
        jz getKeyHomeTilde
        cpi '7'
        jz getKeyHomeTilde
        cpi '4'
        jz getKeyEndTilde
        cpi '8'
        jz getKeyEndTilde
        cpi '5'
        jz getKeyPgUp
        cpi '6'
        jz getKeyPgDn
        jmp getKey
getKeyUp:
        mvi a,'k'
        ret
getKeyDown:
        mvi a,'j'
        ret
getKeyHome:
        mvi a,'g'
        ret
getKeyEnd:
        mvi a,'G'
        ret
getKeyHomeTilde:
        mvi b,'g'
        jmp getKeyTilde
getKeyEndTilde:
        mvi b,'G'
        jmp getKeyTilde
getKeyPgUp:
        mvi b,'b'
        jmp getKeyTilde
getKeyPgDn:
        mvi b,'f'
getKeyTilde:
        push b
        call timedKey
        pop b
        mov a,b
        ret

;
; Console output goes straight to the BIOS CONOUT entry so BDOS does not
; swallow keys that are typed while a page is being printed.
; putChar: A = character. Changes only A and flags.
;
putChar:
        push b
        push d
        push h
        mov c,a
        lxi h,putCharDone
        push h
        lhld 0001h
        lxi d,9
        dad d
        pchl
putCharDone:
        pop h
        pop d
        pop b
        ret

;
; putStr: HL = zero terminated string. Changes only A, HL and flags.
;
putStr:
        mov a,m
        ora a
        rz
        call putChar
        inx h
        jmp putStr

eraseLine:
        mvi a,0dh
        call putChar
        lxi h,strEraseEol
        jmp putStr

;
; showPrompt: reverse video status text on the last screen row.
;
showPrompt:
        call eraseLine
        lxi h,strReverse
        call putStr
        lda atEof
        ora a
        jz promptMore
        lxi h,strEndText
        call putStr
        jmp promptDone
promptMore:
        lxi h,strMoreHead
        call putStr
        call putPercent
        lxi h,strMoreTail
        call putStr
promptDone:
        lxi h,strNormal
        jmp putStr

;
; putPercent: prints the file position (0-100) of the next unread row.
;
putPercent:
        lhld totalRecs
        mov a,h
        ora l
        jz percentHundred
        xchg
        lhld curRecord
percentScale:
        mov a,d
        cpi 2
        jc percentScaled
        ora a
        mov a,d
        rar
        mov d,a
        mov a,e
        rar
        mov e,a
        ora a
        mov a,h
        rar
        mov h,a
        mov a,l
        rar
        mov l,a
        jmp percentScale
percentScaled:
        mov b,d
        mov c,e
        dad h
        dad h
        push h
        dad h
        dad h
        dad h
        push h
        dad h
        pop d
        dad d
        pop d
        dad d
        lxi d,0
percentDivide:
        mov a,l
        sub c
        mov l,a
        mov a,h
        sbb b
        mov h,a
        jc percentDone
        inx d
        jmp percentDivide
percentDone:
        mov a,d
        ora a
        jnz percentHundred
        mov a,e
        cpi 101
        jc percentPrint
percentHundred:
        mvi a,100
percentPrint:
        mvi b,0
        cpi 100
        jc percentTens
        sui 100
        push psw
        mvi a,'1'
        call putChar
        pop psw
        mvi b,1
percentTens:
        mvi c,0
percentTensLoop:
        cpi 10
        jc percentOnes
        sui 10
        inr c
        jmp percentTensLoop
percentOnes:
        mov d,a
        mov a,c
        ora a
        jnz percentTensOut
        mov a,b
        ora a
        jz percentOnesOut
percentTensOut:
        mov a,c
        adi '0'
        call putChar
percentOnesOut:
        mov a,d
        adi '0'
        jmp putChar

;
; printRows: A = number of rows to print. Prints rows from the current file
; position, each followed by CR+LF. Stops early at the end of the file;
; rowsLeft is then not zero.
;
printRows:
        sta rowsLeft
printRowsLoop:
        call getRow
        rc
        call printRow
        lda rowsLeft
        dcr a
        sta rowsLeft
        jnz printRowsLoop
        ret

printRow:
        lda rowLen
        ora a
        jz printRowEnd
        mov b,a
        lxi h,rowBuf
printRowChars:
        mov a,m
        call putChar
        inx h
        dcr b
        jnz printRowChars
printRowEnd:
        mvi a,0dh
        call putChar
        mvi a,0ah
        jmp putChar

;
; checkEof: sets atEof when no row follows the current position. The file
; position is left unchanged.
;
checkEof:
        lhld curRecord
        shld keepRecord
        lda curOffset
        sta keepOffset
        lhld nextRow
        shld keepRow
        call getRow
        sbb a
        sta atEof
        lhld keepRecord
        shld curRecord
        lda keepOffset
        sta curOffset
        lhld keepRow
        shld nextRow
        ret

;
; redrawScreen: HL = row number that follows the last row on the screen.
; Clears the screen and prints the pageRows rows above it. When the end of
; the file comes earlier, the last full page is shown instead.
;
redrawScreen:
        shld targetBottom
redrawAgain:
        lhld targetBottom
        lxi d,pageRows
        mov a,l
        sub e
        mov l,a
        mov a,h
        sbb d
        mov h,a
        jnc redrawTopOk
        lxi h,0
redrawTopOk:
        shld redrawTop
        call seekRow
        lxi h,strClear
        call putStr
        mvi a,pageRows
        call printRows
        lda rowsLeft
        ora a
        jz redrawDone
        lhld redrawTop
        mov a,h
        ora l
        jz redrawDone
        lhld nextRow
        shld targetBottom
        jmp redrawAgain
redrawDone:
        jmp checkEof

;
; seekRow: HL = row number. Sets the file position to the start of that row.
; Rows up to 255 before the highest row seen come from the ring, all others
; are found by rescanning the file from the start.
;
seekRow:
        shld seekTarget
        xchg
        lhld maxKnown
        mov a,l
        sub e
        mov a,h
        sbb d
        jc seekRescan
        ora a
        jnz seekRescan
        lhld seekTarget
        mov a,l
        call ringAddress
        mov e,m
        inx h
        mov d,m
        inx h
        mov a,m
        sta curOffset
        xchg
        shld curRecord
        lhld seekTarget
        shld nextRow
        ret
seekRescan:
        lxi h,0
        shld curRecord
        shld nextRow
        shld maxKnown
        xra a
        sta curOffset
seekScanLoop:
        lhld nextRow
        xchg
        lhld seekTarget
        mov a,l
        cmp e
        jnz seekScanStep
        mov a,h
        cmp d
        rz
seekScanStep:
        call getRow
        rc
        jmp seekScanLoop

;
; ringAddress: A = row number (low byte), returns HL = ring slot address.
;
ringAddress:
        mov l,a
        mvi h,0
        mov e,l
        mov d,h
        dad h
        dad d
        lxi d,ringBuf
        dad d
        ret

;
; getRow: reads the row at the current file position into rowBuf/rowLen and
; advances nextRow. Carry set (nothing read) at the end of the file.
;
getRow:
        lhld nextRow
        mov a,h
        ana l
        cpi 0ffh
        jnz getRowStart
        stc
        ret
getRowStart:
        xchg
        lhld maxKnown
        mov a,l
        sub e
        mov a,h
        sbb d
        jnc getRowSlot
        xchg
        shld maxKnown
getRowSlot:
        lhld nextRow
        mov a,l
        call ringAddress
        lda curRecord
        mov m,a
        inx h
        lda curRecord+1
        mov m,a
        inx h
        lda curOffset
        mov m,a
        xra a
        sta rowLen
        sta gotAny
getRowLoop:
        call savePos
        call getByte
        jc getRowEof
        ani 7fh
        cpi 0ah
        jz getRowDone
        cpi 0dh
        jz getRowLoop
        mov b,a
        mvi a,1
        sta gotAny
        mov a,b
        cpi 09h
        jz getRowTab
        cpi 20h
        jc getRowCtrl
        cpi 7fh
        jz getRowCtrl
        call appendRow
        jmp getRowCheck
getRowCtrl:
        lda rowLen
        cpi screenCols-1
        jc getRowCtrlFits
        call restorePos
        jmp getRowDone
getRowCtrlFits:
        mvi a,'^'
        call appendRow
        mov a,b
        xri 40h
        call appendRow
        jmp getRowCheck
getRowTab:
        mvi a,' '
        call appendRow
        lda rowLen
        ani 7
        jnz getRowTab
getRowCheck:
        lda rowLen
        cpi screenCols
        jnz getRowLoop
getRowFull:
        call savePos
        call getByte
        jc getRowDone
        ani 7fh
        cpi 0dh
        jz getRowFull
        cpi 0ah
        jz getRowDone
        call restorePos
getRowDone:
        lhld nextRow
        inx h
        shld nextRow
        xra a
        ret
getRowEof:
        lda gotAny
        ora a
        jnz getRowDone
        stc
        ret

;
; appendRow: A = character to add to rowBuf. Keeps BC.
;
appendRow:
        push h
        push d
        push psw
        lda rowLen
        mov e,a
        mvi d,0
        lxi h,rowBuf
        dad d
        pop psw
        mov m,a
        lda rowLen
        inr a
        sta rowLen
        pop d
        pop h
        ret

savePos:
        lhld curRecord
        shld savedRecord
        lda curOffset
        sta savedOffset
        ret

restorePos:
        lhld savedRecord
        shld curRecord
        lda savedOffset
        sta curOffset
        ret

;
; getByte: next byte of the file in A, carry set at the end of the file
; (also at CTRL-Z, which is not consumed). Reads a record only when the
; position left the record that is in fileBuf.
;
getByte:
        lhld bufRecord
        xchg
        lhld curRecord
        mov a,l
        cmp e
        jnz getByteLoad
        mov a,h
        cmp d
        jz getByteHave
getByteLoad:
        shld fcb+33
        xra a
        sta fcb+35
        mvi c,33
        lxi d,fcb
        call bdos
        ora a
        jnz getByteFail
        lhld curRecord
        shld bufRecord
getByteHave:
        lda curOffset
        mov e,a
        mvi d,0
        lxi h,fileBuf
        dad d
        mov a,m
        cpi 1ah
        jz getByteEof
        mov b,a
        lda curOffset
        inr a
        ani 7fh
        sta curOffset
        jnz getByteDone
        lhld curRecord
        inx h
        shld curRecord
getByteDone:
        mov a,b
        ora a
        ret
getByteFail:
        lxi h,0ffffh
        shld bufRecord
getByteEof:
        stc
        ret

strClear:
        db 27,'[H',27,'[J',0
strEraseEol:
        db 27,'[K',0
strReverse:
        db 27,'[7m',0
strNormal:
        db 27,'[m',0
strMoreHead:
        db '--More--(',0
strMoreTail:
        db '%) h=help q=quit',0
strEndText:
        db '(END) b=back q=quit',0
strPatMiss:
        db 27,'[7mPattern not found',27,'[m',0
strNoPattern:
        db 27,'[7mNo previous search pattern',27,'[m',0
strUsage:
        db 'Usage: MORE [d:]filename.ext',13,10,0
strNotFound:
        db 'File not found.',13,10,0
strHelp:
        db 'MORE - file pager for CP/M-80 (like Unix more and less)',13,10
        db 13,10
        db '  SPACE, f, z    next page            b          previous page',13,10
        db '  ENTER, j, e    next line            k, y       previous line',13,10
        db '  d              half page forward    u          half page back',13,10
        db '  g, <           top of the file      '
        db 'G, >       bottom of the file',13,10
        db '  /text          search forward (upper/lower case is ignored)',13,10
        db '  n              repeat the last search',13,10
        db '  r, CTRL-L      redraw the screen    h, ?       this help',13,10
        db '  q, Q, CTRL-C   quit',13,10
        db 13,10
        db 'Arrow keys, PgUp, PgDn, Home and End work on a '
        db 'VT100/ANSI terminal.',13,10
        db 'At the end of the file any forward key quits.',13,10
        db 13,10
        db 'Press any key to continue',0

fcb:
        ds 36
totalRecs:
        ds 2
curRecord:
        ds 2
curOffset:
        ds 1
bufRecord:
        ds 2
nextRow:
        ds 2
maxKnown:
        ds 2
atEof:
        ds 1
rowLen:
        ds 1
gotAny:
        ds 1
rowsLeft:
        ds 1
savedRecord:
        ds 2
savedOffset:
        ds 1
keepRecord:
        ds 2
keepOffset:
        ds 1
keepRow:
        ds 2
seekTarget:
        ds 2
targetBottom:
        ds 2
redrawTop:
        ds 2
keyTimeout:
        ds 2
matchCount:
        ds 1
patLen:
        ds 1
patBuf:
        ds 41
lineLen:
        ds 1
lineBuf:
        ds 41
rowBuf:
        ds 80
fileBuf:
        ds 128
ringBuf:
        ds 768
stackBase:
        ds 128
stackTop:
        end

; HOST.COM - CP/M-80 host exchange utility
;
; By Willem Aandewiel
;
; 8080 source for the standard Digital Research CP/M ASM.COM and LOAD.COM.
; Build it inside CP/M-80 (the source must keep its CR+LF line endings):
;   A>ASM HOST
;   A>LOAD HOST
; ASM writes HOST.HEX and HOST.PRN, LOAD turns HOST.HEX into HOST.COM.
; The COM image starts at 0100h; the work drive needs about 40 KB free.
;
; Exchange ABI: immediate byte IN/OUT on F8h; abort is OUT (F9h),00h.
; Host commands: 1 DIR, 2 GET, 3 PUT. GET and PUT accept CP/M wildcards.
; HOST GET name.ext O replaces an existing target and its .HST sidecar.
; HOST PUT name.ext O replaces an existing exchange file (host command 4).
; A dot is printed for every 16 records (2 KB) while GET and PUT run.

        org 0100h

bdos equ 0005h
tailLength equ 0080h
tailText equ 0081h
ccpFcb equ 005ch
portData equ 0f8h
portAbort equ 0f9h

start:
        lxi sp,0ffffh
        xra a
        sta abortFlag
        sta overwriteFlag
        sta workOpen
        sta workCreated
        sta sidecarOpen
        sta sidecarCreated
        call parseCommand
        jc badCommand
        lda commandKind
        cpi 1
        jz commandDir
        mvi e,0ffh
        mvi c,32
        call bdos
        ora a
        jnz userAreaRejected
        call copyAndValidateFcb
        jc filenameRejected
        call checkService
        jc unsupportedService
        lda wildcardActive
        ora a
        jz startSingleCommand
        lda commandKind
        cpi 2
        jz commandGetWildcard
        jmp commandPutWildcard
startSingleCommand:
        lda commandKind
        cpi 2
        jz commandGet
        jmp commandPut

checkService:
        xra a
        call sendByte
        rc
        call readByte
        rc
        ora a
        jnz serviceCheckBad
        call readByte
        rc
        cpi 1
        jnz serviceCheckBad
        call readByte
        rc
        ani 7
        cpi 7
        jnz serviceCheckBad
        ora a
        ret
serviceCheckBad:
        stc
        ret

parseCommand:
        lda tailLength
        ora a
        jz parseBad
        mov e,a
        mvi d,0
        lxi h,tailText
        dad d
        shld tailEndPtr
        lxi h,tailText
parseSkipLeading:
        xchg
        lhld tailEndPtr
        xchg
        call compareHlDe
        jz parseBad
        mov a,m
        cpi ' '
        jnz parseAction
        inx h
        jmp parseSkipLeading
parseAction:
        mov a,m
        call uppercaseCharacter
        cpi 'D'
        jz parseDirToken
        cpi 'G'
        jz parseGetToken
        cpi 'P'
        jz parsePutToken
        jmp parseBad
parseDirToken:
        inx h
        mov a,m
        call uppercaseCharacter
        cpi 'I'
        jnz parseBad
        inx h
        mov a,m
        call uppercaseCharacter
        cpi 'R'
        jnz parseBad
        inx h
        mvi a,1
        sta commandKind
        call checkTokenBoundary
        jc parseBad
        call skipSpaces
        jnc parseBad
        jmp parseGood
parseGetToken:
        inx h
        mov a,m
        call uppercaseCharacter
        cpi 'E'
        jnz parseBad
        inx h
        mov a,m
        call uppercaseCharacter
        cpi 'T'
        jnz parseBad
        inx h
        mvi a,2
        sta commandKind
        call checkTokenBoundary
        jc parseBad
        call skipSpaces
        jc parseBad
        call checkFilenameArgument
        jc parseBad
        jmp parseGood
parsePutToken:
        inx h
        mov a,m
        call uppercaseCharacter
        cpi 'U'
        jnz parseBad
        inx h
        mov a,m
        call uppercaseCharacter
        cpi 'T'
        jnz parseBad
        inx h
        mvi a,3
        sta commandKind
        call checkTokenBoundary
        jc parseBad
        call skipSpaces
        jc parseBad
        call checkFilenameArgument
        jc parseBad
parseGood:
        ora a
        ret
parseBad:
        stc
        ret

uppercaseCharacter:
        cpi 'a'
        rc
        cpi 'z'+1
        rnc
        sui 20h
        ret

checkTokenBoundary:
        xchg
        lhld tailEndPtr
        xchg
        call compareHlDe
        jz tokenBoundaryAtEnd
        mov a,m
        cpi ' '
        jz tokenBoundaryAfterSpace
        stc
        ret
tokenBoundaryAtEnd:
        ora a
        ret
tokenBoundaryAfterSpace:
        ora a
        ret

skipSpaces:
        xchg
        lhld tailEndPtr
        xchg
        call compareHlDe
        jz skipSpacesEnd
        mov a,m
        cpi ' '
        jnz skipSpacesDone
        inx h
        jmp skipSpaces
skipSpacesEnd:
        stc
        ret
skipSpacesDone:
        ora a
        ret

checkFilenameArgument:
        xchg
        lhld tailEndPtr
        xchg
        call compareHlDe
        jz filenameArgumentBad
        mov a,m
        cpi ' '
        jz filenameArgumentBad
filenameScan:
        xchg
        lhld tailEndPtr
        xchg
        call compareHlDe
        jz filenameArgumentGood
        mov a,m
        cpi ' '
        jz filenameTrailingSpaces
        inx h
        jmp filenameScan
filenameTrailingSpaces:
        call skipSpaces
        jc filenameArgumentGood
        lda commandKind
        cpi 2
        jz filenameOptionAllowed
        cpi 3
        jnz filenameArgumentBad
filenameOptionAllowed:
        mov a,m
        call uppercaseCharacter
        cpi 'O'
        jnz filenameArgumentBad
        inx h
        call checkTokenBoundary
        jc filenameArgumentBad
        mvi a,1
        sta overwriteFlag
        call skipSpaces
        jc filenameArgumentGood
        jmp filenameArgumentBad
filenameArgumentGood:
        ora a
        ret
filenameArgumentBad:
        stc
        ret

copyAndValidateFcb:
        lxi h,workFcb
        mvi b,36
        xra a
clearWorkFcb:
        mov m,a
        inx h
        dcr b
        jnz clearWorkFcb
        lda ccpFcb+16
        sta workFcb
        lxi h,ccpFcb+17
        lxi d,workFcb+1
        mvi b,11
copyCcpName:
        mov a,m
        stax d
        inx h
        inx d
        dcr b
        jnz copyCcpName
        call normalizeWildcards
        xra a
        sta wildcardActive
        lxi h,workFcb+1
        mvi b,8
        mvi c,0
        mvi d,0
validateBase:
        mov a,m
        cpi ' '
        jz validateBaseSpace
        mov a,d
        ora a
        jnz invalidFcb
        inr c
        mov a,m
        cpi '?'
        jz validateBaseWildcard
        cpi '*'
        jz validateBaseWildcard
        call validFilenameCharacter
        jc invalidFcb
        jmp validateBaseNext
validateBaseWildcard:
        mvi a,1
        sta wildcardActive
        jmp validateBaseNext
validateBaseSpace:
        mvi d,1
validateBaseNext:
        inx h
        dcr b
        jnz validateBase
        mov a,c
        ora a
        jz invalidFcb
        mvi b,3
        xra a
        mov d,a
validateExtension:
        mov a,m
        cpi ' '
        jz validateExtensionSpace
        mov a,d
        ora a
        jnz invalidFcb
        mov a,m
        cpi '?'
        jz validateExtensionWildcard
        cpi '*'
        jz validateExtensionWildcard
        call validFilenameCharacter
        jc invalidFcb
        jmp validateExtensionNext
validateExtensionWildcard:
        mvi a,1
        sta wildcardActive
        jmp validateExtensionNext
validateExtensionSpace:
        mvi d,1
validateExtensionNext:
        inx h
        dcr b
        jnz validateExtension
        lda workFcb+9
        cpi 'H'
        jnz copyInitSideFcb
        lda workFcb+10
        cpi 'S'
        jnz copyInitSideFcb
        lda workFcb+11
        cpi 'T'
        jz invalidFcb
copyInitSideFcb:
        call initSideFcb
        ora a
        ret
invalidFcb:
        stc
        ret

normalizeWildcards:
        lxi h,workFcb+1
        mvi b,8
normalizeBase:
        mov a,m
        cpi '*'
        jz normalizeBaseFill
        inx h
        dcr b
        jnz normalizeBase
        jmp normalizeExtensionStart
normalizeBaseFill:
        mvi m,'?'
        inx h
        dcr b
        jnz normalizeBaseFill
normalizeExtensionStart:
        lxi h,workFcb+9
        mvi b,3
normalizeExtension:
        mov a,m
        cpi '*'
        jz normalizeExtensionFill
        inx h
        dcr b
        jnz normalizeExtension
        ret
normalizeExtensionFill:
        mvi m,'?'
        inx h
        dcr b
        jnz normalizeExtensionFill
        ret

validFilenameCharacter:
        cpi 'A'
        jc validFilenameDigit
        cpi 'Z'+1
        jc validFilenameGood
validFilenameDigit:
        cpi '0'
        jc validFilenameSymbol
        cpi '9'+1
        jc validFilenameGood
validFilenameSymbol:
        cpi '_'
        jz validFilenameGood
        cpi '-'
        jz validFilenameGood
        stc
        ret
validFilenameGood:
        ora a
        ret

initSideFcb:
        lxi h,sideFcb
        mvi b,36
        xra a
clearSideFcb:
        mov m,a
        inx h
        dcr b
        jnz clearSideFcb
        lxi h,workFcb
        lxi d,sideFcb
        lxi b,12
        call copyBlock
        mvi a,'H'
        sta sideFcb+9
        mvi a,'S'
        sta sideFcb+10
        mvi a,'T'
        sta sideFcb+11
        ret

commandDir:
        mvi a,1
        call sendByte
        jc operationAborted
        call readByte
        jc operationAborted
        ora a
        jnz showHostError
        xra a
        sta dirNameActive
dirRead:
        call readByte
        jc operationAborted
        ora a
        jz dirStringEnd
        sta charValue
        mvi a,1
        sta dirNameActive
        lda charValue
        mov e,a
        mvi c,2
        call bdos
        jmp dirRead
dirStringEnd:
        lda dirNameActive
        ora a
        jz dirListEnd
        call printCrLf
        xra a
        sta dirNameActive
        jmp dirRead
dirListEnd:
        call readByte
        jc operationAborted
        ora a
        jnz showHostError
        jmp terminate

commandGetWildcard:
        xra a
        sta wildcardCount
        lxi h,workFcb+1
        lxi d,patternFcb
        lxi b,11
        call copyBlock
        mvi a,1
        call sendByte
        jc operationAborted
        call readByte
        jc operationAborted
        ora a
        jnz showHostError
getWildcardReadName:
        xra a
        sta hostNameLength
        sta hostNameOverflow
getWildcardReadCharacter:
        call readByte
        jc operationAborted
        ora a
        jz getWildcardNameEnd
        sta charValue
        lda hostNameLength
        cpi 12
        jnc getWildcardNameOverflow
        mov e,a
        mvi d,0
        lxi h,hostFilename
        dad d
        lda charValue
        mov m,a
        lda hostNameLength
        inr a
        sta hostNameLength
        jmp getWildcardReadCharacter
getWildcardNameOverflow:
        mvi a,1
        sta hostNameOverflow
        jmp getWildcardReadCharacter
getWildcardNameEnd:
        lda hostNameOverflow
        ora a
        jnz getWildcardReadName
        lda hostNameLength
        ora a
        jz getWildcardListEnd
        mov e,a
        mvi d,0
        lxi h,hostFilename
        dad d
        xra a
        mov m,a
        call convertHostName
        jc getWildcardReadName
        lda candidateName+8
        cpi 'H'
        jnz getWildcardCheckPattern
        lda candidateName+9
        cpi 'S'
        jnz getWildcardCheckPattern
        lda candidateName+10
        cpi 'T'
        jnz getWildcardCheckPattern
        lxi h,candidateName
        lxi d,workFcb+1
        lxi b,11
        call copyBlock
        mvi a,17
        sta fileError
        call printFileFailure
        jmp getWildcardReadName
getWildcardCheckPattern:
        call matchHostName
        jc getWildcardReadName
        lda wildcardCount
        inr a
        sta wildcardCount
        lxi h,candidateName
        lxi d,workFcb+1
        lxi b,11
        call copyBlock
        lxi h,workFcb+12
        mvi b,24
        xra a
getWildcardClearFcb:
        mov m,a
        inx h
        dcr b
        jnz getWildcardClearFcb
        call initSideFcb
        call commandGetSingle
getWildcardListEnd:
        call readByte
        jc operationAborted
        ora a
        jnz showHostError
        lda wildcardCount
        ora a
        jnz getWildcardFinished
        mvi a,16
        sta fileError
        call printFileFailure
        jmp terminate
getWildcardFinished:
        lxi d,msgGetDone
        call printString
        jmp terminate

convertHostName:
        lxi h,candidateName
        mvi b,11
        mvi a,' '
convertHostClearCandidate:
        mov m,a
        inx h
        dcr b
        jnz convertHostClearCandidate
        xra a
        sta hostNameExtension
        sta candidatePosition
        sta candidateBaseLength
        lxi h,hostFilename
        shld hostSourcePointer
convertHostCharacter:
        lhld hostSourcePointer
        mov a,m
        ora a
        jz convertHostDone
        inx h
        shld hostSourcePointer
        cpi '.'
        jz convertHostDot
        cpi 'a'
        jc convertHostUppercaseDone
        cpi 'z'+1
        jnc convertHostUppercaseDone
        sui 20h
convertHostUppercaseDone:
        call validHostNameCharacter
        jc convertHostInvalid
        mov e,a
        lda hostNameExtension
        ora a
        jnz convertHostStoreExtension
        lda candidateBaseLength
        cpi 8
        jnc convertHostInvalid
        inr a
        sta candidateBaseLength
        mov c,a
        dcr c
        mvi b,0
        lxi h,candidateName
        dad b
        mov a,e
        mov m,a
        jmp convertHostCharacter
convertHostStoreExtension:
        lda candidatePosition
        cpi 11
        jnc convertHostInvalid
        mov c,a
        mvi b,0
        lxi h,candidateName
        dad b
        mov a,e
        mov m,a
        lda candidatePosition
        inr a
        sta candidatePosition
        jmp convertHostCharacter
convertHostDot:
        lda hostNameExtension
        ora a
        jnz convertHostInvalid
        lda candidateBaseLength
        ora a
        jz convertHostInvalid
        mvi a,1
        sta hostNameExtension
        mvi a,8
        sta candidatePosition
        jmp convertHostCharacter
convertHostDone:
        lda candidateBaseLength
        ora a
        jz convertHostInvalid
        ora a
        ret
convertHostInvalid:
        stc
        ret

validHostNameCharacter:
        cpi 'A'
        jc validHostNameCheckDigit
        cpi 'Z'+1
        jc validHostNameGood
validHostNameCheckDigit:
        cpi '0'
        jc validHostNameCheckSymbol
        cpi '9'+1
        jc validHostNameGood
validHostNameCheckSymbol:
        cpi '_'
        jz validHostNameGood
        cpi '-'
        jz validHostNameGood
        stc
        ret
validHostNameGood:
        ora a
        ret

matchHostName:
        lxi h,patternFcb
        lxi d,candidateName
        mvi b,11
matchHostNameByte:
        mov a,m
        cpi '?'
        jz matchHostNameNext
        ldax d
        cmp m
        jnz matchHostNameBad
matchHostNameNext:
        inx h
        inx d
        dcr b
        jnz matchHostNameByte
        ora a
        ret
matchHostNameBad:
        stc
        ret

commandGet:
commandGetSingle:
        xra a
        sta fileError
        sta dotCounter
        sta dotsPrinted
        call printFileStart
        xra a
        sta workOpen
        sta workCreated
        sta sidecarOpen
        sta sidecarCreated
        lda overwriteFlag
        ora a
        jnz getSidecarAvailable
        call ensureTargetAbsent
        jnc getTargetAvailable
        mvi a,1
        sta fileError
        jmp getFileFailure
getTargetAvailable:
        call ensureSidecarAbsent
        jnc getSidecarAvailable
        mvi a,2
        sta fileError
        jmp getFileFailure
getSidecarAvailable:
        mvi a,2
        call sendByte
        jc getTransferFailed
        lxi h,workFcb+1
        mvi b,11
getSendName:
        mov a,m
        ani 07fh
        call sendByte
        jc getTransferFailed
        inx h
        dcr b
        jnz getSendName
        call readByte
        jc getTransferFailed
        ora a
        jz getAccepted
        adi 10
        sta fileError
        jmp getFileFailure
getAccepted:
        lxi h,getLength
        mvi b,4
getReadLength:
        call readByte
        jc getTransferFailed
        mov m,a
        inx h
        dcr b
        jnz getReadLength
        call copyGetLengthToRemaining
        call initGetCrcs
        lda overwriteFlag
        ora a
        cnz deleteExistingTargets
        lxi d,workFcb
        mvi c,22
        call bdos
        cpi 0ffh
        jnz getTargetCreated
        mvi a,20
        sta fileError
        jmp getLocalFailure
getTargetCreated:
        mvi a,1
        sta workOpen
        sta workCreated
        lxi d,recordBuffer
        mvi c,26
        call bdos
        call fillRecordBuffer
        xra a
        sta recordCount
getPayloadLoop:
        call remainingIsZero
        jz getPayloadDone
        call readByte
        jc getFailure
        sta crcInput
        push h
        lxi h,exactCrc
        shld crcPointer
        lda crcInput
        call crcUpdate
        pop h
        ; The DMA buffer cursor is advanced independently of the CRC routine.
        lda recordCount
        mov e,a
        mvi d,0
        lxi h,recordBuffer
        dad d
        lda crcInput
        mov m,a
        call decrementRemaining
        lda recordCount
        inr a
        sta recordCount
        cpi 128
        jnz getPayloadLoop
        call writeGetRecord
        jc getFailure
        call progressTick
        xra a
        sta recordCount
        call fillRecordBuffer
        jmp getPayloadLoop
getPayloadDone:
        lda recordCount
        ora a
        jz getPayloadCrc
        call writeGetRecord
        jc getFailure
getPayloadCrc:
        lxi h,exactCrc
        shld crcPointer
        call finalizeCrc
        call readExpectedCrc
        jc getFailure
        call compareExactCrc
        jc getFailure
        call readByte
        jc getFailure
        ora a
        jz getFinalStatusOk
        adi 10
        sta fileError
        jmp getFailure
getFinalStatusOk:
        call closeWorkFile
        cpi 0ffh
        jnz getClosedOk
        mvi a,7
        sta fileError
        jmp getFailure
getClosedOk:
        call writeSidecar
        jc getFailure
        lda wildcardActive
        ora a
        jz getSingleComplete
        call printFileSuccess
        jmp getWildcardReadName
getSingleComplete:
        call endProgress
        lxi d,msgGetDone
        call printString
        jmp terminate

getFailure:
        lda fileError
        ora a
        jnz getFailureStatusReady
        mvi a,6
        sta fileError
getFailureStatusReady:
        call abortHostForFile
        call cleanupGetFiles
        lda abortFlag
        ora a
        jnz getUserAbort
        jmp getFileFailure

getTransferFailed:
        mvi a,6
        sta fileError
        call abortHostForFile
        lda abortFlag
        ora a
        jnz getUserAbort
        jmp getFileFailure

getLocalFailure:
        call abortHostForFile
        jmp getFileFailure

getFileFailure:
        call printFileFailure
        lda wildcardActive
        ora a
        jnz getWildcardReadName
        jmp terminate

getUserAbort:
        call abortHost
        call printFileFailure
        lxi d,msgAborted
        call printString
        jmp terminate

ensureTargetAbsent:
        call openWorkFcb
        cpi 0ffh
        jz targetAbsent
        lxi d,workFcb
        mvi c,16
        call bdos
        stc
        ret
targetAbsent:
        ora a
        ret

ensureSidecarAbsent:
        call openSideFcb
        cpi 0ffh
        jz sidecarAbsent
        lxi d,sideFcb
        mvi c,16
        call bdos
        stc
        ret
sidecarAbsent:
        ora a
        ret

deleteExistingTargets:
        call openWorkFcb
        lxi d,workFcb
        mvi c,19
        call bdos
        call openSideFcb
        lxi d,sideFcb
        mvi c,19
        call bdos
        ret

copyGetLengthToRemaining:
        lxi h,getLength
        lxi d,remaining
        lxi b,4
        call copyBlock
        ret

initGetCrcs:
        lxi h,exactCrc
        shld crcPointer
        call initCrc
        lxi h,paddedCrc
        shld crcPointer
        call initCrc
        ret

fillRecordBuffer:
        lxi h,recordBuffer
        mvi b,128
fillRecordLoop:
        mvi m,01ah
        inx h
        dcr b
        jnz fillRecordLoop
        ret

writeGetRecord:
        lxi h,recordBuffer
        shld crcDataPointer
        mvi a,128
        sta crcByteCount
        lxi h,paddedCrc
        shld crcPointer
        call crcBuffer
        lxi d,workFcb
        mvi c,21
        call bdos
        ora a
        rz
        call setWriteError
        stc
        ret

setWriteError:
        cpi 2
        jz setWriteErrorDiskFull
        cpi 1
        jz setWriteErrorDirFull
        mvi a,7
        jmp setWriteErrorStore
setWriteErrorDiskFull:
        mvi a,19
        jmp setWriteErrorStore
setWriteErrorDirFull:
        mvi a,20
setWriteErrorStore:
        sta fileError
        ret

progressTick:
        lda dotCounter
        inr a
        sta dotCounter
        ani 0fh
        rnz
        mvi a,1
        sta dotsPrinted
        mvi e,'.'
        mvi c,2
        call bdos
        ret

endProgress:
        lda dotsPrinted
        ora a
        rz
        xra a
        sta dotsPrinted
        mvi e,' '
        mvi c,2
        call bdos
        ret

readExpectedCrc:
        lxi h,expectedCrc
        mvi b,4
readExpectedLoop:
        call readByte
        rc
        mov m,a
        inx h
        dcr b
        jnz readExpectedLoop
        ora a
        ret

compareExactCrc:
        lxi h,exactCrc
        lxi d,expectedCrc
        mvi b,4
compareExactLoop:
        ldax d
        cmp m
        jnz compareExactBad
        inx h
        inx d
        dcr b
        jnz compareExactLoop
        ora a
        ret
compareExactBad:
        stc
        ret

writeSidecar:
        call finalizeCrcAtPadded
        lxi h,sideBuffer
        call initializeSideBuffer
        lxi h,metadataMagic
        lxi d,sideBuffer
        lxi b,4
        call copyBlock
        lxi h,getLength
        lxi d,sideBuffer+4
        lxi b,4
        call copyBlock
        lxi h,paddedCrc
        lxi d,sideBuffer+8
        lxi b,4
        call copyBlock
        lxi h,exactCrc
        lxi d,sideBuffer+12
        lxi b,4
        call copyBlock
        lxi d,sideFcb
        mvi c,22
        call bdos
        cpi 0ffh
        jnz sidecarCreated0
        mvi a,20
        sta fileError
        jmp sidecarWriteBad
sidecarCreated0:
        mvi a,1
        sta sidecarOpen
        sta sidecarCreated
        lxi d,sideBuffer
        mvi c,26
        call bdos
        lxi d,sideFcb
        mvi c,21
        call bdos
        ora a
        jnz sidecarWriteCloseBad
        lxi d,sideFcb
        mvi c,16
        call bdos
        sta statusValue
        xra a
        sta sidecarOpen
        lda statusValue
        cpi 0ffh
        jnz sidecarClosedOk
        mvi a,7
        sta fileError
        jmp sidecarWriteBad
sidecarClosedOk:
        ora a
        ret
sidecarWriteCloseBad:
        call setWriteError
        lxi d,sideFcb
        mvi c,16
        call bdos
        xra a
        sta sidecarOpen
        stc
        ret
sidecarWriteBad:
        stc
        ret

finalizeCrcAtPadded:
        lxi h,paddedCrc
        shld crcPointer
        call finalizeCrc
        ret

initializeSideBuffer:
        lxi h,sideBuffer
        mvi b,128
initializeSideLoop:
        mvi m,01ah
        inx h
        dcr b
        jnz initializeSideLoop
        ret

cleanupGetFiles:
        lda workOpen
        ora a
        jz cleanupDeleteFiles
        call closeWorkFile
cleanupDeleteFiles:
        lda sidecarOpen
        ora a
        jz cleanupDeleteNames
        lxi d,sideFcb
        mvi c,16
        call bdos
        xra a
        sta sidecarOpen
cleanupDeleteNames:
        lda workCreated
        ora a
        jz cleanupSkipWork
        lxi d,workFcb
        mvi c,19
        call bdos
cleanupSkipWork:
        lda sidecarCreated
        ora a
        jz cleanupSkipSide
        lxi d,sideFcb
        mvi c,19
        call bdos
cleanupSkipSide:
        xra a
        sta workOpen
        sta workCreated
        sta sidecarOpen
        sta sidecarCreated
        ret

openWorkFcb:
        lxi h,workFcb
        jmp openFcb
openSideFcb:
        lxi h,sideFcb
openFcb:
        push h
        lxi d,12
        dad d
        xra a
        mov m,a
        inx h
        mov m,a
        inx h
        mov m,a
        pop h
        push h
        lxi d,32
        dad d
        xra a
        mov m,a
        pop d
        mvi c,15
        jmp bdos

closeWorkFile:
        lxi d,workFcb
        mvi c,16
        call bdos
        sta statusValue
        xra a
        sta workOpen
        lda statusValue
        ret

abortHostForFile:
        lda wildcardActive
        ora a
        jz abortHost
        mvi a,1
        out portAbort
        ret

commandPutWildcard:
        xra a
        sta wildcardCount
        sta wildcardIndex
        lxi h,workFcb
        lxi d,searchFcb
        lxi b,36
        call copyBlock
        lxi d,recordBuffer
        mvi c,26
        call bdos
        lxi d,searchFcb
        mvi c,17
        call bdos
putWildcardSearchResult:
        cpi 0ffh
        jz putWildcardCollected
        sta searchResult
        mov l,a
        mvi h,0
        dad h
        dad h
        dad h
        dad h
        dad h
        lxi d,recordBuffer+1
        dad d
        lxi d,candidateName
        mvi b,11
putWildcardSaveName:
        mov a,m
        ani 07fh
        stax d
        inx h
        inx d
        dcr b
        jnz putWildcardSaveName
        lda candidateName+8
        cpi 'H'
        jnz putWildcardCheckDuplicate
        lda candidateName+9
        cpi 'S'
        jnz putWildcardCheckDuplicate
        lda candidateName+10
        cpi 'T'
        jnz putWildcardCheckDuplicate
        lxi h,candidateName
        lxi d,workFcb+1
        lxi b,11
        call copyBlock
        mvi a,17
        sta fileError
        call printFileFailure
        jmp putWildcardSearchNext
putWildcardCheckDuplicate:
        lda wildcardCount
        mov b,a
        lxi h,fileList
putWildcardDuplicateLoop:
        mov a,b
        ora a
        jz putWildcardAddName
        push b
        push h
        lxi d,candidateName
        mvi c,11
putWildcardCompareName:
        ldax d
        cmp m
        jnz putWildcardDuplicateNo
        inx d
        inx h
        dcr c
        jnz putWildcardCompareName
        pop h
        pop b
        jmp putWildcardSearchNext
putWildcardDuplicateNo:
        pop h
        pop b
        lxi d,11
        dad d
        dcr b
        jmp putWildcardDuplicateLoop
putWildcardAddName:
        lda wildcardCount
        cpi 128
        jnc putWildcardListFull
        mov b,a
        lxi h,fileList
putWildcardFindListEnd:
        mov a,b
        ora a
        jz putWildcardCopyName
        lxi d,11
        dad d
        dcr b
        jmp putWildcardFindListEnd
putWildcardCopyName:
        xchg
        lxi h,candidateName
        mvi b,11
putWildcardStoreName:
        mov a,m
        stax d
        inx h
        inx d
        dcr b
        jnz putWildcardStoreName
        lda wildcardCount
        inr a
        sta wildcardCount
        jmp putWildcardSearchNext
putWildcardListFull:
        lxi h,candidateName
        lxi d,workFcb+1
        lxi b,11
        call copyBlock
        mvi a,18
        sta fileError
        call printFileFailure
        jmp putWildcardSearchNext
putWildcardSearchNext:
        lxi d,searchFcb
        mvi c,18
        call bdos
        jmp putWildcardSearchResult
putWildcardCollected:
        lda wildcardCount
        ora a
        jnz putWildcardLoadFile
        mvi a,16
        sta fileError
        call printFileFailure
        jmp terminate
putWildcardLoadFile:
        lda wildcardIndex
        mov b,a
        lxi h,fileList
putWildcardFindFile:
        mov a,b
        ora a
        jz putWildcardFileReady
        lxi d,11
        dad d
        dcr b
        jmp putWildcardFindFile
putWildcardFileReady:
        lxi d,workFcb+1
        mvi b,11
putWildcardCopyFile:
        mov a,m
        stax d
        inx h
        inx d
        dcr b
        jnz putWildcardCopyFile
        lxi h,workFcb+12
        mvi b,24
        xra a
putWildcardClearFcb:
        mov m,a
        inx h
        dcr b
        jnz putWildcardClearFcb
        call initSideFcb
        jmp commandPutSingle
putWildcardNext:
        lda wildcardIndex
        inr a
        sta wildcardIndex
        mov b,a
        lda wildcardCount
        cmp b
        jnz putWildcardLoadFile
        lxi d,msgPutDone
        call printString
        jmp terminate

commandPut:
commandPutSingle:
        xra a
        sta fileError
        sta paddedNote
        sta dotCounter
        sta dotsPrinted
        call printFileStart
        call scanSourceFile
        jnc putSourceReady
        mvi a,7
        sta fileError
        jmp putFileFailure
putSourceReady:
        call loadAndValidateSidecar
        call choosePutTransfer
        lda overwriteFlag
        ora a
        mvi a,3
        jz putSendCommand
        mvi a,4
putSendCommand:
        call sendByte
        jc putTransferFailed
        lxi h,workFcb+1
        mvi b,11
putSendName:
        mov a,m
        ani 07fh
        call sendByte
        jc putTransferFailed
        inx h
        dcr b
        jnz putSendName
        lxi h,putLength
        mvi b,4
putSendLength:
        mov a,m
        call sendByte
        jc putTransferFailed
        inx h
        dcr b
        jnz putSendLength
        lxi h,expectedCrc
        mvi b,4
putSendCrc:
        mov a,m
        call sendByte
        jc putTransferFailed
        inx h
        dcr b
        jnz putSendCrc
        call readByte
        jc putTransferFailed
        ora a
        jz putAccepted
        adi 10
        sta fileError
        jmp putFileFailure
putAccepted:
        call openWorkFcb
        cpi 0ffh
        jnz putFileOpened
        mvi a,7
        sta fileError
        call abortHost
        jmp putFileFailure
putFileOpened:
        mvi a,1
        sta workOpen
        lxi h,putLength
        lxi d,remaining
        lxi b,4
        call copyBlock
        lxi d,recordBuffer
        mvi c,26
        call bdos
putReadRecord:
        call remainingIsZero
        jz putPayloadDone
        lxi d,workFcb
        mvi c,20
        call bdos
        ora a
        jnz putFailed
        call progressTick
        lxi h,recordBuffer
        mvi b,128
putSendRecord:
        push b
        call remainingIsZero
        pop b
        jz putPayloadDone
        mov a,m
        call sendByte
        jc putFailed
        inx h
        call decrementRemaining
        dcr b
        jnz putSendRecord
        jmp putReadRecord
putPayloadDone:
        call readByte
        jc putFailed
        ora a
        jz putHostTransferComplete
        adi 10
        sta fileError
        jmp putHostFailure
putHostTransferComplete:
        call closeWorkFile
        cpi 0ffh
        jz putLocalCloseFailed
        lda wildcardActive
        ora a
        jz putSingleComplete
        call printFileSuccess
        jmp putWildcardNext
putSingleComplete:
        call endProgress
        lxi d,msgPutDone
        call printString
        jmp terminate

putLocalCloseFailed:
        xra a
        sta workOpen
        mvi a,7
        sta fileError
        jmp putFileFailure

putHostFailure:
        lda workOpen
        ora a
        jz putFileFailure
        call closeWorkFile
        jmp putFileFailure

putFailed:
        call abortHost
        mvi a,6
        sta fileError
        lda workOpen
        ora a
        jz putFailedMessage
        call closeWorkFile
putFailedMessage:
        lda abortFlag
        ora a
        jnz putUserAbort
        jmp putFileFailure

putTransferFailed:
        mvi a,6
        sta fileError
        call abortHost
        lda abortFlag
        ora a
        jnz putUserAbort
        jmp putFileFailure

putFileFailure:
        call printFileFailure
        lda abortFlag
        ora a
        jnz putUserAbort
        lda wildcardActive
        ora a
        jz terminate
        jmp putWildcardNext

putUserAbort:
        call printFileFailure
        lxi d,msgAborted
        call printString
        jmp terminate

scanSourceFile:
        xra a
        sta fileBytes
        sta fileBytes+1
        sta fileBytes+2
        sta fileBytes+3
        lxi h,paddedCrc
        shld crcPointer
        call initCrc
        call openWorkFcb
        cpi 0ffh
        jz scanSourceBad
        lxi d,recordBuffer
        mvi c,26
        call bdos
scanSourceRecord:
        lxi d,workFcb
        mvi c,20
        call bdos
        cpi 1
        jz scanSourceDone
        ora a
        jnz scanSourceCloseBad
        lxi h,recordBuffer
        shld crcDataPointer
        mvi a,128
        sta crcByteCount
        lxi h,paddedCrc
        shld crcPointer
        call crcBuffer
        call addFileBytes128
        jmp scanSourceRecord
scanSourceDone:
        lxi d,workFcb
        mvi c,16
        call bdos
        cpi 0ffh
        jz scanSourceBad
        lxi h,paddedCrc
        shld crcPointer
        call finalizeCrc
        lxi h,paddedCrc
        lxi d,scanPaddedCrc
        lxi b,4
        call copyBlock
        ora a
        ret
scanSourceCloseBad:
        lxi d,workFcb
        mvi c,16
        call bdos
scanSourceBad:
        stc
        ret

addFileBytes128:
        lxi h,fileBytes
        mov a,m
        adi 128
        mov m,a
        rnc
        inx h
        mov a,m
        inr a
        mov m,a
        rnz
        inx h
        mov a,m
        inr a
        mov m,a
        rnz
        inx h
        mov a,m
        inr a
        mov m,a
        ret

loadAndValidateSidecar:
        xra a
        sta sidecarValid
        call openSideFcb
        cpi 0ffh
        rz
        lxi d,sideBuffer
        mvi c,26
        call bdos
        lxi d,sideFcb
        mvi c,20
        call bdos
        ora a
        jnz sidecarInvalidClose
        lxi d,sideFcb
        mvi c,20
        call bdos
        cpi 1
        jnz sidecarInvalidClose
        lxi d,sideFcb
        mvi c,16
        call bdos
        cpi 0ffh
        rz
        call validateSideMagic
        rc
        call validateSideLength
        rc
        call compareSidePaddedCrc
        rc
        call validateSideExactCrc
        rc
        mvi a,1
        sta sidecarValid
        ret
sidecarInvalidClose:
        lxi d,sideFcb
        mvi c,16
        call bdos
        ret

validateSideMagic:
        lxi h,sideBuffer
        lxi d,metadataMagic
        mvi b,4
sideMagicLoop:
        ldax d
        cmp m
        jnz sideMagicBad
        inx h
        inx d
        dcr b
        jnz sideMagicLoop
        ora a
        ret
sideMagicBad:
        stc
        ret

validateSideLength:
        lxi h,sideBuffer+4
        lxi d,metaLength
        lxi b,4
        call copyBlock
        lxi h,sideBuffer+8
        lxi d,metaPaddedCrc
        lxi b,4
        call copyBlock
        lxi h,sideBuffer+12
        lxi d,metaExactCrc
        lxi b,4
        call copyBlock
        lxi h,metaLength
        lxi d,roundedLength
        lxi b,4
        call copyBlock
        lxi h,roundedLength
        mov a,m
        adi 127
        mov m,a
        inx h
        mov a,m
        aci 0
        mov m,a
        inx h
        mov a,m
        aci 0
        mov m,a
        inx h
        mov a,m
        aci 0
        mov m,a
        jc sideLengthBad
        lda roundedLength
        ani 080h
        sta roundedLength
        lxi h,roundedLength
        lxi d,fileBytes
        mvi b,4
sideLengthCompare:
        ldax d
        cmp m
        jnz sideLengthBad
        inx h
        inx d
        dcr b
        jnz sideLengthCompare
        ora a
        ret
sideLengthBad:
        stc
        ret

compareSidePaddedCrc:
        lxi h,metaPaddedCrc
        lxi d,scanPaddedCrc
        mvi b,4
sidePaddedCompare:
        ldax d
        cmp m
        jnz sidePaddedBad
        inx h
        inx d
        dcr b
        jnz sidePaddedCompare
        ora a
        ret
sidePaddedBad:
        stc
        ret

validateSideExactCrc:
        lxi h,metaLength
        lxi d,remaining
        lxi b,4
        call copyBlock
        lxi h,exactCrc
        shld crcPointer
        call initCrc
        call openWorkFcb
        cpi 0ffh
        jz sideExactBad
        lxi d,recordBuffer
        mvi c,26
        call bdos
        xra a
        sta recordPosition
sideExactNextRecord:
        call remainingIsZero
        jz sideExactComplete
        lxi d,workFcb
        mvi c,20
        call bdos
        ora a
        jnz sideExactCloseBad
        xra a
        sta recordPosition
sideExactByte:
        call remainingIsZero
        jz sideExactNextRecord
        lda recordPosition
        mov e,a
        mvi d,0
        lxi h,recordBuffer
        dad d
        mov a,m
        sta crcInput
        lxi h,exactCrc
        shld crcPointer
        lda crcInput
        call crcUpdate
        call decrementRemaining
        lda recordPosition
        inr a
        sta recordPosition
        cpi 128
        jz sideExactNextRecord
        jmp sideExactByte
sideExactComplete:
        lxi d,workFcb
        mvi c,16
        call bdos
        cpi 0ffh
        jz sideExactBad
        lxi h,exactCrc
        shld crcPointer
        call finalizeCrc
        lxi h,exactCrc
        lxi d,metaExactCrc
        mvi b,4
sideExactCompare:
        ldax d
        cmp m
        jnz sideExactMismatch
        inx h
        inx d
        dcr b
        jnz sideExactCompare
        ora a
        ret
sideExactMismatch:
        stc
        ret
sideExactCloseBad:
        lxi d,workFcb
        mvi c,16
        call bdos
sideExactBad:
        stc
        ret

choosePutTransfer:
        xra a
        sta paddedNote
        lxi h,fileBytes
        lxi d,putLength
        lxi b,4
        call copyBlock
        lxi h,scanPaddedCrc
        lxi d,expectedCrc
        lxi b,4
        call copyBlock
        lda sidecarValid
        ora a
        jnz choosePutMetadata
        mvi a,1
        sta paddedNote
        ret
choosePutMetadata:
        lxi h,metaLength
        lxi d,putLength
        lxi b,4
        call copyBlock
        lxi h,metaExactCrc
        lxi d,expectedCrc
        lxi b,4
        call copyBlock
        ret

streamPutPayload:
        lxi h,putLength
        lxi d,remaining
        lxi b,4
        call copyBlock
        xra a
        sta recordPosition
        call openWorkFcb
        cpi 0ffh
        jz streamPutBad
        lxi d,recordBuffer
        mvi c,26
        call bdos
streamPutNext:
        call remainingIsZero
        jz streamPutDone
        lda recordPosition
        ora a
        jnz streamPutByte
        lxi d,workFcb
        mvi c,20
        call bdos
        ora a
        jnz streamPutCloseBad
        mvi a,128
        sta recordPosition
        lxi h,recordBuffer
        shld streamPointer
streamPutByte:
        lhld streamPointer
        mov a,m
        call sendByte
        jc streamPutCloseBad
        lhld streamPointer
        inx h
        shld streamPointer
        lda recordPosition
        dcr a
        sta recordPosition
        call decrementRemaining
        jmp streamPutNext
streamPutDone:
        lxi d,workFcb
        mvi c,16
        call bdos
        cpi 0ffh
        jz streamPutBad
        ora a
        ret
streamPutCloseBad:
        lxi d,workFcb
        mvi c,16
        call bdos
streamPutBad:
        stc
        ret

readByte:
        call pollConsoleAbort
        lda abortFlag
        ora a
        jnz portAborted
        in portData
        ora a
        ret

sendByte:
        push psw
        call pollConsoleAbort
        lda abortFlag
        ora a
        jnz sendWasAborted
        pop psw
        out portData
        ora a
        ret
sendWasAborted:
        pop psw
portAborted:
        stc
        ret

pollConsoleAbort:
        push b
        push d
        push h
        mvi c,11
        call bdos
        ora a
        jz pollAbortDone
        mvi c,1
        call bdos
        cpi 03h
        jnz pollAbortDone
        xra a
        out portAbort
        mvi a,1
        sta abortFlag
pollAbortDone:
        pop h
        pop d
        pop b
        ret

abortHost:
        xra a
        out portAbort
        ret

remainingIsZero:
        lda remaining
        mov b,a
        lda remaining+1
        ora b
        mov b,a
        lda remaining+2
        ora b
        mov b,a
        lda remaining+3
        ora b
        ret

decrementRemaining:
        lda remaining
        ora a
        jnz decrementRemainingLow
        mvi a,0ffh
        sta remaining
        lda remaining+1
        ora a
        jnz decrementRemainingHigh1
        mvi a,0ffh
        sta remaining+1
        lda remaining+2
        ora a
        jnz decrementRemainingHigh2
        mvi a,0ffh
        sta remaining+2
        lda remaining+3
        dcr a
        sta remaining+3
        ret
decrementRemainingLow:
        dcr a
        sta remaining
        ret
decrementRemainingHigh1:
        dcr a
        sta remaining+1
        ret
decrementRemainingHigh2:
        dcr a
        sta remaining+2
        ret

initCrc:
        lhld crcPointer
        mvi b,4
initCrcLoop:
        mvi m,0ffh
        inx h
        dcr b
        jnz initCrcLoop
        ret

crcUpdate:
        sta crcInput
        lhld crcPointer
        mov a,m
        mov c,a
        lda crcInput
        xra c
        mov m,a
        mvi b,8
crcBitLoop:
        lhld crcPointer
        inx h
        inx h
        inx h
        mov a,m
        ora a
        rar
        mov m,a
        dcx h
        mov a,m
        rar
        mov m,a
        dcx h
        mov a,m
        rar
        mov m,a
        dcx h
        mov a,m
        rar
        mov m,a
        jnc crcNoPolynomial
        lhld crcPointer
        mov a,m
        xri 020h
        mov m,a
        inx h
        mov a,m
        xri 083h
        mov m,a
        inx h
        mov a,m
        xri 0b8h
        mov m,a
        inx h
        mov a,m
        xri 0edh
        mov m,a
crcNoPolynomial:
        dcr b
        jnz crcBitLoop
        ret

crcBuffer:
        lda crcByteCount
        ora a
        rz
crcBufferLoop:
        lhld crcDataPointer
        mov a,m
        inx h
        shld crcDataPointer
        call crcUpdate
        lda crcByteCount
        dcr a
        sta crcByteCount
        jnz crcBufferLoop
        ret

finalizeCrc:
        lhld crcPointer
        mvi b,4
finalizeCrcLoop:
        mov a,m
        cma
        mov m,a
        inx h
        dcr b
        jnz finalizeCrcLoop
        ret

operationAborted:
        call abortHost
        lxi d,msgAborted
        call printString
        jmp terminate

userAreaRejected:
        lxi d,msgUserArea
        call printString
        jmp terminate

unsupportedService:
        lxi d,msgServiceError
        call printString
        jmp terminate

filenameRejected:
        lxi d,msgFilename
        call printString
        jmp terminate

localFileError:
        lxi d,msgLocalError
        call printString
        jmp terminate

showHostError:
        sta statusValue
        lxi d,msgHostError
        call printString
        lda statusValue
        adi '0'
        mov e,a
        mvi c,2
        call bdos
        call printCrLf
        jmp terminate

badCommand:
        lxi d,msgUsage
        call printString
        jmp terminate

printFileStart:
        lda commandKind
        cpi 2
        jnz printStartPut
        lxi d,msgGetFailurePrefix
        call printString
        jmp printStartName
printStartPut:
        lxi d,msgPutFailurePrefix
        call printString
printStartName:
        call printFilename
        lxi d,msgFailureSeparator
        call printString
        mvi a,1
        sta startPrinted
        ret

printFileFailure:
        call endProgress
        lda startPrinted
        ora a
        jz printFileFailureFull
        xra a
        sta startPrinted
        jmp printFailureReason
printFileFailureFull:
        lda commandKind
        cpi 2
        jnz printPutFailurePrefix
        lxi d,msgGetFailurePrefix
        call printString
        jmp printFailureFilename
printPutFailurePrefix:
        lxi d,msgPutFailurePrefix
        call printString
printFailureFilename:
        call printFilename
        lxi d,msgFailureSeparator
        call printString
printFailureReason:
        lda fileError
        cpi 1
        jz printReasonTargetExists
        cpi 2
        jz printReasonSidecarExists
        cpi 6
        jz printReasonTransfer
        cpi 7
        jz printReasonLocalIo
        cpi 11
        jz printReasonInvalid
        cpi 12
        jz printReasonUnavailable
        cpi 13
        jz printReasonHostExists
        cpi 14
        jz printReasonHostIo
        cpi 15
        jz printReasonBusy
        cpi 16
        jz printReasonNoMatches
        cpi 17
        jz printReasonReserved
        cpi 18
        jz printReasonListFull
        cpi 19
        jz printReasonDiskFull
        cpi 20
        jz printReasonDirFull
        lxi d,msgReasonUnknown
        jmp printReason
printReasonTargetExists:
        lxi d,msgReasonTargetExists
        jmp printReason
printReasonSidecarExists:
        lxi d,msgReasonSidecarExists
        jmp printReason
printReasonTransfer:
        lxi d,msgReasonTransfer
        jmp printReason
printReasonLocalIo:
        lxi d,msgReasonLocalIo
        jmp printReason
printReasonInvalid:
        lxi d,msgReasonInvalid
        jmp printReason
printReasonUnavailable:
        lxi d,msgReasonUnavailable
        jmp printReason
printReasonHostExists:
        lxi d,msgReasonHostExists
        jmp printReason
printReasonHostIo:
        lxi d,msgReasonHostIo
        jmp printReason
printReasonBusy:
        lxi d,msgReasonBusy
        jmp printReason
printReasonNoMatches:
        lxi d,msgReasonNoMatches
        jmp printReason
printReasonReserved:
        lxi d,msgReasonReserved
        jmp printReason
printReasonListFull:
        lxi d,msgReasonListFull
        jmp printReason
printReasonDiskFull:
        lxi d,msgReasonDiskFull
        jmp printReason
printReasonDirFull:
        lxi d,msgReasonDirFull
printReason:
        call printString
        ret

printFileSuccess:
        call endProgress
        lda startPrinted
        ora a
        jz printFileSuccessFull
        xra a
        sta startPrinted
        lda paddedNote
        ora a
        lxi d,msgCompleteOnly
        jz printCompleteLine
        lxi d,msgCompletePadded
printCompleteLine:
        call printString
        ret
printFileSuccessFull:
        lda commandKind
        cpi 2
        jnz printPutSuccessPrefix
        lxi d,msgGetFailurePrefix
        call printString
        jmp printSuccessFilename
printPutSuccessPrefix:
        lxi d,msgPutFailurePrefix
        call printString
printSuccessFilename:
        call printFilename
        lxi d,msgSuccessSeparator
        call printString
        ret

printFilename:
        push psw
        push b
        push d
        push h
        lxi h,workFcb+1
        mvi b,8
printFilenameBase:
        mov a,m
        inx h
        cpi ' '
        jz printFilenameExtension
        mov e,a
        push b
        push h
        mvi c,2
        call bdos
        pop h
        pop b
        dcr b
        jnz printFilenameBase
printFilenameExtension:
        lda workFcb+9
        cpi ' '
        jz printFilenameDone
        mvi e,'.'
        mvi c,2
        call bdos
        lxi h,workFcb+9
        mvi b,3
printFilenameExtensionLoop:
        mov a,m
        inx h
        cpi ' '
        jz printFilenameDone
        mov e,a
        push b
        push h
        mvi c,2
        call bdos
        pop h
        pop b
        dcr b
        jnz printFilenameExtensionLoop
printFilenameDone:
        pop h
        pop d
        pop b
        pop psw
        ret

printString:
        mvi c,9
        call bdos
        ret

printCrLf:
        mvi e,13
        mvi c,2
        call bdos
        mvi e,10
        mvi c,2
        call bdos
        ret

terminate:
        mvi c,0
        call bdos
        jmp terminate

compareHlDe:
        mov a,l
        cmp e
        rnz
        mov a,h
        cmp d
        ret
copyBlock:
        push psw
copyBlockLoop:
        mov a,m
        stax d
        inx h
        inx d
        dcx b
        mov a,b
        ora c
        jnz copyBlockLoop
        pop psw
        ret
msgUsage:
        db 'Usage: HOST DIR | HOST GET name.ext [O] | HOST PUT name.ext [O]',13,10,'$'
msgFilename:
        db 'Invalid or reserved filename.',13,10,'$'
msgUserArea:
        db 'HOST GET/PUT requires USER 0.',13,10,'$'
msgServiceError:
        db 'Host exchange service unavailable.',13,10,'$'
msgHostError:
        db 'Host exchange status: ','$'
msgLocalError:
        db 'Local file I/O error.',13,10,'$'
msgAborted:
        db 'Transfer aborted.',13,10,'$'
msgGetDone:
        db 'GET complete.',13,10,'$'
msgPutDone:
        db 'PUT complete.',13,10,'$'
msgCompletePadded:
        db 'complete (no valid HST metadata; complete records sent).',13,10,'$'
msgGetFailurePrefix:
        db 'GET ','$'
msgPutFailurePrefix:
        db 'PUT ','$'
msgFailureSeparator:
        db ': ','$'
msgSuccessSeparator:
        db ': complete.',13,10,'$'
msgCompleteOnly:
        db 'complete.',13,10,'$'
msgReasonTargetExists:
        db 'target file already exists.',13,10,'$'
msgReasonSidecarExists:
        db 'sidecar metadata file already exists.',13,10,'$'
msgReasonTransfer:
        db 'transfer failed (I/O or checksum error).',13,10,'$'
msgReasonLocalIo:
        db 'local CP/M-80 file I/O failed.',13,10,'$'
msgReasonInvalid:
        db 'invalid or reserved filename.',13,10,'$'
msgReasonUnavailable:
        db 'exchange file is missing or unavailable.',13,10,'$'
msgReasonHostExists:
        db 'exchange destination already exists.',13,10,'$'
msgReasonHostIo:
        db 'exchange I/O or checksum error.',13,10,'$'
msgReasonBusy:
        db 'exchange service is busy.',13,10,'$'
msgReasonNoMatches:
        db 'no matching files.',13,10,'$'
msgReasonReserved:
        db 'reserved .HST metadata file; not transferred.',13,10,'$'
msgReasonListFull:
        db 'wildcard match list is full; not transferred.',13,10,'$'
msgReasonDiskFull:
        db 'disk full; the file does not fit on the CP/M drive.',13,10,'$'
msgReasonDirFull:
        db 'directory full or file cannot be created.',13,10,'$'
msgReasonUnknown:
        db 'unspecified error.',13,10,'$'
metadataMagic:
        db 'HST1'

commandKind:
        db 0
wildcardActive:
        db 0
startPrinted:
        db 0
paddedNote:
        db 0
fileError:
        db 0
wildcardCount:
        db 0
wildcardIndex:
        db 0
searchResult:
        db 0
hostNameLength:
        db 0
hostNameOverflow:
        db 0
hostNameExtension:
        db 0
hostSourcePointer:
        dw 0
candidatePosition:
        db 0
candidateBaseLength:
        db 0
abortFlag:
        db 0
overwriteFlag:
        db 0
dirNameActive:
        db 0
dotCounter:
        db 0
dotsPrinted:
        db 0
charValue:
        db 0
recordCount:
        db 0
recordPosition:
        db 0
crcByteCount:
        db 0
sidecarValid:
        db 0
workOpen:
        db 0
statusValue:
        db 0
sidecarOpen:
        db 0
workCreated:
        db 0
sidecarCreated:
        db 0
crcInput:
        db 0
tailEndPtr:
        dw 0
crcPointer:
        dw 0
crcDataPointer:
        dw 0
streamPointer:
        dw 0

getLength:
        db 0,0,0,0
remaining:
        db 0,0,0,0
expectedCrc:
        db 0,0,0,0
exactCrc:
        db 0,0,0,0
paddedCrc:
        db 0,0,0,0
scanPaddedCrc:
        db 0,0,0,0
fileBytes:
        db 0,0,0,0
metaLength:
        db 0,0,0,0
metaPaddedCrc:
        db 0,0,0,0
metaExactCrc:
        db 0,0,0,0
putLength:
        db 0,0,0,0
roundedLength:
        db 0,0,0,0
workFcb:
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
searchFcb:
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
sideFcb:
        db 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
patternFcb:
        db 0,0,0,0,0,0,0,0,0,0,0
candidateName:
        db 0,0,0,0,0,0,0,0,0,0,0
hostFilename:
        db 0,0,0,0,0,0,0,0,0,0,0,0,0
fileList:
        ds 1408
recordBuffer:
        ds 128
sideBuffer:
        ds 128
        end

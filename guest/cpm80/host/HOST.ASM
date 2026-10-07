; HOST.COM - CP/M-80 host exchange utility
; Build from this directory with:
;   z80asm -o HOST.COM HOST.ASM
; The resulting raw COM image starts at 0100h.
;
; Exchange ABI: immediate byte IN/OUT on F8h; abort is OUT (F9h),00h.
; Host commands: 1 DIR, 2 GET, 3 PUT. GET and PUT accept CP/M wildcards.

        org     0100h

bdos:           equ     0005h
tailLength:     equ     0080h
tailText:       equ     0081h
ccpFcb:         equ     005ch
portData:       equ     0f8h
portAbort:      equ     0f9h

start:
        ld      sp,0ffffh
        xor     a
        ld      (abortFlag),a
        ld      (workOpen),a
        ld      (workCreated),a
        ld      (sidecarOpen),a
        ld      (sidecarCreated),a
        call    parseCommand
        jp      c,badCommand
        ld      a,(commandKind)
        cp      1
        jp      z,commandDir
        ld      e,0ffh
        ld      c,32
        call    bdos
        or      a
        jp      nz,userAreaRejected
        call    copyAndValidateFcb
        jp      c,filenameRejected
        call    checkService
        jp      c,unsupportedService
        ld      a,(wildcardActive)
        or      a
        jp      z,startSingleCommand
        ld      a,(commandKind)
        cp      2
        jp      z,commandGetWildcard
        jp      commandPutWildcard
startSingleCommand:
        ld      a,(commandKind)
        cp      2
        jp      z,commandGet
        jp      commandPut

checkService:
        xor     a
        call    sendByte
        ret     c
        call    readByte
        ret     c
        or      a
        jp      nz,serviceCheckBad
        call    readByte
        ret     c
        cp      1
        jp      nz,serviceCheckBad
        call    readByte
        ret     c
        and     7
        cp      7
        jp      nz,serviceCheckBad
        or      a
        ret
serviceCheckBad:
        scf
        ret

parseCommand:
        ld      a,(tailLength)
        or      a
        jp      z,parseBad
        ld      e,a
        ld      d,0
        ld      hl,tailText
        add     hl,de
        ld      (tailEndPtr),hl
        ld      hl,tailText
parseSkipLeading:
        ld      de,(tailEndPtr)
        or      a
        sbc     hl,de
        jp      z,parseBad
        add     hl,de
        ld      a,(hl)
        cp      ' '
        jp      nz,parseAction
        inc     hl
        jp      parseSkipLeading
parseAction:
        ld      a,(hl)
        call    uppercaseCharacter
        cp      'D'
        jp      z,parseDirToken
        cp      'G'
        jp      z,parseGetToken
        cp      'P'
        jp      z,parsePutToken
        jp      parseBad
parseDirToken:
        inc     hl
        ld      a,(hl)
        call    uppercaseCharacter
        cp      'I'
        jp      nz,parseBad
        inc     hl
        ld      a,(hl)
        call    uppercaseCharacter
        cp      'R'
        jp      nz,parseBad
        inc     hl
        ld      a,1
        ld      (commandKind),a
        call    checkTokenBoundary
        jp      c,parseBad
        call    skipSpaces
        jp      nc,parseBad
        jp      parseGood
parseGetToken:
        inc     hl
        ld      a,(hl)
        call    uppercaseCharacter
        cp      'E'
        jp      nz,parseBad
        inc     hl
        ld      a,(hl)
        call    uppercaseCharacter
        cp      'T'
        jp      nz,parseBad
        inc     hl
        ld      a,2
        ld      (commandKind),a
        call    checkTokenBoundary
        jp      c,parseBad
        call    skipSpaces
        jp      c,parseBad
        call    checkFilenameArgument
        jp      c,parseBad
        jp      parseGood
parsePutToken:
        inc     hl
        ld      a,(hl)
        call    uppercaseCharacter
        cp      'U'
        jp      nz,parseBad
        inc     hl
        ld      a,(hl)
        call    uppercaseCharacter
        cp      'T'
        jp      nz,parseBad
        inc     hl
        ld      a,3
        ld      (commandKind),a
        call    checkTokenBoundary
        jp      c,parseBad
        call    skipSpaces
        jp      c,parseBad
        call    checkFilenameArgument
        jp      c,parseBad
parseGood:
        or      a
        ret
parseBad:
        scf
        ret

uppercaseCharacter:
        cp      'a'
        ret     c
        cp      'z'+1
        ret     nc
        sub     20h
        ret

checkTokenBoundary:
        ld      de,(tailEndPtr)
        or      a
        sbc     hl,de
        jp      z,tokenBoundaryAtEnd
        add     hl,de
        ld      a,(hl)
        cp      ' '
        jp      z,tokenBoundaryAfterSpace
        scf
        ret
tokenBoundaryAtEnd:
        add     hl,de
        or      a
        ret
tokenBoundaryAfterSpace:
        or      a
        ret

skipSpaces:
        ld      de,(tailEndPtr)
        or      a
        sbc     hl,de
        jp      z,skipSpacesEnd
        add     hl,de
        ld      a,(hl)
        cp      ' '
        jp      nz,skipSpacesDone
        inc     hl
        jp      skipSpaces
skipSpacesEnd:
        add     hl,de
        scf
        ret
skipSpacesDone:
        or      a
        ret

checkFilenameArgument:
        ld      de,(tailEndPtr)
        or      a
        sbc     hl,de
        jp      z,filenameArgumentBad
        add     hl,de
        ld      a,(hl)
        cp      ' '
        jp      z,filenameArgumentBad
filenameScan:
        ld      de,(tailEndPtr)
        or      a
        sbc     hl,de
        jp      z,filenameArgumentGood
        add     hl,de
        ld      a,(hl)
        cp      ' '
        jp      z,filenameTrailingSpaces
        inc     hl
        jp      filenameScan
filenameTrailingSpaces:
        call    skipSpaces
        jp      c,filenameArgumentGood
        scf
        ret
filenameArgumentGood:
        add     hl,de
        or      a
        ret
filenameArgumentBad:
        scf
        ret

copyAndValidateFcb:
        ld      hl,workFcb
        ld      b,36
        xor     a
clearWorkFcb:
        ld      (hl),a
        inc     hl
        djnz    clearWorkFcb
        ld      a,(ccpFcb+16)
        ld      (workFcb),a
        ld      hl,ccpFcb+17
        ld      de,workFcb+1
        ld      b,11
copyCcpName:
        ld      a,(hl)
        ld      (de),a
        inc     hl
        inc     de
        djnz    copyCcpName
        call    normalizeWildcards
        xor     a
        ld      (wildcardActive),a
        ld      hl,workFcb+1
        ld      b,8
        ld      c,0
        ld      d,0
validateBase:
        ld      a,(hl)
        cp      ' '
        jp      z,validateBaseSpace
        ld      a,d
        or      a
        jp      nz,invalidFcb
        inc     c
        ld      a,(hl)
        cp      '?'
        jp      z,validateBaseWildcard
        cp      '*'
        jp      z,validateBaseWildcard
        call    validFilenameCharacter
        jp      c,invalidFcb
        jp      validateBaseNext
validateBaseWildcard:
        ld      a,1
        ld      (wildcardActive),a
        jp      validateBaseNext
validateBaseSpace:
        ld      d,1
validateBaseNext:
        inc     hl
        djnz    validateBase
        ld      a,c
        or      a
        jp      z,invalidFcb
        ld      b,3
        xor     a
        ld      d,a
validateExtension:
        ld      a,(hl)
        cp      ' '
        jp      z,validateExtensionSpace
        ld      a,d
        or      a
        jp      nz,invalidFcb
        ld      a,(hl)
        cp      '?'
        jp      z,validateExtensionWildcard
        cp      '*'
        jp      z,validateExtensionWildcard
        call    validFilenameCharacter
        jp      c,invalidFcb
        jp      validateExtensionNext
validateExtensionWildcard:
        ld      a,1
        ld      (wildcardActive),a
        jp      validateExtensionNext
validateExtensionSpace:
        ld      d,1
validateExtensionNext:
        inc     hl
        djnz    validateExtension
        ld      a,(workFcb+9)
        cp      'H'
        jp      nz,copyInitSideFcb
        ld      a,(workFcb+10)
        cp      'S'
        jp      nz,copyInitSideFcb
        ld      a,(workFcb+11)
        cp      'T'
        jp      z,invalidFcb
copyInitSideFcb:
        call    initSideFcb
        or      a
        ret
invalidFcb:
        scf
        ret

normalizeWildcards:
        ld      hl,workFcb+1
        ld      b,8
normalizeBase:
        ld      a,(hl)
        cp      '*'
        jp      z,normalizeBaseFill
        inc     hl
        djnz    normalizeBase
        jp      normalizeExtensionStart
normalizeBaseFill:
        ld      (hl),'?'
        inc     hl
        djnz    normalizeBaseFill
normalizeExtensionStart:
        ld      hl,workFcb+9
        ld      b,3
normalizeExtension:
        ld      a,(hl)
        cp      '*'
        jp      z,normalizeExtensionFill
        inc     hl
        djnz    normalizeExtension
        ret
normalizeExtensionFill:
        ld      (hl),'?'
        inc     hl
        djnz    normalizeExtensionFill
        ret

validFilenameCharacter:
        cp      'A'
        jp      c,validFilenameDigit
        cp      'Z'+1
        jp      c,validFilenameGood
validFilenameDigit:
        cp      '0'
        jp      c,validFilenameSymbol
        cp      '9'+1
        jp      c,validFilenameGood
validFilenameSymbol:
        cp      '_'
        jp      z,validFilenameGood
        cp      '-'
        jp      z,validFilenameGood
        scf
        ret
validFilenameGood:
        or      a
        ret

initSideFcb:
        ld      hl,sideFcb
        ld      b,36
        xor     a
clearSideFcb:
        ld      (hl),a
        inc     hl
        djnz    clearSideFcb
        ld      hl,workFcb
        ld      de,sideFcb
        ld      bc,12
        ldir
        ld      a,'H'
        ld      (sideFcb+9),a
        ld      a,'S'
        ld      (sideFcb+10),a
        ld      a,'T'
        ld      (sideFcb+11),a
        ret

commandDir:
        ld      a,1
        call    sendByte
        jp      c,operationAborted
        call    readByte
        jp      c,operationAborted
        or      a
        jp      nz,showHostError
        xor     a
        ld      (dirNameActive),a
dirRead:
        call    readByte
        jp      c,operationAborted
        or      a
        jp      z,dirStringEnd
        ld      (charValue),a
        ld      a,1
        ld      (dirNameActive),a
        ld      a,(charValue)
        ld      e,a
        ld      c,2
        call    bdos
        jp      dirRead
dirStringEnd:
        ld      a,(dirNameActive)
        or      a
        jp      z,dirListEnd
        call    printCrLf
        xor     a
        ld      (dirNameActive),a
        jp      dirRead
dirListEnd:
        call    readByte
        jp      c,operationAborted
        or      a
        jp      nz,showHostError
        jp      terminate

commandGetWildcard:
        xor     a
        ld      (wildcardCount),a
        ld      hl,workFcb+1
        ld      de,patternFcb
        ld      bc,11
        ldir
        ld      a,1
        call    sendByte
        jp      c,operationAborted
        call    readByte
        jp      c,operationAborted
        or      a
        jp      nz,showHostError
getWildcardReadName:
        xor     a
        ld      (hostNameLength),a
        ld      (hostNameOverflow),a
getWildcardReadCharacter:
        call    readByte
        jp      c,operationAborted
        or      a
        jp      z,getWildcardNameEnd
        ld      (charValue),a
        ld      a,(hostNameLength)
        cp      12
        jp      nc,getWildcardNameOverflow
        ld      e,a
        ld      d,0
        ld      hl,hostFilename
        add     hl,de
        ld      a,(charValue)
        ld      (hl),a
        ld      a,(hostNameLength)
        inc     a
        ld      (hostNameLength),a
        jp      getWildcardReadCharacter
getWildcardNameOverflow:
        ld      a,1
        ld      (hostNameOverflow),a
        jp      getWildcardReadCharacter
getWildcardNameEnd:
        ld      a,(hostNameOverflow)
        or      a
        jp      nz,getWildcardReadName
        ld      a,(hostNameLength)
        or      a
        jp      z,getWildcardListEnd
        ld      e,a
        ld      d,0
        ld      hl,hostFilename
        add     hl,de
        xor     a
        ld      (hl),a
        call    convertHostName
        jp      c,getWildcardReadName
        ld      a,(candidateName+8)
        cp      'H'
        jp      nz,getWildcardCheckPattern
        ld      a,(candidateName+9)
        cp      'S'
        jp      nz,getWildcardCheckPattern
        ld      a,(candidateName+10)
        cp      'T'
        jp      nz,getWildcardCheckPattern
        ld      hl,candidateName
        ld      de,workFcb+1
        ld      bc,11
        ldir
        ld      a,17
        ld      (fileError),a
        call    printFileFailure
        jp      getWildcardReadName
getWildcardCheckPattern:
        call    matchHostName
        jp      c,getWildcardReadName
        ld      a,(wildcardCount)
        inc     a
        ld      (wildcardCount),a
        ld      hl,candidateName
        ld      de,workFcb+1
        ld      bc,11
        ldir
        ld      hl,workFcb+12
        ld      b,24
        xor     a
getWildcardClearFcb:
        ld      (hl),a
        inc     hl
        djnz    getWildcardClearFcb
        call    initSideFcb
        call    commandGetSingle
getWildcardListEnd:
        call    readByte
        jp      c,operationAborted
        or      a
        jp      nz,showHostError
        ld      a,(wildcardCount)
        or      a
        jp      nz,getWildcardFinished
        ld      a,16
        ld      (fileError),a
        call    printFileFailure
        jp      terminate
getWildcardFinished:
        ld      de,msgGetDone
        call    printString
        jp      terminate

convertHostName:
        ld      hl,candidateName
        ld      b,11
        ld      a,' '
convertHostClearCandidate:
        ld      (hl),a
        inc     hl
        djnz    convertHostClearCandidate
        xor     a
        ld      (hostNameExtension),a
        ld      (candidatePosition),a
        ld      (candidateBaseLength),a
        ld      hl,hostFilename
        ld      (hostSourcePointer),hl
convertHostCharacter:
        ld      hl,(hostSourcePointer)
        ld      a,(hl)
        or      a
        jp      z,convertHostDone
        inc     hl
        ld      (hostSourcePointer),hl
        cp      '.'
        jp      z,convertHostDot
        cp      'a'
        jp      c,convertHostUppercaseDone
        cp      'z'+1
        jp      nc,convertHostUppercaseDone
        sub     20h
convertHostUppercaseDone:
        call    validHostNameCharacter
        jp      c,convertHostInvalid
        ld      e,a
        ld      a,(hostNameExtension)
        or      a
        jp      nz,convertHostStoreExtension
        ld      a,(candidateBaseLength)
        cp      8
        jp      nc,convertHostInvalid
        inc     a
        ld      (candidateBaseLength),a
        ld      c,a
        dec     c
        ld      b,0
        ld      hl,candidateName
        add     hl,bc
        ld      a,e
        ld      (hl),a
        jp      convertHostCharacter
convertHostStoreExtension:
        ld      a,(candidatePosition)
        cp      11
        jp      nc,convertHostInvalid
        ld      c,a
        ld      b,0
        ld      hl,candidateName
        add     hl,bc
        ld      a,e
        ld      (hl),a
        ld      a,(candidatePosition)
        inc     a
        ld      (candidatePosition),a
        jp      convertHostCharacter
convertHostDot:
        ld      a,(hostNameExtension)
        or      a
        jp      nz,convertHostInvalid
        ld      a,(candidateBaseLength)
        or      a
        jp      z,convertHostInvalid
        ld      a,1
        ld      (hostNameExtension),a
        ld      a,8
        ld      (candidatePosition),a
        jp      convertHostCharacter
convertHostDone:
        ld      a,(candidateBaseLength)
        or      a
        jp      z,convertHostInvalid
        or      a
        ret
convertHostInvalid:
        scf
        ret

validHostNameCharacter:
        cp      'A'
        jp      c,validHostNameCheckDigit
        cp      'Z'+1
        jp      c,validHostNameGood
validHostNameCheckDigit:
        cp      '0'
        jp      c,validHostNameCheckSymbol
        cp      '9'+1
        jp      c,validHostNameGood
validHostNameCheckSymbol:
        cp      '_'
        jp      z,validHostNameGood
        cp      '-'
        jp      z,validHostNameGood
        scf
        ret
validHostNameGood:
        or      a
        ret

matchHostName:
        ld      hl,patternFcb
        ld      de,candidateName
        ld      b,11
matchHostNameByte:
        ld      a,(hl)
        cp      '?'
        jp      z,matchHostNameNext
        ld      a,(de)
        cp      (hl)
        jp      nz,matchHostNameBad
matchHostNameNext:
        inc     hl
        inc     de
        djnz    matchHostNameByte
        or      a
        ret
matchHostNameBad:
        scf
        ret

commandGet:
commandGetSingle:
        xor     a
        ld      (fileError),a
        call    printFileStart
        xor     a
        ld      (workOpen),a
        ld      (workCreated),a
        ld      (sidecarOpen),a
        ld      (sidecarCreated),a
        call    ensureTargetAbsent
        jp      nc,getTargetAvailable
        ld      a,1
        ld      (fileError),a
        jp      getFileFailure
getTargetAvailable:
        call    ensureSidecarAbsent
        jp      nc,getSidecarAvailable
        ld      a,2
        ld      (fileError),a
        jp      getFileFailure
getSidecarAvailable:
        ld      a,2
        call    sendByte
        jp      c,getTransferFailed
        ld      hl,workFcb+1
        ld      b,11
getSendName:
        ld      a,(hl)
        and     07fh
        call    sendByte
        jp      c,getTransferFailed
        inc     hl
        djnz    getSendName
        call    readByte
        jp      c,getTransferFailed
        or      a
        jp      z,getAccepted
        add     a,10
        ld      (fileError),a
        jp      getFileFailure
getAccepted:
        ld      hl,getLength
        ld      b,4
getReadLength:
        call    readByte
        jp      c,getTransferFailed
        ld      (hl),a
        inc     hl
        djnz    getReadLength
        call    copyGetLengthToRemaining
        call    initGetCrcs
        ld      de,workFcb
        ld      c,22
        call    bdos
        cp      0ffh
        jp      nz,getTargetCreated
        ld      a,7
        ld      (fileError),a
        jp      getLocalFailure
getTargetCreated:
        ld      a,1
        ld      (workOpen),a
        ld      (workCreated),a
        ld      de,recordBuffer
        ld      c,26
        call    bdos
        call    fillRecordBuffer
        xor     a
        ld      (recordCount),a
getPayloadLoop:
        call    remainingIsZero
        jp      z,getPayloadDone
        call    readByte
        jp      c,getFailure
        ld      (crcInput),a
        push    hl
        ld      hl,exactCrc
        ld      (crcPointer),hl
        ld      a,(crcInput)
        call    crcUpdate
        pop     hl
        ; The DMA buffer cursor is advanced independently of the CRC routine.
        ld      a,(recordCount)
        ld      e,a
        ld      d,0
        ld      hl,recordBuffer
        add     hl,de
        ld      a,(crcInput)
        ld      (hl),a
        call    decrementRemaining
        ld      a,(recordCount)
        inc     a
        ld      (recordCount),a
        cp      128
        jp      nz,getPayloadLoop
        call    writeGetRecord
        jp      c,getFailure
        xor     a
        ld      (recordCount),a
        call    fillRecordBuffer
        jp      getPayloadLoop
getPayloadDone:
        ld      a,(recordCount)
        or      a
        jp      z,getPayloadCrc
        call    writeGetRecord
        jp      c,getFailure
getPayloadCrc:
        ld      hl,exactCrc
        ld      (crcPointer),hl
        call    finalizeCrc
        call    readExpectedCrc
        jp      c,getFailure
        call    compareExactCrc
        jp      c,getFailure
        call    readByte
        jp      c,getFailure
        or      a
        jp      z,getFinalStatusOk
        add     a,10
        ld      (fileError),a
        jp      getFailure
getFinalStatusOk:
        call    closeWorkFile
        cp      0ffh
        jp      z,getFailure
        call    writeSidecar
        jp      c,getFailure
        ld      a,(wildcardActive)
        or      a
        jp      z,getSingleComplete
        call    printFileSuccess
        jp      getWildcardReadName
getSingleComplete:
        ld      de,msgGetDone
        call    printString
        jp      terminate

getFailure:
        ld      a,(fileError)
        or      a
        jp      nz,getFailureStatusReady
        ld      a,6
        ld      (fileError),a
getFailureStatusReady:
        call    abortHostForFile
        call    cleanupGetFiles
        ld      a,(abortFlag)
        or      a
        jp      nz,getUserAbort
        jp      getFileFailure

getTransferFailed:
        ld      a,6
        ld      (fileError),a
        call    abortHostForFile
        ld      a,(abortFlag)
        or      a
        jp      nz,getUserAbort
        jp      getFileFailure

getLocalFailure:
        call    abortHostForFile
        jp      getFileFailure

getFileFailure:
        call    printFileFailure
        ld      a,(wildcardActive)
        or      a
        jp      nz,getWildcardReadName
        jp      terminate

getUserAbort:
        call    abortHost
        call    printFileFailure
        ld      de,msgAborted
        call    printString
        jp      terminate

ensureTargetAbsent:
        call    openWorkFcb
        cp      0ffh
        jp      z,targetAbsent
        ld      de,workFcb
        ld      c,16
        call    bdos
        scf
        ret
targetAbsent:
        or      a
        ret

ensureSidecarAbsent:
        call    openSideFcb
        cp      0ffh
        jp      z,sidecarAbsent
        ld      de,sideFcb
        ld      c,16
        call    bdos
        scf
        ret
sidecarAbsent:
        or      a
        ret

copyGetLengthToRemaining:
        ld      hl,getLength
        ld      de,remaining
        ld      bc,4
        ldir
        ret

initGetCrcs:
        ld      hl,exactCrc
        ld      (crcPointer),hl
        call    initCrc
        ld      hl,paddedCrc
        ld      (crcPointer),hl
        call    initCrc
        ret

fillRecordBuffer:
        ld      hl,recordBuffer
        ld      b,128
fillRecordLoop:
        ld      (hl),01ah
        inc     hl
        djnz    fillRecordLoop
        ret

writeGetRecord:
        ld      hl,recordBuffer
        ld      (crcDataPointer),hl
        ld      a,128
        ld      (crcByteCount),a
        ld      hl,paddedCrc
        ld      (crcPointer),hl
        call    crcBuffer
        ld      de,workFcb
        ld      c,21
        call    bdos
        or      a
        ret     z
        scf
        ret

readExpectedCrc:
        ld      hl,expectedCrc
        ld      b,4
readExpectedLoop:
        call    readByte
        ret     c
        ld      (hl),a
        inc     hl
        djnz    readExpectedLoop
        or      a
        ret

compareExactCrc:
        ld      hl,exactCrc
        ld      de,expectedCrc
        ld      b,4
compareExactLoop:
        ld      a,(de)
        cp      (hl)
        jp      nz,compareExactBad
        inc     hl
        inc     de
        djnz    compareExactLoop
        or      a
        ret
compareExactBad:
        scf
        ret

writeSidecar:
        call    finalizeCrcAtPadded
        ld      hl,sideBuffer
        call    initializeSideBuffer
        ld      hl,metadataMagic
        ld      de,sideBuffer
        ld      bc,4
        ldir
        ld      hl,getLength
        ld      de,sideBuffer+4
        ld      bc,4
        ldir
        ld      hl,paddedCrc
        ld      de,sideBuffer+8
        ld      bc,4
        ldir
        ld      hl,exactCrc
        ld      de,sideBuffer+12
        ld      bc,4
        ldir
        ld      de,sideFcb
        ld      c,22
        call    bdos
        cp      0ffh
        jp      z,sidecarWriteBad
        ld      a,1
        ld      (sidecarOpen),a
        ld      (sidecarCreated),a
        ld      de,sideBuffer
        ld      c,26
        call    bdos
        ld      de,sideFcb
        ld      c,21
        call    bdos
        or      a
        jp      nz,sidecarWriteCloseBad
        ld      de,sideFcb
        ld      c,16
        call    bdos
        ld      (statusValue),a
        xor     a
        ld      (sidecarOpen),a
        ld      a,(statusValue)
        cp      0ffh
        jp      z,sidecarWriteBad
        or      a
        ret
sidecarWriteCloseBad:
        ld      de,sideFcb
        ld      c,16
        call    bdos
        xor     a
        ld      (sidecarOpen),a
        scf
        ret
sidecarWriteBad:
        scf
        ret

finalizeCrcAtPadded:
        ld      hl,paddedCrc
        ld      (crcPointer),hl
        call    finalizeCrc
        ret

initializeSideBuffer:
        ld      hl,sideBuffer
        ld      b,128
initializeSideLoop:
        ld      (hl),01ah
        inc     hl
        djnz    initializeSideLoop
        ret

cleanupGetFiles:
        ld      a,(workOpen)
        or      a
        jp      z,cleanupDeleteFiles
        call    closeWorkFile
cleanupDeleteFiles:
        ld      a,(sidecarOpen)
        or      a
        jp      z,cleanupDeleteNames
        ld      de,sideFcb
        ld      c,16
        call    bdos
        xor     a
        ld      (sidecarOpen),a
cleanupDeleteNames:
        ld      a,(workCreated)
        or      a
        jp      z,cleanupSkipWork
        ld      de,workFcb
        ld      c,19
        call    bdos
cleanupSkipWork:
        ld      a,(sidecarCreated)
        or      a
        jp      z,cleanupSkipSide
        ld      de,sideFcb
        ld      c,19
        call    bdos
cleanupSkipSide:
        xor     a
        ld      (workOpen),a
        ld      (workCreated),a
        ld      (sidecarOpen),a
        ld      (sidecarCreated),a
        ret

openWorkFcb:
        ld      hl,workFcb
        jp      openFcb
openSideFcb:
        ld      hl,sideFcb
openFcb:
        push    hl
        ld      de,12
        add     hl,de
        xor     a
        ld      (hl),a
        inc     hl
        ld      (hl),a
        inc     hl
        ld      (hl),a
        pop     hl
        push    hl
        ld      de,32
        add     hl,de
        xor     a
        ld      (hl),a
        pop     de
        ld      c,15
        jp      bdos

closeWorkFile:
        ld      de,workFcb
        ld      c,16
        call    bdos
        ld      (statusValue),a
        xor     a
        ld      (workOpen),a
        ld      a,(statusValue)
        ret

abortHostForFile:
        ld      a,(wildcardActive)
        or      a
        jp      z,abortHost
        ld      a,1
        out     (portAbort),a
        ret

commandPutWildcard:
        xor     a
        ld      (wildcardCount),a
        ld      (wildcardIndex),a
        ld      hl,workFcb
        ld      de,searchFcb
        ld      bc,36
        ldir
        ld      de,recordBuffer
        ld      c,26
        call    bdos
        ld      de,searchFcb
        ld      c,17
        call    bdos
putWildcardSearchResult:
        cp      0ffh
        jp      z,putWildcardCollected
        ld      (searchResult),a
        ld      l,a
        ld      h,0
        add     hl,hl
        add     hl,hl
        add     hl,hl
        add     hl,hl
        add     hl,hl
        ld      de,recordBuffer+1
        add     hl,de
        ld      de,candidateName
        ld      b,11
putWildcardSaveName:
        ld      a,(hl)
        and     07fh
        ld      (de),a
        inc     hl
        inc     de
        djnz    putWildcardSaveName
        ld      a,(candidateName+8)
        cp      'H'
        jp      nz,putWildcardCheckDuplicate
        ld      a,(candidateName+9)
        cp      'S'
        jp      nz,putWildcardCheckDuplicate
        ld      a,(candidateName+10)
        cp      'T'
        jp      nz,putWildcardCheckDuplicate
        ld      hl,candidateName
        ld      de,workFcb+1
        ld      bc,11
        ldir
        ld      a,17
        ld      (fileError),a
        call    printFileFailure
        jp      putWildcardSearchNext
putWildcardCheckDuplicate:
        ld      a,(wildcardCount)
        ld      b,a
        ld      hl,fileList
putWildcardDuplicateLoop:
        ld      a,b
        or      a
        jp      z,putWildcardAddName
        push    bc
        push    hl
        ld      de,candidateName
        ld      c,11
putWildcardCompareName:
        ld      a,(de)
        cp      (hl)
        jp      nz,putWildcardDuplicateNo
        inc     de
        inc     hl
        dec     c
        jp      nz,putWildcardCompareName
        pop     hl
        pop     bc
        jp      putWildcardSearchNext
putWildcardDuplicateNo:
        pop     hl
        pop     bc
        ld      de,11
        add     hl,de
        dec     b
        jp      putWildcardDuplicateLoop
putWildcardAddName:
        ld      a,(wildcardCount)
        cp      128
        jp      nc,putWildcardListFull
        ld      b,a
        ld      hl,fileList
putWildcardFindListEnd:
        ld      a,b
        or      a
        jp      z,putWildcardCopyName
        ld      de,11
        add     hl,de
        dec     b
        jp      putWildcardFindListEnd
putWildcardCopyName:
        ex      de,hl
        ld      hl,candidateName
        ld      b,11
putWildcardStoreName:
        ld      a,(hl)
        ld      (de),a
        inc     hl
        inc     de
        djnz    putWildcardStoreName
        ld      a,(wildcardCount)
        inc     a
        ld      (wildcardCount),a
        jp      putWildcardSearchNext
putWildcardListFull:
        ld      hl,candidateName
        ld      de,workFcb+1
        ld      bc,11
        ldir
        ld      a,18
        ld      (fileError),a
        call    printFileFailure
        jp      putWildcardSearchNext
putWildcardSearchNext:
        ld      de,searchFcb
        ld      c,18
        call    bdos
        jp      putWildcardSearchResult
putWildcardCollected:
        ld      a,(wildcardCount)
        or      a
        jp      nz,putWildcardLoadFile
        ld      a,16
        ld      (fileError),a
        call    printFileFailure
        jp      terminate
putWildcardLoadFile:
        ld      a,(wildcardIndex)
        ld      b,a
        ld      hl,fileList
putWildcardFindFile:
        ld      a,b
        or      a
        jp      z,putWildcardFileReady
        ld      de,11
        add     hl,de
        dec     b
        jp      putWildcardFindFile
putWildcardFileReady:
        ld      de,workFcb+1
        ld      b,11
putWildcardCopyFile:
        ld      a,(hl)
        ld      (de),a
        inc     hl
        inc     de
        djnz    putWildcardCopyFile
        ld      hl,workFcb+12
        ld      b,24
        xor     a
putWildcardClearFcb:
        ld      (hl),a
        inc     hl
        djnz    putWildcardClearFcb
        call    initSideFcb
        jp      commandPutSingle
putWildcardNext:
        ld      a,(wildcardIndex)
        inc     a
        ld      (wildcardIndex),a
        ld      b,a
        ld      a,(wildcardCount)
        cp      b
        jp      nz,putWildcardLoadFile
        ld      de,msgPutDone
        call    printString
        jp      terminate

commandPut:
commandPutSingle:
        xor     a
        ld      (fileError),a
        ld      (paddedNote),a
        call    printFileStart
        call    scanSourceFile
        jp      nc,putSourceReady
        ld      a,7
        ld      (fileError),a
        jp      putFileFailure
putSourceReady:
        call    loadAndValidateSidecar
        call    choosePutTransfer
        ld      a,3
        call    sendByte
        jp      c,putTransferFailed
        ld      hl,workFcb+1
        ld      b,11
putSendName:
        ld      a,(hl)
        and     07fh
        call    sendByte
        jp      c,putTransferFailed
        inc     hl
        djnz    putSendName
        ld      hl,putLength
        ld      b,4
putSendLength:
        ld      a,(hl)
        call    sendByte
        jp      c,putTransferFailed
        inc     hl
        djnz    putSendLength
        ld      hl,expectedCrc
        ld      b,4
putSendCrc:
        ld      a,(hl)
        call    sendByte
        jp      c,putTransferFailed
        inc     hl
        djnz    putSendCrc
        call    readByte
        jp      c,putTransferFailed
        or      a
        jp      z,putAccepted
        add     a,10
        ld      (fileError),a
        jp      putFileFailure
putAccepted:
        call    openWorkFcb
        cp      0ffh
        jp      nz,putFileOpened
        ld      a,7
        ld      (fileError),a
        call    abortHost
        jp      putFileFailure
putFileOpened:
        ld      a,1
        ld      (workOpen),a
        ld      hl,putLength
        ld      de,remaining
        ld      bc,4
        ldir
        ld      de,recordBuffer
        ld      c,26
        call    bdos
putReadRecord:
        call    remainingIsZero
        jp      z,putPayloadDone
        ld      de,workFcb
        ld      c,20
        call    bdos
        or      a
        jp      nz,putFailed
        ld      hl,recordBuffer
        ld      b,128
putSendRecord:
        push    bc
        call    remainingIsZero
        pop     bc
        jp      z,putPayloadDone
        ld      a,(hl)
        call    sendByte
        jp      c,putFailed
        inc     hl
        call    decrementRemaining
        djnz    putSendRecord
        jp      putReadRecord
putPayloadDone:
        call    readByte
        jp      c,putFailed
        or      a
        jp      z,putHostTransferComplete
        add     a,10
        ld      (fileError),a
        jp      putHostFailure
putHostTransferComplete:
        call    closeWorkFile
        cp      0ffh
        jp      z,putLocalCloseFailed
        ld      a,(wildcardActive)
        or      a
        jp      z,putSingleComplete
        call    printFileSuccess
        jp      putWildcardNext
putSingleComplete:
        ld      de,msgPutDone
        call    printString
        jp      terminate

putLocalCloseFailed:
        xor     a
        ld      (workOpen),a
        ld      a,7
        ld      (fileError),a
        jp      putFileFailure

putHostFailure:
        ld      a,(workOpen)
        or      a
        jp      z,putFileFailure
        call    closeWorkFile
        jp      putFileFailure

putFailed:
        call    abortHost
        ld      a,6
        ld      (fileError),a
        ld      a,(workOpen)
        or      a
        jp      z,putFailedMessage
        call    closeWorkFile
putFailedMessage:
        ld      a,(abortFlag)
        or      a
        jp      nz,putUserAbort
        jp      putFileFailure

putTransferFailed:
        ld      a,6
        ld      (fileError),a
        call    abortHost
        ld      a,(abortFlag)
        or      a
        jp      nz,putUserAbort
        jp      putFileFailure

putFileFailure:
        call    printFileFailure
        ld      a,(abortFlag)
        or      a
        jp      nz,putUserAbort
        ld      a,(wildcardActive)
        or      a
        jp      z,terminate
        jp      putWildcardNext

putUserAbort:
        call    printFileFailure
        ld      de,msgAborted
        call    printString
        jp      terminate

scanSourceFile:
        xor     a
        ld      (fileBytes),a
        ld      (fileBytes+1),a
        ld      (fileBytes+2),a
        ld      (fileBytes+3),a
        ld      hl,paddedCrc
        ld      (crcPointer),hl
        call    initCrc
        call    openWorkFcb
        cp      0ffh
        jp      z,scanSourceBad
        ld      de,recordBuffer
        ld      c,26
        call    bdos
scanSourceRecord:
        ld      de,workFcb
        ld      c,20
        call    bdos
        cp      1
        jp      z,scanSourceDone
        or      a
        jp      nz,scanSourceCloseBad
        ld      hl,recordBuffer
        ld      (crcDataPointer),hl
        ld      a,128
        ld      (crcByteCount),a
        ld      hl,paddedCrc
        ld      (crcPointer),hl
        call    crcBuffer
        call    addFileBytes128
        jp      scanSourceRecord
scanSourceDone:
        ld      de,workFcb
        ld      c,16
        call    bdos
        cp      0ffh
        jp      z,scanSourceBad
        ld      hl,paddedCrc
        ld      (crcPointer),hl
        call    finalizeCrc
        ld      hl,paddedCrc
        ld      de,scanPaddedCrc
        ld      bc,4
        ldir
        or      a
        ret
scanSourceCloseBad:
        ld      de,workFcb
        ld      c,16
        call    bdos
scanSourceBad:
        scf
        ret

addFileBytes128:
        ld      hl,fileBytes
        ld      a,(hl)
        add     a,128
        ld      (hl),a
        ret     nc
        inc     hl
        ld      a,(hl)
        inc     a
        ld      (hl),a
        ret     nz
        inc     hl
        ld      a,(hl)
        inc     a
        ld      (hl),a
        ret     nz
        inc     hl
        ld      a,(hl)
        inc     a
        ld      (hl),a
        ret

loadAndValidateSidecar:
        xor     a
        ld      (sidecarValid),a
        call    openSideFcb
        cp      0ffh
        ret     z
        ld      de,sideBuffer
        ld      c,26
        call    bdos
        ld      de,sideFcb
        ld      c,20
        call    bdos
        or      a
        jp      nz,sidecarInvalidClose
        ld      de,sideFcb
        ld      c,20
        call    bdos
        cp      1
        jp      nz,sidecarInvalidClose
        ld      de,sideFcb
        ld      c,16
        call    bdos
        cp      0ffh
        ret     z
        call    validateSideMagic
        ret     c
        call    validateSideLength
        ret     c
        call    compareSidePaddedCrc
        ret     c
        call    validateSideExactCrc
        ret     c
        ld      a,1
        ld      (sidecarValid),a
        ret
sidecarInvalidClose:
        ld      de,sideFcb
        ld      c,16
        call    bdos
        ret

validateSideMagic:
        ld      hl,sideBuffer
        ld      de,metadataMagic
        ld      b,4
sideMagicLoop:
        ld      a,(de)
        cp      (hl)
        jp      nz,sideMagicBad
        inc     hl
        inc     de
        djnz    sideMagicLoop
        or      a
        ret
sideMagicBad:
        scf
        ret

validateSideLength:
        ld      hl,sideBuffer+4
        ld      de,metaLength
        ld      bc,4
        ldir
        ld      hl,sideBuffer+8
        ld      de,metaPaddedCrc
        ld      bc,4
        ldir
        ld      hl,sideBuffer+12
        ld      de,metaExactCrc
        ld      bc,4
        ldir
        ld      hl,metaLength
        ld      de,roundedLength
        ld      bc,4
        ldir
        ld      hl,roundedLength
        ld      a,(hl)
        add     a,127
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        adc     a,0
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        adc     a,0
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        adc     a,0
        ld      (hl),a
        jp      c,sideLengthBad
        ld      a,(roundedLength)
        and     080h
        ld      (roundedLength),a
        ld      hl,roundedLength
        ld      de,fileBytes
        ld      b,4
sideLengthCompare:
        ld      a,(de)
        cp      (hl)
        jp      nz,sideLengthBad
        inc     hl
        inc     de
        djnz    sideLengthCompare
        or      a
        ret
sideLengthBad:
        scf
        ret

compareSidePaddedCrc:
        ld      hl,metaPaddedCrc
        ld      de,scanPaddedCrc
        ld      b,4
sidePaddedCompare:
        ld      a,(de)
        cp      (hl)
        jp      nz,sidePaddedBad
        inc     hl
        inc     de
        djnz    sidePaddedCompare
        or      a
        ret
sidePaddedBad:
        scf
        ret

validateSideExactCrc:
        ld      hl,metaLength
        ld      de,remaining
        ld      bc,4
        ldir
        ld      hl,exactCrc
        ld      (crcPointer),hl
        call    initCrc
        call    openWorkFcb
        cp      0ffh
        jp      z,sideExactBad
        ld      de,recordBuffer
        ld      c,26
        call    bdos
        xor     a
        ld      (recordPosition),a
sideExactNextRecord:
        call    remainingIsZero
        jp      z,sideExactComplete
        ld      de,workFcb
        ld      c,20
        call    bdos
        or      a
        jp      nz,sideExactCloseBad
        xor     a
        ld      (recordPosition),a
sideExactByte:
        call    remainingIsZero
        jp      z,sideExactNextRecord
        ld      a,(recordPosition)
        ld      e,a
        ld      d,0
        ld      hl,recordBuffer
        add     hl,de
        ld      a,(hl)
        ld      (crcInput),a
        ld      hl,exactCrc
        ld      (crcPointer),hl
        ld      a,(crcInput)
        call    crcUpdate
        call    decrementRemaining
        ld      a,(recordPosition)
        inc     a
        ld      (recordPosition),a
        cp      128
        jp      z,sideExactNextRecord
        jp      sideExactByte
sideExactComplete:
        ld      de,workFcb
        ld      c,16
        call    bdos
        cp      0ffh
        jp      z,sideExactBad
        ld      hl,exactCrc
        ld      (crcPointer),hl
        call    finalizeCrc
        ld      hl,exactCrc
        ld      de,metaExactCrc
        ld      b,4
sideExactCompare:
        ld      a,(de)
        cp      (hl)
        jp      nz,sideExactMismatch
        inc     hl
        inc     de
        djnz    sideExactCompare
        or      a
        ret
sideExactMismatch:
        scf
        ret
sideExactCloseBad:
        ld      de,workFcb
        ld      c,16
        call    bdos
sideExactBad:
        scf
        ret

choosePutTransfer:
        xor     a
        ld      (paddedNote),a
        ld      hl,fileBytes
        ld      de,putLength
        ld      bc,4
        ldir
        ld      hl,scanPaddedCrc
        ld      de,expectedCrc
        ld      bc,4
        ldir
        ld      a,(sidecarValid)
        or      a
        jp      nz,choosePutMetadata
        ld      a,1
        ld      (paddedNote),a
        ret
choosePutMetadata:
        ld      hl,metaLength
        ld      de,putLength
        ld      bc,4
        ldir
        ld      hl,metaExactCrc
        ld      de,expectedCrc
        ld      bc,4
        ldir
        ret

streamPutPayload:
        ld      hl,putLength
        ld      de,remaining
        ld      bc,4
        ldir
        xor     a
        ld      (recordPosition),a
        call    openWorkFcb
        cp      0ffh
        jp      z,streamPutBad
        ld      de,recordBuffer
        ld      c,26
        call    bdos
streamPutNext:
        call    remainingIsZero
        jp      z,streamPutDone
        ld      a,(recordPosition)
        or      a
        jp      nz,streamPutByte
        ld      de,workFcb
        ld      c,20
        call    bdos
        or      a
        jp      nz,streamPutCloseBad
        ld      a,128
        ld      (recordPosition),a
        ld      hl,recordBuffer
        ld      (streamPointer),hl
streamPutByte:
        ld      hl,(streamPointer)
        ld      a,(hl)
        call    sendByte
        jp      c,streamPutCloseBad
        ld      hl,(streamPointer)
        inc     hl
        ld      (streamPointer),hl
        ld      a,(recordPosition)
        dec     a
        ld      (recordPosition),a
        call    decrementRemaining
        jp      streamPutNext
streamPutDone:
        ld      de,workFcb
        ld      c,16
        call    bdos
        cp      0ffh
        jp      z,streamPutBad
        or      a
        ret
streamPutCloseBad:
        ld      de,workFcb
        ld      c,16
        call    bdos
streamPutBad:
        scf
        ret

readByte:
        call    pollConsoleAbort
        ld      a,(abortFlag)
        or      a
        jp      nz,portAborted
        in      a,(portData)
        or      a
        ret

sendByte:
        push    af
        call    pollConsoleAbort
        ld      a,(abortFlag)
        or      a
        jp      nz,sendWasAborted
        pop     af
        out     (portData),a
        or      a
        ret
sendWasAborted:
        pop     af
portAborted:
        scf
        ret

pollConsoleAbort:
        push    bc
        push    de
        push    hl
        ld      c,11
        call    bdos
        or      a
        jp      z,pollAbortDone
        ld      c,1
        call    bdos
        cp      03h
        jp      nz,pollAbortDone
        xor     a
        out     (portAbort),a
        ld      a,1
        ld      (abortFlag),a
pollAbortDone:
        pop     hl
        pop     de
        pop     bc
        ret

abortHost:
        xor     a
        out     (portAbort),a
        ret

remainingIsZero:
        ld      a,(remaining)
        ld      b,a
        ld      a,(remaining+1)
        or      b
        ld      b,a
        ld      a,(remaining+2)
        or      b
        ld      b,a
        ld      a,(remaining+3)
        or      b
        ret

decrementRemaining:
        ld      a,(remaining)
        or      a
        jp      nz,decrementRemainingLow
        ld      a,0ffh
        ld      (remaining),a
        ld      a,(remaining+1)
        or      a
        jp      nz,decrementRemainingHigh1
        ld      a,0ffh
        ld      (remaining+1),a
        ld      a,(remaining+2)
        or      a
        jp      nz,decrementRemainingHigh2
        ld      a,0ffh
        ld      (remaining+2),a
        ld      a,(remaining+3)
        dec     a
        ld      (remaining+3),a
        ret
decrementRemainingLow:
        dec     a
        ld      (remaining),a
        ret
decrementRemainingHigh1:
        dec     a
        ld      (remaining+1),a
        ret
decrementRemainingHigh2:
        dec     a
        ld      (remaining+2),a
        ret

initCrc:
        ld      hl,(crcPointer)
        ld      b,4
initCrcLoop:
        ld      (hl),0ffh
        inc     hl
        djnz    initCrcLoop
        ret

crcUpdate:
        ld      (crcInput),a
        ld      hl,(crcPointer)
        ld      a,(hl)
        ld      c,a
        ld      a,(crcInput)
        xor     c
        ld      (hl),a
        ld      b,8
crcBitLoop:
        ld      hl,(crcPointer)
        inc     hl
        inc     hl
        inc     hl
        ld      a,(hl)
        srl     a
        ld      (hl),a
        dec     hl
        ld      a,(hl)
        rr      a
        ld      (hl),a
        dec     hl
        ld      a,(hl)
        rr      a
        ld      (hl),a
        dec     hl
        ld      a,(hl)
        rr      a
        ld      (hl),a
        jp      nc,crcNoPolynomial
        ld      hl,(crcPointer)
        ld      a,(hl)
        xor     020h
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        xor     083h
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        xor     0b8h
        ld      (hl),a
        inc     hl
        ld      a,(hl)
        xor     0edh
        ld      (hl),a
crcNoPolynomial:
        djnz    crcBitLoop
        ret

crcBuffer:
        ld      a,(crcByteCount)
        or      a
        ret     z
crcBufferLoop:
        ld      hl,(crcDataPointer)
        ld      a,(hl)
        inc     hl
        ld      (crcDataPointer),hl
        call    crcUpdate
        ld      a,(crcByteCount)
        dec     a
        ld      (crcByteCount),a
        jp      nz,crcBufferLoop
        ret

finalizeCrc:
        ld      hl,(crcPointer)
        ld      b,4
finalizeCrcLoop:
        ld      a,(hl)
        cpl
        ld      (hl),a
        inc     hl
        djnz    finalizeCrcLoop
        ret

operationAborted:
        call    abortHost
        ld      de,msgAborted
        call    printString
        jp      terminate

userAreaRejected:
        ld      de,msgUserArea
        call    printString
        jp      terminate

unsupportedService:
        ld      de,msgServiceError
        call    printString
        jp      terminate

filenameRejected:
        ld      de,msgFilename
        call    printString
        jp      terminate

localFileError:
        ld      de,msgLocalError
        call    printString
        jp      terminate

showHostError:
        ld      (statusValue),a
        ld      de,msgHostError
        call    printString
        ld      a,(statusValue)
        add     a,'0'
        ld      e,a
        ld      c,2
        call    bdos
        call    printCrLf
        jp      terminate

badCommand:
        ld      de,msgUsage
        call    printString
        jp      terminate

printFileStart:
        ld      a,(commandKind)
        cp      2
        jp      nz,printStartPut
        ld      de,msgGetFailurePrefix
        call    printString
        jp      printStartName
printStartPut:
        ld      de,msgPutFailurePrefix
        call    printString
printStartName:
        call    printFilename
        ld      de,msgFailureSeparator
        call    printString
        ld      a,1
        ld      (startPrinted),a
        ret

printFileFailure:
        ld      a,(startPrinted)
        or      a
        jp      z,printFileFailureFull
        xor     a
        ld      (startPrinted),a
        jp      printFailureReason
printFileFailureFull:
        ld      a,(commandKind)
        cp      2
        jp      nz,printPutFailurePrefix
        ld      de,msgGetFailurePrefix
        call    printString
        jp      printFailureFilename
printPutFailurePrefix:
        ld      de,msgPutFailurePrefix
        call    printString
printFailureFilename:
        call    printFilename
        ld      de,msgFailureSeparator
        call    printString
printFailureReason:
        ld      a,(fileError)
        cp      1
        jp      z,printReasonTargetExists
        cp      2
        jp      z,printReasonSidecarExists
        cp      6
        jp      z,printReasonTransfer
        cp      7
        jp      z,printReasonLocalIo
        cp      11
        jp      z,printReasonInvalid
        cp      12
        jp      z,printReasonUnavailable
        cp      13
        jp      z,printReasonHostExists
        cp      14
        jp      z,printReasonHostIo
        cp      15
        jp      z,printReasonBusy
        cp      16
        jp      z,printReasonNoMatches
        cp      17
        jp      z,printReasonReserved
        cp      18
        jp      z,printReasonListFull
        ld      de,msgReasonUnknown
        jp      printReason
printReasonTargetExists:
        ld      de,msgReasonTargetExists
        jp      printReason
printReasonSidecarExists:
        ld      de,msgReasonSidecarExists
        jp      printReason
printReasonTransfer:
        ld      de,msgReasonTransfer
        jp      printReason
printReasonLocalIo:
        ld      de,msgReasonLocalIo
        jp      printReason
printReasonInvalid:
        ld      de,msgReasonInvalid
        jp      printReason
printReasonUnavailable:
        ld      de,msgReasonUnavailable
        jp      printReason
printReasonHostExists:
        ld      de,msgReasonHostExists
        jp      printReason
printReasonHostIo:
        ld      de,msgReasonHostIo
        jp      printReason
printReasonBusy:
        ld      de,msgReasonBusy
        jp      printReason
printReasonNoMatches:
        ld      de,msgReasonNoMatches
        jp      printReason
printReasonReserved:
        ld      de,msgReasonReserved
        jp      printReason
printReasonListFull:
        ld      de,msgReasonListFull
printReason:
        call    printString
        ret

printFileSuccess:
        ld      a,(startPrinted)
        or      a
        jp      z,printFileSuccessFull
        xor     a
        ld      (startPrinted),a
        ld      a,(paddedNote)
        or      a
        ld      de,msgCompleteOnly
        jp      z,printCompleteLine
        ld      de,msgCompletePadded
printCompleteLine:
        call    printString
        ret
printFileSuccessFull:
        ld      a,(commandKind)
        cp      2
        jp      nz,printPutSuccessPrefix
        ld      de,msgGetFailurePrefix
        call    printString
        jp      printSuccessFilename
printPutSuccessPrefix:
        ld      de,msgPutFailurePrefix
        call    printString
printSuccessFilename:
        call    printFilename
        ld      de,msgSuccessSeparator
        call    printString
        ret

printFilename:
        push    af
        push    bc
        push    de
        push    hl
        ld      hl,workFcb+1
        ld      b,8
printFilenameBase:
        ld      a,(hl)
        inc     hl
        cp      ' '
        jp      z,printFilenameExtension
        ld      e,a
        push    bc
        push    hl
        ld      c,2
        call    bdos
        pop     hl
        pop     bc
        djnz    printFilenameBase
printFilenameExtension:
        ld      a,(workFcb+9)
        cp      ' '
        jp      z,printFilenameDone
        ld      e,'.'
        ld      c,2
        call    bdos
        ld      hl,workFcb+9
        ld      b,3
printFilenameExtensionLoop:
        ld      a,(hl)
        inc     hl
        cp      ' '
        jp      z,printFilenameDone
        ld      e,a
        push    bc
        push    hl
        ld      c,2
        call    bdos
        pop     hl
        pop     bc
        djnz    printFilenameExtensionLoop
printFilenameDone:
        pop     hl
        pop     de
        pop     bc
        pop     af
        ret

printString:
        ld      c,9
        call    bdos
        ret

printCrLf:
        ld      e,13
        ld      c,2
        call    bdos
        ld      e,10
        ld      c,2
        call    bdos
        ret

terminate:
        ld      c,0
        call    bdos
        jp      terminate

msgUsage:
        db      "Usage: HOST DIR | HOST GET name.ext | HOST PUT name.ext",13,10,"$"
msgFilename:
        db      "Invalid or reserved filename.",13,10,"$"
msgUserArea:
        db      "HOST GET/PUT requires USER 0.",13,10,"$"
msgServiceError:
        db      "Host exchange service unavailable.",13,10,"$"
msgHostError:
        db      "Host exchange status: ","$"
msgLocalError:
        db      "Local file I/O error.",13,10,"$"
msgAborted:
        db      "Transfer aborted.",13,10,"$"
msgGetDone:
        db      "GET complete.",13,10,"$"
msgPutDone:
        db      "PUT complete.",13,10,"$"
msgCompletePadded:
        db      "complete (no valid HST metadata; complete records sent).",13,10,"$"
msgGetFailurePrefix:
        db      "GET ","$"
msgPutFailurePrefix:
        db      "PUT ","$"
msgFailureSeparator:
        db      ": ","$"
msgSuccessSeparator:
        db      ": complete.",13,10,"$"
msgCompleteOnly:
        db      "complete.",13,10,"$"
msgReasonTargetExists:
        db      "target file already exists.",13,10,"$"
msgReasonSidecarExists:
        db      "sidecar metadata file already exists.",13,10,"$"
msgReasonTransfer:
        db      "transfer failed (I/O or checksum error).",13,10,"$"
msgReasonLocalIo:
        db      "local CP/M-80 file I/O failed.",13,10,"$"
msgReasonInvalid:
        db      "invalid or reserved filename.",13,10,"$"
msgReasonUnavailable:
        db      "exchange file is missing or unavailable.",13,10,"$"
msgReasonHostExists:
        db      "exchange destination already exists.",13,10,"$"
msgReasonHostIo:
        db      "exchange I/O or checksum error.",13,10,"$"
msgReasonBusy:
        db      "exchange service is busy.",13,10,"$"
msgReasonNoMatches:
        db      "no matching files.",13,10,"$"
msgReasonReserved:
        db      "reserved .HST metadata file; not transferred.",13,10,"$"
msgReasonListFull:
        db      "wildcard match list is full; not transferred.",13,10,"$"
msgReasonUnknown:
        db      "unspecified error.",13,10,"$"
metadataMagic:
        db      "HST1"

commandKind:
        db      0
wildcardActive:
        db      0
startPrinted:
        db      0
paddedNote:
        db      0
fileError:
        db      0
wildcardCount:
        db      0
wildcardIndex:
        db      0
searchResult:
        db      0
hostNameLength:
        db      0
hostNameOverflow:
        db      0
hostNameExtension:
        db      0
hostSourcePointer:
        dw      0
candidatePosition:
        db      0
candidateBaseLength:
        db      0
abortFlag:
        db      0
dirNameActive:
        db      0
charValue:
        db      0
recordCount:
        db      0
recordPosition:
        db      0
crcByteCount:
        db      0
sidecarValid:
        db      0
workOpen:
        db      0
statusValue:
        db      0
sidecarOpen:
        db      0
workCreated:
        db      0
sidecarCreated:
        db      0
crcInput:
        db      0
tailEndPtr:
        dw      0
crcPointer:
        dw      0
crcDataPointer:
        dw      0
streamPointer:
        dw      0

getLength:
        ds      4,0
remaining:
        ds      4,0
expectedCrc:
        ds      4,0
exactCrc:
        ds      4,0
paddedCrc:
        ds      4,0
scanPaddedCrc:
        ds      4,0
fileBytes:
        ds      4,0
metaLength:
        ds      4,0
metaPaddedCrc:
        ds      4,0
metaExactCrc:
        ds      4,0
putLength:
        ds      4,0
roundedLength:
        ds      4,0
workFcb:
        ds      36,0
searchFcb:
        ds      36,0
sideFcb:
        ds      36,0
patternFcb:
        ds      11,0
candidateName:
        ds      11,0
hostFilename:
        ds      13,0
fileList:
        ds      1408,0
recordBuffer:
        ds      128,0
sideBuffer:
        ds      128,0

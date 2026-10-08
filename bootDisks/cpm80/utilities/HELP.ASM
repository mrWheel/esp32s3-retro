; HELP 3.3 - Intel 8080, Digital Research ASM 2.0, CP/M 2.2+
; By Willem Aandewiel
; HELP [file [topic ...]]  (current drive/user; optional A: prefix)
; With or without arguments: interactive HELP>FILE>TOPIC> prompt.
; Bare input selects a child. Enter goes up; Enter at HELP> exits.
; Entering a topic shows its text and children; going up lists children.
; .NAME tries a child, then a sibling of the last selected topic.
; Names: 12 characters, file basename: 8. Max 10 tokens.
; Exact topic matches take precedence over unique prefixes.
; ASCII ///1NAME and ///1 NAME; levels 1..9; CRLF, LF or CR.
; Indexed HLP: check entry layout; ignore unused level metadata.
; Legacy ///NAME without a digit is treated as a level-1 topic.
; NUL padding is accepted only if no further text follows it.
; Text lines: max 255 bytes. Overlong input is reported, not truncated.
; Rescans use physical line numbers, not non-unique marker strings.
; First root is default. An explicit root may precede subtopics.
; No Z80 instructions, macros, underscores or long identifiers.
        ORG     100H
BDOS    EQU     5
CR      EQU     13
LF      EQU     10
        LXI     SP,STKTOP
        LDA     80H
        MOV     B,A
        LXI     H,81H
        CALL    PARSE
        JC      BADCLI
        LDA     NTOK
        ORA     A
        JZ      INTER
        CALL    RUN
        MVI     A,1
        STA     INTERA
        JMP     LOOP
BADCLI: LXI     D,MSGIN
        CALL    PRINT
        JMP     EXIT
INTER:  MVI     A,1
        STA     INTERA
        LXI     D,BANNER
        CALL    PRINT
        CALL    LIST
LOOP:   XRA     A
        STA     UPONLY
        CALL    PRPATH
        LXI     D,INPUT
        MVI     C,10
        CALL    SYS
        CALL    NEWLN
        LDA     INPUT+1
        ORA     A
        JZ      BACK
        MOV     B,A
        LXI     H,INPUT+2
        CALL    PARSE
        JC      BADINT
        LDA     NTOK
        ORA     A
        JZ      LOOP
        CALL    RUN
        JMP     LOOP
; Empty input removes one visible path component.
BACK:   LDA     NKEEP
        ORA     A
        JZ      EXIT
        DCR     A
        JZ      TOP
        STA     NTOK
        LXI     H,KEEP
        LXI     D,TOKS
        MVI     B,130
        CALL    COPY
        MVI     A,1
        STA     UPONLY
        CALL    RUNABS
        JMP     LOOP
TOP:    STA     NKEEP
        LXI     D,MSGALL
        CALL    PRINT
        CALL    LIST
        JMP     LOOP
; The committed display path is separate from typed abbreviations.
PRPATH: LXI     D,PROMPT
        CALL    PRINT
        LDA     NKEEP
        ORA     A
        RZ
        MOV     B,A
        LXI     H,DPATH
PRP1:   PUSH    H
        CALL    PSTR
        MVI     A,'>'
        CALL    PUTC
        POP     H
        LXI     D,13
        DAD     D
        DCR     B
        JNZ     PRP1
        RET
BADINT: LXI     D,MSGIN
        CALL    PRINT
        JMP     LOOP
EXIT:   JMP     0
; SYS preserves all register pairs; A and flags are BDOS results.
SYS:    PUSH    B
        PUSH    D
        PUSH    H
        CALL    BDOS
        POP     H
        POP     D
        POP     B
        RET
PRINT:  PUSH    B
        MVI     C,9
        CALL    SYS
        POP     B
        RET
PUTC:   PUSH    B
        PUSH    D
        MOV     E,A
        MVI     C,2
        CALL    SYS
        POP     D
        POP     B
        RET
NEWLN:  MVI     A,CR
        CALL    PUTC
        MVI     A,LF
        JMP     PUTC
UPPER:  CPI     'a'
        RC
        CPI     'z'+1
        RNC
        SUI     20H
        RET
PSTR:   MOV     A,M
        ORA     A
        RZ
        CALL    PUTC
        INX     H
        JMP     PSTR
; B = source length, HL = source. Fixed 13-byte token slots.
PARSE:  XRA     A
        STA     NTOK
        LXI     D,TOKS
PSKIP:  MOV     A,B
        ORA     A
        RZ
        MOV     A,M
        CPI     ' '
        JZ      PSP
        CPI     9
        JNZ     PNEW
PSP:    INX     H
        DCR     B
        JMP     PSKIP
PNEW:   LDA     NTOK
        CPI     10
        JNC     PERR
        INR     A
        STA     NTOK
        MVI     C,12
PCHAR:  MOV     A,B
        ORA     A
        JZ      PEND
        MOV     A,M
        CPI     ' '
        JZ      PEND
        CPI     9
        JZ      PEND
        MOV     A,C
        ORA     A
        JZ      PERR
        MOV     A,M
        CPI     21H
        JC      PERR
        CPI     7FH
        JNC     PERR
        CALL    UPPER
        STAX    D
        INX     D
        INX     H
        DCR     B
        DCR     C
        JMP     PCHAR
PEND:   XRA     A
        STAX    D
        INX     D
        INR     C
PPAD:   DCR     C
        JZ      PSKIP
        STAX    D
        INX     D
        JMP     PPAD
PERR:   STC
        RET
; A = token number. HL = token; BC,DE destroyed.
TOKEN:  LXI     H,TOKS
        LXI     D,13
TKN1:   ORA     A
        RZ
        DAD     D
        DCR     A
        JMP     TKN1
; Copy B bytes from HL to DE.
COPY:   MOV     A,M
        STAX    D
        INX     H
        INX     D
        DCR     B
        JNZ     COPY
        RET
; Expand relative command by appending to saved successful path.
DOT:    LDA     NKEEP
        ORA     A
        JZ      DNONE
        MOV     B,A
        LDA     NTOK
        ADD     B
        CPI     11
        JNC     DERR
        STA     NTOK
        LXI     H,TOKS
        LXI     D,TEMP
        MVI     B,130
        CALL    COPY
        LXI     H,KEEP
        LXI     D,TOKS
        MVI     B,130
        CALL    COPY
        LDA     NKEEP
        CALL    TOKEN
        XCHG
        LXI     H,TEMP
        LDA     NTOK
        MOV     B,A
        LDA     NKEEP
        MOV     C,A
        MOV     A,B
        SUB     C
        MOV     C,A
DCP:    MVI     B,13
        CALL    COPY
        DCR     C
        JNZ     DCP
        LDA     NKEEP
        CALL    TOKEN
        MOV     A,M
        CPI     '.'
        JNZ     DVALID
DREM:   INX     H
        MOV     A,M
        DCX     H
        MOV     M,A
        INX     H
        ORA     A
        JNZ     DREM
DVALID: LDA     NKEEP
        CALL    TOKEN
        MOV     A,M
        ORA     A
        JZ      DERR
        RET
DNONE:  LXI     D,MSGNC
        JMP     DMSG
DERR:   LXI     D,MSGIN
DMSG:   CALL    PRINT
        STC
        RET
RUN:    XRA     A
        STA     MATCH
        STA     RELTRY
        LDA     TOKS
        CPI     '.'
        JZ      RUNREL
        LDA     INTERA
        ORA     A
        JZ      ABSOL
        LDA     NKEEP
        ORA     A
        JZ      ABSOL
; In a topic, a bare name is a direct child, not a filename.
        CALL    DOT
        RC
        JMP     ABSOL
RUNREL: CALL    DOT
        RC
        MVI     A,1
        STA     RELTRY
        JMP     ABSOL
; Parent navigation already supplied an absolute token path.
RUNABS: XRA     A
        STA     MATCH
        STA     RELTRY
ABSOL:  CALL    NAME
        JC      INERR
        CALL    DFILE
        CALL    OPEN
        JC      OPERR
        XRA     A
        STA     LEVEL
        MVI     A,1
        STA     WANT
        LDA     NTOK
        CPI     1
        JZ      FIRST
; Try an explicit root name. If absent, use the first root.
        CALL    SEARCH
        LDA     MATCH
        ORA     A
        JZ      AGAIN
        CALL    CHOOSE
        JC      FAIL
        CALL    SAVNAM
        MVI     A,1
        STA     LEVEL
        LDA     WANT
        INR     A
        STA     WANT
        JMP     PATH
AGAIN:  CALL    REOPEN
        JC      FAIL
FIRST:  CALL    RLINE
        JC      FAIL
        CALL    MARK
        JC      FIRST
        LDA     TLEV
        CPI     1
        JNZ     FIRST
        STA     LEVEL
PATH:   LDA     WANT
        MOV     B,A
        LDA     NTOK
        CMP     B
        JZ      FOUND
        CALL    SEARCH
        CALL    CHOOSE
        JC      FAIL
        CALL    SAVNAM
        LDA     LEVEL
        INR     A
        STA     LEVEL
        LDA     WANT
        INR     A
        STA     WANT
        JMP     PATH
FOUND:  LDA     NTOK
        STA     NKEEP
        LXI     H,TOKS
        LXI     D,KEEP
        MVI     B,130
        CALL    COPY
        LXI     H,WPATH
        LXI     D,DPATH
        MVI     B,130
        CALL    COPY
        CALL    BODY
        CALL    CLOSE
        JMP     CKERR
 ; Build the visible filename without extension or drive prefix.
DFILE:  LXI     H,FNAME+1
        LXI     D,WPATH
        MVI     B,8
DF1:    MOV     A,M
        CPI     ' '
        JZ      DFEND
        STAX    D
        INX     H
        INX     D
        DCR     B
        JNZ     DF1
DFEND:  XRA     A
        STAX    D
        RET
; Save a canonical marker name in the corresponding display slot.
SAVNAM: CALL    MARK
        LDA     WANT
        LXI     H,WPATH
        LXI     D,13
SN1:    ORA     A
        JZ      SN2
        DAD     D
        DCR     A
        JMP     SN1
SN2:    XCHG
        LHLD    TNAME
        MVI     B,12
SN3:    MOV     A,M
        ORA     A
        JZ      DFEND
        CALL    UPPER
        STAX    D
        INX     H
        INX     D
        DCR     B
        JNZ     SN3
        JMP     DFEND
; A failed first relative component may instead name a sibling.
; Do not retry an ambiguity, an I/O error, or a later component.
FAIL:   LDA     RELTRY
        ORA     A
        JZ      FAIL0
        XRA     A
        STA     RELTRY
        LDA     IOERR
        ORA     A
        JNZ     FAIL0
        LDA     MATCH
        ORA     A
        JNZ     FAIL0
        LDA     NKEEP
        CPI     2
        JC      FAIL0
        MOV     B,A
        LDA     WANT
        CMP     B
        JNZ     FAIL0
        LDA     NKEEP
        CALL    TOKEN
        PUSH    H
        LDA     NKEEP
        DCR     A
        CALL    TOKEN
        XCHG
        POP     H
        LDA     NKEEP
        MOV     B,A
        LDA     NTOK
        SUB     B
        MOV     C,A
SIBCP:  MVI     B,13
        CALL    COPY
        DCR     C
        JNZ     SIBCP
        LDA     NTOK
        DCR     A
        STA     NTOK
        CALL    CLOSE
        JMP     ABSOL
FAIL0:  CALL    CLOSE
        LDA     IOERR
        ORA     A
        JNZ     CKERR
        LDA     MATCH
        CPI     2
        LXI     D,MSGAMB
        JZ      FAILP
        LXI     D,MSGNF
FAILP:  JMP     PRINT
INERR:  LXI     D,MSGIN
        JMP     PRINT
OPERR:  LDA     ISOPEN
        ORA     A
        JNZ     FAIL
        LXI     D,MSGFIL
        JMP     PRINT
CKERR:  LDA     IOERR
        ORA     A
        RZ
        LXI     D,MSGIO
        JMP     PRINT
; File token: [A:]basename[.HLP]. Reject wildcards and truncation.
NAME:   LXI     H,FNAME
        MVI     M,0
        INX     H
        MVI     B,8
NAMSP:  MVI     M,' '
        INX     H
        DCR     B
        JNZ     NAMSP
        MVI     M,'H'
        INX     H
        MVI     M,'L'
        INX     H
        MVI     M,'P'
        LXI     H,TOKS+1
        MOV     A,M
        CPI     ':'
        DCX     H
        JNZ     NAMGO
        MOV     A,M
        SUI     'A'
        CPI     16
        JNC     PERR
        INR     A
        STA     FNAME
        INX     H
        INX     H
NAMGO:  LXI     D,FNAME+1
        MVI     B,8
        MOV     A,M
        ORA     A
        JZ      PERR
        CPI     '.'
        JZ      PERR
NAMLP:  MOV     A,M
        ORA     A
        RZ
        CPI     '.'
        JZ      NAMEXT
        CPI     ':'
        JZ      PERR
        CPI     '*'
        JZ      PERR
        CPI     '?'
        JZ      PERR
        CPI     '/'
        JZ      PERR
        CPI     5CH
        JZ      PERR
        CPI     '='
        JZ      PERR
        CPI     '<'
        JZ      PERR
        CPI     '>'
        JZ      PERR
        CPI     ';'
        JZ      PERR
        CPI     ','
        JZ      PERR
        CPI     '['
        JZ      PERR
        CPI     ']'
        JZ      PERR
        MOV     A,B
        ORA     A
        JZ      PERR
        MOV     A,M
        STAX    D
        INX     D
        INX     H
        DCR     B
        JMP     NAMLP
NAMEXT: INX     H
        MOV     A,M
        CPI     'H'
        JNZ     PERR
        INX     H
        MOV     A,M
        CPI     'L'
        JNZ     PERR
        INX     H
        MOV     A,M
        CPI     'P'
        JNZ     PERR
        INX     H
        MOV     A,M
        ORA     A
        JNZ     PERR
        RET
CLEAR:  LXI     H,FCB
        MVI     B,36
        XRA     A
CL1:    MOV     M,A
        INX     H
        DCR     B
        JNZ     CL1
        RET
OPEN:   XRA     A
        STA     ISOPEN
        STA     IOERR
        STA     ENDF
        STA     PENDCH
        STA     POS
        LXI     H,0
        SHLD    LNUM
        CALL    CLEAR
        LXI     H,FNAME
        LXI     D,FCB
        MVI     B,12
        CALL    COPY
        LXI     D,FCB
        MVI     C,15
        CALL    SYS
        CPI     0FFH
        JZ      PERR
        MVI     A,1
        STA     ISOPEN
        CALL    RECORD
        RC
; Detect a first index entry by its binary level/offset fields,
; then validate every entry. Text cannot contain such control bytes.
        MVI     A,1
        STA     IXSTR
        LXI     H,BUF
        CALL    IDXENT
        JNC     IDXREC
        CALL    IDTERM
        JC      TEXT
; Once detected, validate layout but ignore level metadata:
; navigation reads levels from the text, not from the index.
IDXREC: XRA     A
        STA     IXSTR
        LXI     H,BUF
        MVI     B,8
IDXLP:  CALL    IDTERM
        JNC     IDXEND
        CALL    IDXENT
        JC      IOFAIL
        LXI     D,16
        DAD     D
        DCR     B
        JNZ     IDXLP
        CALL    RECORD
        JC      IOFAIL
        JMP     IDXREC
IDXEND: CALL    RECORD
        RC
TEXT:   XRA     A
        STA     POS
        RET
; End-of-index is the complete reserved name, not any dollar prefix.
IDTERM: PUSH    H
        PUSH    B
        MOV     A,M
        CPI     '$'
        JNZ     IDBAD
        MVI     C,11
IDT1:   INX     H
        MOV     A,M
        CPI     ' '
        JNZ     IDBAD
        DCR     C
        JNZ     IDT1
        ORA     A
        JMP     IDRET
; Validate name and offset; require level 1..9 only for detection.
IDXENT: PUSH    H
        PUSH    B
        MVI     C,12
IDN:    MOV     A,M
        CPI     20H
        JC      IDBAD
        CPI     7FH
        JNC     IDBAD
        INX     H
        DCR     C
        JNZ     IDN
        INX     H
        INX     H
        MOV     A,M
        CPI     128
        JNC     IDBAD
        LDA     IXSTR
        ORA     A
        JZ      IDRET
        INX     H
        MOV     A,M
        CPI     1
        JC      IDBAD
        CPI     10
        JNC     IDBAD
        ORA     A
        JMP     IDRET
IDBAD:  STC
IDRET:  POP     B
        POP     H
        RET
CLOSE:  LDA     ISOPEN
        ORA     A
        RZ
        LXI     D,FCB
        MVI     C,16
        CALL    SYS
        XRA     A
        STA     ISOPEN
        RET
REOPEN: CALL    CLOSE
        JMP     OPEN
RECORD: PUSH    B
        PUSH    D
        LXI     D,BUF
        MVI     C,26
        CALL    SYS
        LXI     D,FCB
        MVI     C,20
        CALL    SYS
        POP     D
        POP     B
        ORA     A
        JZ      RECOK
        CPI     1
        JNZ     IOFAIL
        MVI     A,1
        STA     ENDF
        STC
        RET
RECOK:  XRA     A
        STA     POS
        RET
IOFAIL: MVI     A,1
        STA     IOERR
        STA     ENDF
        STC
        RET
; Get text byte, preserving BC,DE,HL. Pending byte is for bare CR.
BYTE:   LDA     PENDCH
        ORA     A
        JZ      BYRAW
        XRA     A
        STA     PENDCH
        LDA     PBYTE
        ORA     A
        RET
BYRAW:  LDA     ENDF
        ORA     A
        JNZ     PERR
        LDA     POS
        CPI     128
        JNZ     BYGET
        CALL    RECORD
        RC
BYGET:  PUSH    H
        PUSH    D
        LDA     POS
        MOV     E,A
        MVI     D,0
        LXI     H,BUF
        DAD     D
        INR     A
        STA     POS
        MOV     A,M
        POP     D
        POP     H
        CPI     1AH
        JZ      BYEOF
        ORA     A
        RET
BYEOF:  MVI     A,1
        STA     ENDF
        STC
        RET
; Read a whole line, bounded and NUL terminated. Line count is 16-bit.
RLINE:  LXI     H,LINE
        MVI     B,255
RL1:    CALL    BYTE
        JC      RLEOF
        CPI     CR
        JZ      RLCR
        CPI     LF
        JZ      RLEND
        ORA     A
        JZ      RLPAD
        MOV     C,A
        MOV     A,B
        ORA     A
        JZ      IOFAIL
        MOV     M,C
        INX     H
        DCR     B
        JMP     RL1
RLCR:   CALL    BYTE
        JC      RLEND
        CPI     LF
        JZ      RLEND
        STA     PBYTE
        MVI     A,1
        STA     PENDCH
RLEND:  MVI     M,0
        LHLD    LNUM
        INX     H
        SHLD    LNUM
        MOV     A,H
        ORA     L
        JZ      IOFAIL
        ORA     A
        RET
; Zero-filled final records are common in older HLP files.
; Validate the tail instead of silently hiding text after a NUL.
RLPAD:  CALL    BYTE
        JC      RLEOF
        ORA     A
        JZ      RLPAD
        JMP     IOFAIL
RLEOF:  MOV     A,B
        CPI     255
        JZ      PERR
        JMP     RLEND
; Recognize marker anywhere in the line, as CP/M Plus does.
MARK:   LXI     H,LINE
MK1:    MOV     A,M
        ORA     A
        JZ      PERR
        INX     H
        CPI     '/'
        JNZ     MK1
        MOV     A,M
        CPI     '/'
        JNZ     MK1
        INX     H
        MOV     A,M
        CPI     '/'
        JNZ     MK1
        INX     H
        MOV     A,M
        CPI     '1'
        JC      MKOLD
        CPI     '9'+1
        JNC     MKOLD
        SUI     '0'
        STA     TLEV
        INX     H
        JMP     MKSP
; Missing level in legacy files (e.g. ///DDT): a top-level name.
; Accept only an alphabetic start, not ///0 or arbitrary garbage.
MKOLD:  CALL    UPPER
        CPI     'A'
        JC      MK1
        CPI     'Z'+1
        JNC     MK1
        MVI     A,1
        STA     TLEV
MKSP:   MOV     A,M
        CPI     ' '
        JZ      MKSK
        CPI     9
        JNZ     MKNAME
MKSK:   INX     H
        JMP     MKSP
MKNAME: ORA     A
        JZ      PERR
        SHLD    TNAME
; Trim trailing spaces/tabs in-place for exact matching.
MKEND:  MOV     A,M
        ORA     A
        JZ      MKBACK
        INX     H
        JMP     MKEND
MKBACK: DCX     H
        MOV     A,M
        CPI     ' '
        JZ      MKZERO
        CPI     9
        JNZ     MKYES
MKZERO: MVI     M,0
        JMP     MKBACK
MKYES:  XRA     A
        RET
; Search direct children within LEVEL scope. MATCH=0,1,2 (saturated).
; EXACT flag prefers exact over prefix, and detects duplicate exacts.
SEARCH: XRA     A
        STA     MATCH
        STA     EXACT
SR1:    CALL    RLINE
        RC
        CALL    MARK
        JC      SR1
        LDA     TLEV
        MOV     B,A
        LDA     LEVEL
        CMP     B
        RNC
        INR     A
        CMP     B
        JNZ     SR1
        LDA     WANT
        CALL    TOKEN
        XCHG
        LHLD    TNAME
CMP1:   LDAX    D
        ORA     A
        JZ      CMPEND
        MOV     B,A
        MOV     A,M
        CALL    UPPER
        CMP     B
        JNZ     SR1
        INX     H
        INX     D
        JMP     CMP1
CMPEND: MOV     A,M
        ORA     A
        JZ      EXACTM
        LDA     EXACT
        ORA     A
        JNZ     SR1
        JMP     ADDMAT
EXACTM: LDA     EXACT
        ORA     A
        JNZ     ADDMAT
        MVI     A,1
        STA     EXACT
        XRA     A
        STA     MATCH
ADDMAT: LDA     MATCH
        CPI     2
        JNC     SR1
        INR     A
        STA     MATCH
        LHLD    LNUM
        SHLD    PICK
        JMP     SR1
; Reopen and seek to selected line, not the first identical marker.
CHOOSE: LDA     IOERR
        ORA     A
        JNZ     PERR
        LDA     MATCH
        CPI     1
        JNZ     PERR
        CALL    REOPEN
        RC
CH1:    CALL    RLINE
        RC
        LHLD    PICK
        XCHG
        LHLD    LNUM
        MOV     A,H
        CMP     D
        JNZ     CH1
        MOV     A,L
        CMP     E
        JNZ     CH1
        ORA     A
        RET
BODY:   CALL    NEWLN
        XRA     A
        STA     PAGE
        STA     SUBS
        CALL    BL1
        LDA     IOERR
        ORA     A
        RNZ
        LDA     SUBS
        ORA     A
        RNZ
        LXI     D,MSGNON
        JMP     PRINT
BL1:    CALL    RLINE
        RC
        CALL    MARK
        JNC     CHILD
        LDA     UPONLY
        ORA     A
        JNZ     BL1
        LXI     H,LINE
        CALL    PSTR
        CALL    NEWLN
        CALL    PAGING
        JNC     BL1
; Q skips the remaining body, but still presents the child choices.
        MVI     A,1
        STA     UPONLY
        JMP     BL1
CHILD:  LDA     TLEV
        MOV     B,A
        LDA     LEVEL
        CMP     B
        RNC
        INR     A
        CMP     B
        JNZ     CHNEXT
        LDA     SUBS
        ORA     A
        JNZ     CHPR
        MVI     A,1
        STA     SUBS
        LXI     D,MSGSUB
        CALL    PRINT
CHPR:   LHLD    TNAME
        CALL    PSTR
        CALL    NEWLN
        CALL    PAGING
        RC
CHNEXT: CALL    RLINE
        RC
        CALL    MARK
        JC      CHNEXT
        JMP     CHILD
PAGING: LDA     PAGE
        INR     A
        STA     PAGE
        CPI     20
        JNC     MORE
        ORA     A
        RET
MORE:   LXI     D,MSGMOR
        CALL    PRINT
        MVI     C,1
        CALL    SYS
        CALL    UPPER
        PUSH    PSW
        CALL    NEWLN
        POP     PSW
        CPI     'Q'
        JZ      PERR
        XRA     A
        STA     PAGE
        RET
; Search uses a separate DMA, preserving command input and file buffers.
LIST:   MVI     C,31
        CALL    BDOS
        LXI     D,4
        DAD     D
        MOV     A,M
        CMA
        ANI     1FH
        STA     EXMASK
        CALL    CLEAR
        LXI     H,FCB+1
        MVI     B,8
LSINIT: MVI     M,'?'
        INX     H
        DCR     B
        JNZ     LSINIT
        MVI     M,'H'
        INX     H
        MVI     M,'L'
        INX     H
        MVI     M,'P'
        LXI     D,DIRBUF
        MVI     C,26
        CALL    SYS
        LXI     D,FCB
        MVI     C,17
        CALL    SYS
        CPI     0FFH
        JZ      LSNONE
LS1:    ANI     3
        ADD     A
        ADD     A
        ADD     A
        ADD     A
        ADD     A
        MOV     E,A
        MVI     D,0
        LXI     H,DIRBUF
        DAD     D
; Only list extent zero, avoiding duplicate filenames for large files.
        PUSH    H
        LXI     D,12
        DAD     D
        LDA     EXMASK
        ANA     M
        MOV     B,A
        INX     H
        INX     H
        MOV     A,M
        ANI     3FH
        ORA     B
        POP     H
        JNZ     LSNEXT
        INX     H
        MVI     B,8
LSNAME: MOV     A,M
        ANI     7FH
        CALL    PUTC
        INX     H
        DCR     B
        JNZ     LSNAME
        CALL    NEWLN
LSNEXT: LXI     D,FCB
        MVI     C,18
        CALL    SYS
        CPI     0FFH
        JNZ     LS1
        JMP     NEWLN
LSNONE: LXI     D,MSGNO
        JMP     PRINT
BANNER: DB      CR,LF,'CP/M Help System 3.3',CR,LF
        DB      'Available help files:',CR,LF,'$'
PROMPT: DB      'HELP>$'
MSGALL: DB      CR,LF,'Available help files:',CR,LF,'$'
MSGNON: DB      'No subtopics.',CR,LF,'$'
MSGIN:  DB      'Invalid name or input too long.',CR,LF,'$'
MSGNC:  DB      'No current topic.',CR,LF,'$'
MSGFIL: DB     'Help file not found.',CR,LF,'$'
MSGNF:  DB      'Help topic not found.',CR,LF,'$'
MSGAMB: DB      'Ambiguous topic; type more characters.',CR,LF,'$'
MSGIO:  DB      'Read error, bad index, or text limit exceeded.',CR,LF,'$'
MSGSUB: DB      CR,LF,'Subtopics:',CR,LF,'$'
MSGMOR: DB      '-- More -- RETURN=continue, Q=quit $'
MSGNO:  DB      'No .HLP files found.',CR,LF,'$'
NTOK:   DB      0
NKEEP:  DB      0
INTERA: DB      0
UPONLY: DB      0
RELTRY: DB      0
IXSTR:  DB      0
ISOPEN: DB      0
IOERR:  DB      0
ENDF:   DB      0
POS:    DB      0
PENDCH: DB      0
PBYTE:  DB      0
LEVEL:  DB      0
TLEV:   DB      0
WANT:   DB      0
MATCH:  DB      0
EXACT:  DB      0
PAGE:   DB      0
SUBS:   DB      0
EXMASK: DB      31
LNUM:   DW      0
PICK:   DW      0
TNAME:  DW      0
INPUT:  DB      127,0
        DS      128
FNAME:  DS      12
FCB:    DS      36
TOKS:   DS      130
KEEP:   DS      130
DPATH:  DS      130
WPATH:  DS      130
TEMP:   DS      130
LINE:   DS      256
BUF:    DS      128
DIRBUF: DS      128
        DS      256
STKTOP  EQU     $
        END

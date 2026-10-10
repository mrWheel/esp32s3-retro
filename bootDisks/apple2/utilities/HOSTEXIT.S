; Leaves HOST by closing every open file and re-entering the resident BASIC.SYSTEM through its warm-start vector.

        .export _hostReturnToBasic
        .import __HIMEM__
        .importzp sp
        .constructor hostMoveStack, 1

MLI     = $BF00
WARMDOS = $BE00

        .segment "ONCE"

; The ProDOS startup puts the C stack at $BEFF, on top of BASIC.SYSTEM's global page code; keep it below HIMEM instead.
.proc hostMoveStack
        lda #<__HIMEM__
        sta sp
        lda #>__HIMEM__
        sta sp+1
        rts
.endproc

        .code

.proc _hostReturnToBasic
        jsr MLI
        .byte $CC
        .word closeAll
        bit $C082
        ldx #$FF
        txs
        jmp WARMDOS
closeAll:
        .byte 1, 0
.endproc

bits 16
cpu 8086
org 0
 times 0x100 db 0
; .CMD entry point at offset 0x100. The viewer sets up its own segment
; registers and stack, so the entry only jumps to it. RETRO86 has no
; IBM BIOS data area and no ROM date at F000:FFF5, so the terminal
; front end at 0x1800 can only report 'Not reported' on this machine;
; it is kept dormant for IBM-compatible guests. To start it instead,
; replace the jump with the front end's own stack stub:
;   cli
;   push cs
;   pop ss
;   mov sp,0x1d2
;   sti
;   jmp terminal_entry
start:
 jmp hostinfo_start
; Host information viewer (SYSVIEW 1.0, RETRO86-SYSINFO/1 interface
; on ports 0xF8/0xF9, implemented by main/cpm86Machine.c).
hostinfo_start:
 push cs
 pop ds
 push cs
 pop es
 cli
 mov ax,cs
 mov ss,ax
 mov sp,stack_top
 sti
 cld
host_refresh:
 mov si,clear
 call puts
 mov byte [lines],0
 mov si,title
 call puts
 ; Probe before any write. Older RETRO86 firmware returns FF on this port.
 in al,0xf8
 cmp al,0xa6
 je supported
 mov si,missing
 call puts
 jmp finished
supported:
 mov al,1
 out 0xf8,al
 ; Check protocol header before interpreting a response.
 mov si,signature
check_header:
 lodsb
 or al,al
 jz header_ok
 mov ah,al
 in al,0xf9
 cmp al,ah
 jne protocol_bad
 jmp check_header
header_ok:
 mov si,os_label
 call puts
 mov cl,12
 int 0xe0
 call hexword
 mov si,drive_label
 call puts
 mov cl,25
 int 0xe0
 add al,'A'
 call output
 mov si,user_label
 call puts
 mov cl,32
 mov dl,255
 int 0xe0
 xor ah,ah
 call decimal
 mov si,newline
 call puts
 ; Bound a faulty stream even if firmware never returns a terminator.
 mov word [remaining],4096
read_stream:
 in al,0xf9
 or al,al
 jz finished
 call output
 dec word [remaining]
 jnz read_stream
protocol_bad:
 mov si,invalid
 call puts
finished:
 mov si,prompt
 call puts
wait_final:
 call key
 cmp al,27
 je exit
 cmp al,'q'
 je exit
 cmp al,'Q'
 je exit
 cmp al,'r'
 je restart
 cmp al,'R'
 jne wait_final
restart:
 jmp host_refresh
exit:
 mov si,restore
 call rawputs
 xor cx,cx
 int 0xe0
 jmp short $
key:
 mov cl,6
 mov dl,255
 int 0xe0
 or al,al
 jz key
 ret
output:
 call raw
 cmp al,10
 jne .return
 inc byte [lines]
 cmp byte [lines],20
 jb .return
 push ax
 push bx
 push cx
 push dx
 push si
 mov si,more
 call rawputs
.wait:
 call key
 cmp al,27
 je .quit
 cmp al,'q'
 je .quit
 cmp al,'Q'
 je .quit
 cmp al,' '
 je .continue
 cmp al,13
 jne .wait
.continue:
 mov si,erase
 call rawputs
 mov byte [lines],0
 pop si
 pop dx
 pop cx
 pop bx
 pop ax
 jmp short .return
.quit:
 jmp exit
.return:
 ret
raw:
 push ax
 push bx
 push cx
 push dx
 push si
 push di
 push bp
 mov dl,al
 mov cl,6
 int 0xe0
 pop bp
 pop di
 pop si
 pop dx
 pop cx
 pop bx
 pop ax
 ret
puts:
 lodsb
 or al,al
 jz .end
 call output
 jmp puts
.end:
 ret
rawputs:
 lodsb
 or al,al
 jz .end
 call raw
 jmp rawputs
.end:
 ret
hexword:
 push ax
 push bx
 push cx
 push dx
 mov bx,ax
 mov cx,4
.loop:
 push cx
 mov cl,4
 rol bx,cl
 mov al,bl
 and al,15
 add al,'0'
 cmp al,'9'
 jbe .emit
 add al,7
.emit:
 call output
 pop cx
 loop .loop
 mov al,'h'
 call output
 pop dx
 pop cx
 pop bx
 pop ax
 ret
decimal:
 push ax
 push bx
 push cx
 push dx
 xor cx,cx
 mov bx,10
.divide:
 xor dx,dx
 div bx
 push dx
 inc cx
 or ax,ax
 jnz .divide
.print:
 pop ax
 add al,'0'
 call output
 loop .print
 pop dx
 pop cx
 pop bx
 pop ax
 ret
lines db 0
remaining dw 0
clear db 27,'[0m',27,'[2J',27,'[H',0
title db 'SYSVIEW 1.0 - RETRO86 emulator information',13,10
 db '------------------------------------------------------------',13,10,0
signature db 'RETRO86-SYSINFO/1',13,10,0
os_label db 'CP/M BDOS version:  ',0
drive_label db 13,10,'Current CP/M drive: ',0
user_label db '    User area: ',0
newline db 13,10,13,10,0
missing db 'Emulator information interface is not available.',13,10
 db 'Install the matching RETRO86 firmware update first.',13,10
 db 'No IBM BIOS guesses or CPU FLAGS-based detection are used.',13,10,0
invalid db 13,10,'Invalid or unsupported emulator information response.',13,10,0
prompt db 13,10,'R = refresh    Q or Esc = exit to CP/M',13,10,0
more db '-- More -- Space/Return = continue; Q/Esc = exit',0
erase db 13,27,'[2K',0
restore db 27,'[0m',27,'[?25h',13,10,0
 align 2
 times 512 db 0
stack_top:
; Pad so the terminal front end sits at load offset 0x1800, exactly
; where the old REBUILD.PY patch placed it. The padding also bounds
; the dormant viewer: nasm fails if it ever grows past 0x1800.
 times 0x1800 - ($ - $$) db 0
; VT100/ANSI terminal front end, formerly patch/SYSVANSI.ASM. Dormant
; on RETRO86 (no IBM BIOS data area to read); kept for IBM-compatible
; guests. It shows a guest hardware overview through CP/M BDOS console
; I/O only, with no PC BIOS calls on its executed path. Labels
; refresh, puts, decimal and restore carry a term_ prefix here to
; avoid clashing with the viewer above.
terminal_entry:
 push cs
 pop ds
 cld
t_refresh:
 mov si,screen
 call t_puts
 ; CPU classification using FLAGS, with the original flags restored.
 pushf
 pop bx
 mov ax,bx
 and ax,0x0fff
 push ax
 popf
 pushf
 pop ax
 and ax,0xf000
 cmp ax,0xf000
 je cpu86
 mov ax,bx
 or ax,0xf000
 push ax
 popf
 pushf
 pop ax
 and ax,0xf000
 jz cpu286
 mov si,cpu386
 jmp short cpudone
cpu86:
 mov si,cpu8086
 jmp short cpudone
cpu286:
 mov si,cpu80286
cpudone:
 push bx
 popf
 call t_puts
 mov si,date_label
 call t_puts
 ; IBM-compatible ROM date at physical FFFF5h. Never execute ROM code.
 push es
 mov ax,0xf000
 mov es,ax
 mov di,0xfff5
 mov cx,8
.date:
 mov al,[es:di]
 cmp al,32
 jb .unknown
 cmp al,126
 ja .unknown
 jmp short .emit
.unknown:
 mov al,'?'
.emit:
 call putc
 inc di
 loop .date
 pop es
 mov si,math_label
 call t_puts
 push es
 mov ax,0x40
 mov es,ax
 mov ax,[es:0x10]
 mov [equipment],ax
 mov ax,[es:0x13]
 mov [memory_kib],ax
 mov al,[es:0x49]
 mov [video_mode],al
 pop es
 mov byte [bda_valid],0
 cmp word [memory_kib],1
 jb .invalid_bda
 cmp word [memory_kib],1024
 ja .invalid_bda
 mov byte [bda_valid],1
.invalid_bda:
 mov ax,[equipment]
 test ax,2
 mov si,no
 jz .math
 mov si,yes
.math:
 cmp byte [bda_valid],0
 jne .math_output
 mov si,unknown
.math_output:
 call t_puts
 mov si,memory_label
 call t_puts
 cmp byte [bda_valid],0
 je .memory_unknown
 mov ax,[memory_kib]
 call t_decimal
 mov si,kib
 call t_puts
 jmp short .memory_done
.memory_unknown:
 mov si,unknown
 call t_puts
.memory_done:
 mov si,video_label
 call t_puts
 cmp byte [bda_valid],0
 je .video_unknown
 xor ax,ax
 mov al,[video_mode]
 call t_decimal
 mov si,video_note
 call t_puts
 jmp short .video_done
.video_unknown:
 mov si,unknown
 call t_puts
.video_done:
 mov si,floppy_label
 call t_puts
 cmp byte [bda_valid],0
 je .floppy_unknown
 mov ax,[equipment]
 test ax,1
 jz .floppy_zero
 mov cl,6
 shr ax,cl
 and ax,3
 inc ax
 jmp short .floppy
.floppy_zero:
 xor ax,ax
.floppy:
 call t_decimal
 jmp short .floppy_done
.floppy_unknown:
 mov si,unknown
 call t_puts
.floppy_done:
 mov si,lpt_label
 call t_puts
 cmp byte [bda_valid],0
 je .lpt_unknown
 mov ax,[equipment]
 mov cl,14
 shr ax,cl
 and ax,3
 call t_decimal
 jmp short .lpt_done
.lpt_unknown:
 mov si,unknown
 call t_puts
.lpt_done:
 mov si,game_label
 call t_puts
 mov ax,[equipment]
 test ax,0x1000
 mov si,no
 jz .game
 mov si,yes
.game:
 cmp byte [bda_valid],0
 jne .game_output
 mov si,unknown
.game_output:
 call t_puts
 mov si,footer
 call t_puts
waitkey:
 mov cl,6
 mov dl,255
 int 0xe0
 and al,0x7f
 jz waitkey
 cmp al,27
 je done
 cmp al,'q'
 je done
 cmp al,'Q'
 je done
 cmp al,'r'
 je refresh_jump
 cmp al,'R'
 je refresh_jump
 jmp short waitkey
refresh_jump:
 jmp t_refresh
done:
 mov si,t_restore
 call t_puts
 xor cx,cx
 int 0xe0
 jmp short $

; No output routine reads input. Preserve registers used by caller.
putc:
 push ax
 push bx
 push cx
 push dx
 push si
 push di
 push bp
 mov dl,al
 mov cl,6
 int 0xe0
 pop bp
 pop di
 pop si
 pop dx
 pop cx
 pop bx
 pop ax
 ret
t_puts:
 lodsb
 or al,al
 jz .end
 call putc
 jmp short t_puts
.end:
 ret
t_decimal:
 push ax
 push bx
 push cx
 push dx
 xor cx,cx
 mov bx,10
.divide:
 xor dx,dx
 div bx
 push dx
 inc cx
 or ax,ax
 jnz .divide
.print:
 pop ax
 add al,'0'
 call putc
 loop .print
 pop dx
 pop cx
 pop bx
 pop ax
 ret

equipment dw 0
memory_kib dw 0
video_mode db 0
bda_valid db 0
screen db 27,'[0m',27,'[2J',27,'[H',27,'[?25l'
 db 'SYSVIEW 0.1 - VT100/ANSI terminal edition',13,10
 db 'System hardware overview (IBM-compatible guest)',13,10
 db '-------------------------------------------------------------',13,10,13,10
 db 'Processor class:       ',0
cpu8086 db '8086/8088 compatible (includes NEC V20/V30)',0
cpu80286 db '80286',0
cpu386 db '80386 or later',0
date_label db 13,10,'IBM ROM date field:    ',0
math_label db 13,10,'Math coprocessor:      ',0
memory_label db 13,10,'Conventional memory:   ',0
kib db ' KiB (BIOS data area)',0
video_label db 13,10,'Guest BIOS video mode: ',0
video_note db ' (decimal; not the remote terminal type)',0
floppy_label db 13,10,'Floppy drives:         ',0
lpt_label db 13,10,'Parallel/LPT ports:    ',0
game_label db 13,10,'Game port:             ',0
unknown db 'Not reported (no valid IBM BIOS data area)',0
yes db 'Yes (BIOS equipment flag)',0
no db 'No (BIOS equipment flag)',0
footer db 13,10,'Secondary video:       Not probed in terminal edition',13,10,13,10
 db 'Information comes from guest CPU/BIOS data, not your host Mac.',13,10
 db 'BIOS equipment flags may be incomplete or emulated.',13,10
 db 'No PC BIOS calls, video-memory writes or speaker/CRTC probes.',13,10,13,10
 db 'R = refresh    Q or ESC = exit to CP/M',13,10,0
t_restore db 27,'[0m',27,'[?25h',27,'[24;1H',13,10,0

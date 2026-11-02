bits 16
cpu 8086
org 0x1800
entry:
 push cs
 pop ds
 cld
refresh:
 mov si,screen
 call puts
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
 call puts
 mov si,date_label
 call puts
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
 call puts
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
 call puts
 mov si,memory_label
 call puts
 cmp byte [bda_valid],0
 je .memory_unknown
 mov ax,[memory_kib]
 call decimal
 mov si,kib
 call puts
 jmp short .memory_done
.memory_unknown:
 mov si,unknown
 call puts
.memory_done:
 mov si,video_label
 call puts
 cmp byte [bda_valid],0
 je .video_unknown
 xor ax,ax
 mov al,[video_mode]
 call decimal
 mov si,video_note
 call puts
 jmp short .video_done
.video_unknown:
 mov si,unknown
 call puts
.video_done:
 mov si,floppy_label
 call puts
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
 call decimal
 jmp short .floppy_done
.floppy_unknown:
 mov si,unknown
 call puts
.floppy_done:
 mov si,lpt_label
 call puts
 cmp byte [bda_valid],0
 je .lpt_unknown
 mov ax,[equipment]
 mov cl,14
 shr ax,cl
 and ax,3
 call decimal
 jmp short .lpt_done
.lpt_unknown:
 mov si,unknown
 call puts
.lpt_done:
 mov si,game_label
 call puts
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
 call puts
 mov si,footer
 call puts
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
 jmp refresh
done:
 mov si,restore
 call puts
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
puts:
 lodsb
 or al,al
 jz .end
 call putc
 jmp short puts
.end:
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
restore db 27,'[0m',27,'[?25h',27,'[24;1H',13,10,0

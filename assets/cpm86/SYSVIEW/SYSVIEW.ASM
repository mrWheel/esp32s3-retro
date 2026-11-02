bits 16
cpu 8086
org 0
 times 0x100 db 0
start:
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
refresh:
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
 jmp refresh
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

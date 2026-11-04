; EDIT86: independent CP/M-86 ANSI editor, inspired by CB86 EDIT 2.04.
; NASM syntax, 8086 only. BDOS INT E0, single code/data group.
bits 16
cpu 8086
org 0
 times 100h db 0
start:
 push cs
 pop ds
 push cs
 pop es
 cli
 mov ax,cs
 mov ss,ax
 mov sp,stacktop
 sti
 cld
 mov si,80h
 xor cx,cx
 mov cl,[si]
 inc si
 mov di,namebuf
.skip:
 jcxz .prompt
 lodsb
 dec cx
 cmp al,' '
 je .skip
 dec si
 inc cx
.copy:
 jcxz .done
 lodsb
 cmp al,' '
 je .done
 cmp al,13
 je .done
 cmp di,namebuf+14
 jae badname
 stosb
 dec cx
 jmp .copy
.done:
 mov byte [di],0
 jmp .parse
.prompt:
 mov si,askname
 call puts
 call readline
 cmp byte [inputbuf+14],0
 jne badname
 mov si,inputbuf
 mov di,namebuf
 mov cx,16
 rep movsb
.parse:
 call parse_name
 jc badname
 call load
 jc loadfail
 mov si,initseq
 call puts
main:
 call render
 call getkey
 cmp ax,100h
 je left
 cmp ax,101h
 je right
 cmp ax,102h
 je up
 cmp ax,103h
 je down
 cmp ax,104h
 je home
 cmp ax,105h
 je endline
 cmp ax,106h
 je pageup
 cmp ax,107h
 je pagedown
 cmp ax,108h
 je delete
 cmp al,27
 je commands
 cmp al,21
 je left
 cmp al,6
 je right
 cmp al,26
 je up
 cmp al,10
 je down
 cmp al,1
 je home
 cmp al,2
 je bottom
 cmp al,20
 je top
 cmp al,18
 je pageup
 cmp al,8
 je backspace
 cmp al,127
 je backspace
 cmp al,13
 je newline
 cmp al,9
 je tabkey
 cmp al,14
 je deleteline
 cmp al,11
 je copyline
 cmp al,24
 je cutline
 cmp al,23
 je paste
 cmp al,4
 je clearclip
 cmp al,7
 je search
 cmp al,19
 je repeatsearch
 cmp al,15
 je savekey
 cmp al,17
 je quitkey
 cmp al,32
 jb main
 cmp al,126
 ja main
 call insertchar
 jmp main
left:
 cmp word [cursor],0
 je main
 dec word [cursor]
 jmp main
right:
 mov ax,[cursor]
 cmp ax,[length]
 jae main
 inc word [cursor]
 jmp main
home:
 call findhome
 mov [cursor],bx
 jmp main
endline:
 call findend
 mov [cursor],bx
 jmp main
top:
 mov word [cursor],0
 jmp main
bottom:
 mov ax,[length]
 mov [cursor],ax
 jmp main
up:
 call moveup
 jmp main
down:
 call movedown
 jmp main
pageup:
 mov bp,21
.loop:
 call moveup
 dec bp
 jnz .loop
 jmp main
pagedown:
 mov bp,21
.loop:
 call movedown
 dec bp
 jnz .loop
 jmp main
newline:
 mov al,10
 call insertchar
 jmp main
tabkey:
 call getcolumn
 and ax,7
 mov bp,8
 sub bp,ax
.loop:
 mov al,' '
 call insertchar
 dec bp
 jnz .loop
 jmp main
backspace:
 cmp word [cursor],0
 je main
 dec word [cursor]
delete:
 call deletechar
 jmp main
deleteline:
 call findhome
 mov [cursor],bx
 call findend
 cmp bx,[length]
 jae .count
 inc bx
.count:
 sub bx,[cursor]
 mov bp,bx
.loop:
 or bp,bp
 jz .done
 call deletechar
 dec bp
 jmp .loop
.done:
 jmp main
copyline:
 call copy_line
 jmp main
cutline:
 call copy_line
 jc main
 jmp deleteline
clearclip:
 mov word [clipsize],0
 jmp main
paste:
 mov ax,[length]
 add ax,[clipsize]
 cmp ax,MAXTEXT
 ja toolarge
 mov si,clip
 mov bp,[clipsize]
.loop:
 or bp,bp
 jz .done
 lodsb
 push si
 call insertchar
 pop si
 dec bp
 jmp .loop
.done:
 jmp main
search:
 mov si,findprompt
 call prompt
 mov si,inputbuf
 mov di,pattern
 mov cx,64
 rep movsb
repeatsearch:
 cmp byte [pattern],0
 je main
 mov bx,[cursor]
 inc bx
.scan:
 cmp bx,[length]
 jae .none
 mov si,pattern
 mov di,text
 add di,bx
.match:
 lodsb
 or al,al
 jz .found
 mov dx,di
 sub dx,text
 cmp dx,[length]
 jae .next
 cmp al,[di]
 jne .next
 inc di
 jmp .match
.next:
 inc bx
 jmp .scan
.found:
 mov [cursor],bx
 jmp main
.none:
 mov si,notfound
 call message
 jmp main
savekey:
 call save
 jmp main
quitkey:
 jmp quit
commands:
 mov si,cmdprompt
 call prompt
 mov si,inputbuf
 call uppercase
 cmp byte [si],'E'
 jne .save
 cmp byte [si+1],'N'
 jne .unknown
 cmp byte [si+2],'D'
 jne .unknown
 cmp byte [si+3],0
 jne .unknown
 call save
 jc main
 jmp exit
.save:
 mov di,csave
 call equals
 jz savekey
 mov di,cquit
 call equals
 jz quit
 mov di,chelp
 call equals
 jz help
 mov di,ctop
 call equals
 jz top
 mov di,cbottom
 call equals
 jz bottom
 cmp byte [si],0
 je main
.unknown:
 mov si,unknown
 call message
 jmp main
quit:
 cmp byte [dirty],0
 je exit
 mov si,confirm
 call prompt
 cmp byte [inputbuf],'Y'
 je exit
 cmp byte [inputbuf],'y'
 je exit
 jmp main
help:
 mov si,clearseq
 call puts
 mov si,helptext
 call puts
 call getkey
 jmp main
exit:
 mov si,exitseq
 call puts
 xor cx,cx
 xor dx,dx
 call bdos
 jmp $
badname:
 mov si,namemsg
 jmp fatal
loadfail:
 mov si,loadmsg
fatal:
 call puts
 jmp exit
toolarge:
 mov si,fullmsg
 call message
 jmp main
; Internal text offsets are 0..length; newline is LF; no NUL terminator.
MAXTEXT equ 40000
insertchar:
 cmp word [length],MAXTEXT
 jae .full
 push ax
 mov cx,[length]
 sub cx,[cursor]
 mov si,text
 add si,[length]
 dec si
 mov di,si
 inc di
 std
 rep movsb
 cld
 pop ax
 mov bx,[cursor]
 mov [text+bx],al
 inc word [length]
 inc word [cursor]
 mov byte [dirty],1
 ret
.full:
 mov al,7
 call putc
 ret
deletechar:
 mov si,[cursor]
 cmp si,[length]
 jae .done
 add si,text
 mov di,si
 inc si
 mov cx,[length]
 sub cx,[cursor]
 dec cx
 rep movsb
 dec word [length]
 mov byte [dirty],1
.done:
 ret
findhome:
 mov bx,[cursor]
.loop:
 or bx,bx
 jz .done
 cmp byte [text+bx-1],10
 je .done
 dec bx
 jmp .loop
.done:
 ret
findend:
 mov bx,[cursor]
.loop:
 cmp bx,[length]
 jae .done
 cmp byte [text+bx],10
 je .done
 inc bx
 jmp .loop
.done:
 ret
getcolumn:
 call findhome
 mov ax,[cursor]
 sub ax,bx
 ret
moveup:
 call getcolumn
 or bx,bx
 jz .done
 dec bx
 mov dx,bx
.prev:
 or bx,bx
 jz .target
 cmp byte [text+bx-1],10
 je .target
 dec bx
 jmp .prev
.target:
 add bx,ax
 cmp bx,dx
 jbe .set
 mov bx,dx
.set:
 mov [cursor],bx
.done:
 ret
movedown:
 call getcolumn
 push ax
 call findend
 pop ax
 cmp bx,[length]
 jae .done
 inc bx
 mov dx,bx
.next:
 cmp dx,[length]
 jae .target
 mov si,dx
 cmp byte [text+si],10
 je .target
 inc dx
 jmp .next
.target:
 add bx,ax
 cmp bx,dx
 jbe .set
 mov bx,dx
.set:
 mov [cursor],bx
.done:
 ret
copy_line:
 call findhome
 mov si,text
 add si,bx
 push bx
 call findend
 cmp bx,[length]
 jae .count
 inc bx
.count:
 pop ax
 sub bx,ax
 cmp bx,2048
 ja .large
 mov [clipsize],bx
 mov cx,bx
 mov di,clip
 rep movsb
 clc
 ret
.large:
 mov si,clipmsg
 call message
 stc
 ret
; Redraw 22 text rows, status row 23, hint row 24; 80 columns.
render:
 xor bx,bx
 xor dx,dx
 xor cx,cx
.locate:
 cmp bx,[cursor]
 jae .located
 mov al,[text+bx]
 inc bx
 cmp al,10
 jne .col
 inc dx
 xor cx,cx
 jmp .locate
.col:
 inc cx
 jmp .locate
.located:
 mov [curline],dx
 mov [curcol],cx
 cmp dx,[topline]
 jae .below
 mov [topline],dx
.below:
 mov ax,[topline]
 add ax,21
 cmp dx,ax
 jbe .horizontal
 sub dx,21
 mov [topline],dx
.horizontal:
 cmp cx,[leftcol]
 jae .right
 mov [leftcol],cx
.right:
 mov ax,[leftcol]
 add ax,79
 cmp cx,ax
 jbe .begin
 sub cx,79
 mov [leftcol],cx
.begin:
 mov si,hidehome
 call puts
 xor bx,bx
 mov cx,[topline]
.skip:
 jcxz .rows
 cmp bx,[length]
 jae .rows
 cmp byte [text+bx],10
 jne .skipchar
 dec cx
.skipchar:
 inc bx
 jmp .skip
.rows:
 mov byte [row],1
.row:
 xor dx,dx
.ch:
 cmp bx,[length]
 jae .endrow
 mov al,[text+bx]
 inc bx
 cmp al,10
 je .endrow
 cmp dx,[leftcol]
 jb .advance
 mov cx,dx
 sub cx,[leftcol]
 cmp cx,80
 jae .advance
 cmp al,32
 jb .dot
 cmp al,126
 jbe .print
.dot:
 mov al,'.'
.print:
 call putc
.advance:
 inc dx
 jmp .ch
.endrow:
 ; Clear only when fewer than 80 visible chars; avoid wrap/pending wrap.
 mov ax,dx
 sub ax,[leftcol]
 jc .clear
 cmp ax,80
 jae .nextrow
.clear:
 mov si,eraseol
 call puts
.nextrow:
 inc byte [row]
 cmp byte [row],23
 jae .status
 xor ax,ax
 mov al,[row]
 mov dx,1
 call position
 jmp .row
.status:
 mov ax,23
 mov dx,1
 call position
 mov si,statusseq
 call puts
 mov si,namebuf
 call puts
 mov si,linelabel
 call puts
 mov ax,[curline]
 inc ax
 call number
 mov si,columnlabel
 call puts
 mov ax,[curcol]
 inc ax
 call number
 mov si,byteslabel
 call puts
 mov ax,[length]
 call number
 cmp byte [dirty],0
 je .clean
 mov si,changed
 call puts
.clean:
 mov si,statusend
 call puts
 mov ax,24
 mov dx,1
 call position
 mov si,hint
 call puts
 mov ax,[curline]
 sub ax,[topline]
 inc ax
 mov dx,[curcol]
 sub dx,[leftcol]
 inc dx
 call position
 mov si,showcursor
 call puts
 ret
position:
 push ax
 mov si,csi
 call puts
 pop ax
 push dx
 call number
 mov al,';'
 call putc
 pop ax
 call number
 mov al,'H'
 call putc
 ret
number:
 push bx
 push cx
 push dx
 xor cx,cx
 mov bx,10
.loop:
 xor dx,dx
 div bx
 push dx
 inc cx
 or ax,ax
 jnz .loop
.out:
 pop ax
 add al,'0'
 call putc
 loop .out
 pop dx
 pop cx
 pop bx
 ret
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
 call bdos
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
 jz .done
 call putc
 jmp puts
.done:
 ret
rawkey:
 cmp byte [queued],0
 je .poll
 mov al,[queued]
 mov byte [queued],0
 xor ah,ah
 ret
.poll:
 push bx
 push cx
 push dx
 push si
 push di
 push bp
.loop:
 mov cl,6
 mov dl,255
 call bdos
 or al,al
 jz .loop
 xor ah,ah
 pop bp
 pop di
 pop si
 pop dx
 pop cx
 pop bx
 ret
; ESC alone opens commands. Finite poll separates ESC from CSI/SS3.
getkey:
 call rawkey
 cmp al,27
 je .escape
 ret
.escape:
 mov bp,6000
.wait:
 push bp
 mov cl,6
 mov dl,255
 call bdos
 pop bp
 or al,al
 jnz .prefix
 dec bp
 jnz .wait
 mov ax,27
 ret
.prefix:
 cmp al,'['
 je .seq
 cmp al,'O'
 je .seq
 mov [queued],al
 mov ax,27
 ret
.seq:
 call rawkey
 cmp al,'A'
 je .up
 cmp al,'B'
 je .down
 cmp al,'C'
 je .right
 cmp al,'D'
 je .left
 cmp al,'H'
 je .home
 cmp al,'F'
 je .end
 cmp al,'0'
 jb .ignore
 cmp al,'9'
 ja .ignore
 mov bl,al
 mov bp,16
.consume:
 call rawkey
 cmp al,40h
 jae .final
 dec bp
 jnz .consume
.ignore:
 xor ax,ax
 ret
.final:
 cmp al,'~'
 jne .ignore
 cmp bl,'1'
 je .home
 cmp bl,'7'
 je .home
 cmp bl,'4'
 je .end
 cmp bl,'8'
 je .end
 cmp bl,'3'
 je .delete
 cmp bl,'5'
 je .pgup
 cmp bl,'6'
 je .pgdn
 jmp .ignore
.left: mov ax,100h
 ret
.right: mov ax,101h
 ret
.up: mov ax,102h
 ret
.down: mov ax,103h
 ret
.home: mov ax,104h
 ret
.end: mov ax,105h
 ret
.pgup: mov ax,106h
 ret
.pgdn: mov ax,107h
 ret
.delete: mov ax,108h
 ret
prompt:
 push si
 mov ax,24
 mov dx,1
 call position
 mov si,eraseol
 call puts
 pop si
 call puts
 call readline
 ret
readline:
 mov di,inputbuf
 mov byte [di],0
.loop:
 call getkey
 cmp ax,255
 ja .loop
 cmp al,27
 je .cancel
 cmp al,13
 je .done
 cmp al,8
 je .back
 cmp al,127
 je .back
 cmp al,32
 jb .loop
 cmp al,126
 ja .loop
 cmp di,inputbuf+63
 jae .loop
 stosb
 call putc
 jmp .loop
.back:
 cmp di,inputbuf
 jbe .loop
 dec di
 mov si,backseq
 call puts
 jmp .loop
.cancel:
 mov di,inputbuf
.done:
 mov byte [di],0
 ret
message:
 call prompt
 ret
uppercase:
 push si
.loop:
 mov al,[si]
 or al,al
 jz .done
 cmp al,'a'
 jb .next
 cmp al,'z'
 ja .next
 sub byte [si],32
.next:
 inc si
 jmp .loop
.done:
 pop si
 ret
equals:
 push si
 push di
.loop:
 lodsb
 cmp al,[di]
 jne .done
 inc di
 or al,al
 jnz .loop
.done:
 pop di
 pop si
 ret
; Strict single CP/M filename, optional A:..P:, no wildcards or paths.
parse_name:
 mov si,namebuf
 call uppercase
 mov di,fcb
 mov cx,36
 xor ax,ax
 rep stosb
 mov di,fcb+1
 mov cx,11
 mov al,' '
 rep stosb
 cmp byte [si+1],':'
 jne .base
 lodsb
 sub al,'A'-1
 cmp al,1
 jb .bad
 cmp al,16
 ja .bad
 mov [fcb],al
 inc si
.base:
 mov di,fcb+1
 mov cx,8
 mov bp,0
.loop:
 lodsb
 or al,al
 jz .end
 cmp al,'.'
 je .ext
 or cx,cx
 jz .bad
 call validchar
 jc .bad
 stosb
 dec cx
 inc bp
 jmp .loop
.ext:
 or bp,bp
 jz .bad
 mov di,fcb+9
 mov cx,3
.extloop:
 lodsb
 or al,al
 jz .end
 or cx,cx
 jz .bad
 call validchar
 jc .bad
 stosb
 dec cx
 jmp .extloop
.end:
 or bp,bp
 jz .bad
 ; Reserve temporary extension $$$.
 cmp byte [fcb+9],'$'
 jne .ok
 cmp byte [fcb+10],'$'
 jne .ok
 cmp byte [fcb+11],'$'
 je .bad
.ok:
 clc
 ret
.bad:
 stc
 ret
validchar:
 cmp al,'!'
 jb .bad
 cmp al,126
 ja .bad
 push si
 mov si,forbidden
.loop:
 cmp byte [si],0
 je .ok
 cmp al,[si]
 je .invalid
 inc si
 jmp .loop
.invalid:
 pop si
.bad:
 stc
 ret
.ok:
 pop si
 clc
 ret
bdos:
 ; Preserve all registers except AX; DS/ES remain our segment.
 push bx
 push cx
 push dx
 push si
 push di
 push bp
 push ds
 push es
 int 0e0h
 pop es
 pop ds
 pop bp
 pop di
 pop si
 pop dx
 pop cx
 pop bx
 ret
dma:
 mov dx,dma_buf
 mov cl,26
 call bdos
 ret
load:
 call dma
 mov dx,fcb
 mov cl,15
 call bdos
 cmp al,255
 jne .read
 clc
 ret
.read:
 mov dx,fcb
 mov cl,20
 call bdos
 or al,al
 jnz .endfile
 mov si,dma_buf
 mov bp,128
.bytes:
 lodsb
 cmp al,26
 je .close
 cmp al,13
 je .next
 cmp al,9
 je .tab
 cmp al,10
 je .store
 cmp al,32
 jb .binary
 cmp al,126
 ja .binary
.store:
 cmp word [length],MAXTEXT
 jae .overflow
 mov bx,[length]
 mov [text+bx],al
 inc word [length]
 cmp al,10
 jne .column
 mov word [loadcol],0
 jmp .next
.column:
 inc word [loadcol]
.next:
 dec bp
 jnz .bytes
 jmp .read
.tab:
 mov cx,8
 mov ax,[loadcol]
 and ax,7
 sub cx,ax
.spaces:
 cmp word [length],MAXTEXT
 jae .overflow
 mov bx,[length]
 mov byte [text+bx],' '
 inc word [length]
 inc word [loadcol]
 loop .spaces
 jmp .next
.endfile:
 cmp al,1
 jne .overflow
.close:
 mov dx,fcb
 mov cl,16
 call bdos
 cmp al,255
 je .fail
 clc
 ret
.binary:
 mov byte [binary],1
.overflow:
 mov dx,fcb
 mov cl,16
 call bdos
.fail:
 stc
 ret
; Transaction: write temporary, close, remove old backup, rename original
; to backup, rename temporary to original. Never truncate the original.
save:
 mov si,fcb
 mov di,tempfcb
 mov cx,36
 rep movsb
 mov byte [tempfcb+9],'$'
 mov byte [tempfcb+10],'$'
 mov byte [tempfcb+11],'$'
 mov word [tempfcb+12],0
 mov word [tempfcb+14],0
 mov byte [tempfcb+32],0
 ; Refuse stale temporary file instead of deleting somebody else's data.
 mov dx,tempfcb
 mov cl,15
 call bdos
 cmp al,255
 je .create
 mov cl,16
 call bdos
 mov si,tempmsg
 call message
 stc
 ret
.create:
 mov cl,22
 call bdos
 cmp al,255
 je .error
 call dma
 mov word [savepos],0
 mov byte [pending],0
.record:
 mov di,dma_buf
 mov bp,128
.byte:
 cmp byte [pending],0
 jne .lf
 mov bx,[savepos]
 cmp bx,[length]
 jae .pad
 mov al,[text+bx]
 inc word [savepos]
 cmp al,10
 jne .emit
 mov byte [pending],1
 mov al,13
 jmp .emit
.lf:
 mov byte [pending],0
 mov al,10
 jmp .emit
.pad:
 mov al,26
.emit:
 stosb
 dec bp
 jnz .byte
 mov dx,tempfcb
 mov cl,21
 call bdos
 or al,al
 jnz .writeerror
 mov ax,[savepos]
 cmp ax,[length]
 jb .record
 cmp byte [pending],0
 jne .record
 mov cl,16
 call bdos
 cmp al,255
 je .error
 ; Prepare closed, reset FCBs.
 mov si,fcb
 mov di,backupfcb
 mov cx,36
 rep movsb
 mov byte [backupfcb+9],'B'
 mov byte [backupfcb+10],'A'
 mov byte [backupfcb+11],'K'
 cmp word [fcb+9],4142h
 jne .backup
 cmp byte [fcb+11],'K'
 jne .backup
 mov byte [backupfcb+10],'K'
 mov byte [backupfcb+11],'1'
.backup:
 mov dx,fcb
 mov cl,15
 call bdos
 cmp al,255
 je .install
 mov cl,16
 call bdos
 cmp al,255
 je .error
 mov dx,backupfcb
 mov cl,19
 call bdos
 ; Rename checks destination absence itself; old backup may not exist.
 mov si,fcb
 mov di,backupfcb
 call renamefile
 cmp al,255
 je .error
.install:
 mov si,tempfcb
 mov di,fcb
 call renamefile
 cmp al,255
 je .error
 mov byte [dirty],0
 mov si,savedmsg
 call message
 clc
 ret
.writeerror:
 mov cl,16
 call bdos
.error:
 mov si,savemsg
 call message
 stc
 ret
renamefile:
 ; CP/M rename: old drive/name bytes 0..11, new name bytes 16..27.
 push di
 mov di,renamefcb
 mov cx,36
 xor ax,ax
 rep stosb
 mov di,renamefcb
 mov cx,12
 rep movsb
 pop si
 mov di,renamefcb+16
 mov cx,12
 rep movsb
 mov dx,renamefcb
 mov cl,23
 call bdos
 ret
initseq db 27,'[?7l',27,'[0m',27,'[2J',0
clearseq db 27,'[0m',27,'[2J',27,'[H',0
hidehome db 27,'[?25l',27,'[H',0
showcursor db 27,'[?25h',0
exitseq db 27,'[0m',27,'[?7h',27,'[?25h',27,'[24;1H',27,'[K',13,10,0
csi db 27,'[',0
backseq db 8,' ',8,0
eraseol db 27,'[K',0
statusseq db 27,'[7m',27,'[K','EDIT86 ',0
statusend db 27,'[0m',0
linelabel db '  Line ',0
columnlabel db ' Col ',0
byteslabel db '  Bytes ',0
changed db ' *',0
hint db 'ESC commands  ^O save  ^Q quit  ^G find  ^S next  ^K copy ^X cut ^W paste',27,'[K',0
askname db 'EDIT86 - CP/M-86 VT100/ANSI text editor',13,10,'File (8.3): ',0
namemsg db 'Invalid filename. Use [A:..P:]NAME.EXT, no paths or wildcards.',13,10,0
loadmsg db 'Cannot load: read error, non-ASCII text, or over 40000 expanded bytes.',13,10,0
cmdprompt db 'Command (SAVE END QUIT HELP TOP BOTTOM): ',0
findprompt db 'Find (case sensitive): ',0
confirm db 'Discard unsaved changes? Type Y then Enter: ',0
unknown db 'Unknown command. Press Enter.',0
notfound db 'Not found to end of file. Press Enter.',0
fullmsg db 'Text limit is 40000 bytes. Press Enter.',0
clipmsg db 'Line exceeds 2048-byte clipboard. Press Enter.',0
tempmsg db 'Save refused: NAME.$$$ already exists. Press Enter.',0
savemsg db 'SAVE FAILED. Keep NAME.$$$ and .BAK for recovery. Press Enter.',0
savedmsg db 'Saved (previous file in .BAK, or .BK1 for .BAK input). Enter.',0
csave db 'SAVE',0
cquit db 'QUIT',0
chelp db 'HELP',0
ctop db 'TOP',0
cbottom db 'BOTTOM',0
forbidden db '"*+,./:;<=>?[\]|',0
helptext db 'EDIT86 - CP/M-86 VT100/ANSI edition',13,10,13,10
 db 'Arrows move; Home/End line; PageUp/PageDown screen; Delete removes right.',13,10
 db 'Enter splits a line; Backspace joins/removes left; Tab inserts spaces.',13,10
 db '^U left  ^F right  ^Z up  ^J down  ^A home  ^T top  ^B bottom',13,10
 db '^R page up  ^N delete whole line  ^K copy line  ^X cut line',13,10
 db '^W paste clipboard  ^D clear clipboard  ^G find  ^S find next',13,10
 db '^O save  ^Q quit (asks if changed)  ESC command prompt',13,10,13,10
 db 'Commands: SAVE, END (save/exit), QUIT, HELP, TOP, BOTTOM.',13,10
 db '40000 text bytes. ASCII text only. Tabs expand to spaces on loading.',13,10
 db 'CRLF output, CP/M Ctrl-Z record padding. Backup on successful save.',13,10
 db 'Independent implementation: no DOS shell, macros, printing or spooler.',13,10
 db '80 columns, 24 rows. Disable terminal XON/XOFF for Ctrl-S / Ctrl-Q.',13,10,13,10
 db 'Press any key to return.',0
length dw 0
cursor dw 0
topline dw 0
leftcol dw 0
curline dw 0
curcol dw 0
loadcol dw 0
clipsize dw 0
savepos dw 0
queued db 0
dirty db 0
binary db 0
pending db 0
row db 0
namebuf times 16 db 0
inputbuf times 64 db 0
pattern times 64 db 0
fcb times 36 db 0
tempfcb times 36 db 0
backupfcb times 36 db 0
renamefcb times 36 db 0
dma_buf times 128 db 0
clip times 2048 db 0
text times MAXTEXT db 0
 times 1024 db 0
stacktop:

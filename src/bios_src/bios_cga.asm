; BIOS source for 8086tiny IBM PC emulator (revision 1.25 and above). Compiles with NASM.
; Copyright 2013-14, Adrian Cable (adrian.cable@gmail.com) - http://www.megalith.co.uk/8086tiny
;
; Revision 2.34
;
; Modified by Julian Olds to work with CGA and provide more complete BIOS functions.
;
; This work is licensed under the MIT License. See included LICENSE.TXT.

	cpu	8086

; Here we define macros for some custom instructions that help the emulator talk with the outside
; world. They are described in detail in the hint.html file, which forms part of the emulator
; distribution.

%macro	extended_putchar_al 0
	db	0x0f, 0x00
%endmacro

%macro	extended_get_rtc 0
	db	0x0f, 0x01
%endmacro

%macro	extended_read_disk 0
	db	0x0f, 0x02
%endmacro

%macro	extended_write_disk 0
	db	0x0f, 0x03
%endmacro

org	100h				; BIOS loads at offset 0x0100

main:
	jmp	bios_entry

; Here go pointers to the different data tables used for instruction decoding

	dw	rm_mode12_reg1	; Table 0: R/M mode 1/2 "register 1" lookup
	dw	rm_mode012_reg2	; Table 1: R/M mode 1/2 "register 2" lookup
	dw	rm_mode12_disp	; Table 2: R/M mode 1/2 "DISP multiplier" lookup
	dw	rm_mode12_dfseg	; Table 3: R/M mode 1/2 "default segment" lookup
	dw	rm_mode0_reg1	; Table 4: R/M mode 0 "register 1" lookup
	dw	rm_mode012_reg2 ; Table 5: R/M mode 0 "register 2" lookup
	dw	rm_mode0_disp	; Table 6: R/M mode 0 "DISP multiplier" lookup
	dw	rm_mode0_dfseg	; Table 7: R/M mode 0 "default segment" lookup
	dw	xlat_ids	; Table 8: Translation of raw opcode index ("Raw ID") to function number ("Xlat'd ID")
	dw	ex_data		; Table 9: Translation of Raw ID to Extra Data
	dw	std_flags	; Table 10: How each Raw ID sets the flags (bit 1 = sets SZP, bit 2 = sets AF/OF for arithmetic, bit 3 = sets OF/CF for logic)
	dw	parity		; Table 11: Parity flag loop-up table (256 entries)
	dw	base_size	; Table 12: Translation of Raw ID to base instruction size (bytes)
	dw	i_w_adder	; Table 13: Translation of Raw ID to i_w size adder yes/no
	dw	i_mod_adder	; Table 14: Translation of Raw ID to i_mod size adder yes/no
	dw	jxx_dec_a	; Table 15: Jxx decode table A
	dw	jxx_dec_b	; Table 16: Jxx decode table B
	dw	jxx_dec_c	; Table 17: Jxx decode table C
	dw	jxx_dec_d	; Table 18: Jxx decode table D
	dw	flags_mult	; Table 19: FLAGS multipliers

; These values (BIOS ID string, BIOS date and so forth) go at the very top of memory

biosstr	db	'8086tiny BIOS Revision 2.25!', 0, 0		; Why not?
mem_top	db	0xea, 0, 0x01, 0, 0xf0, '03/26/14', 0, 0xfe, 0

bios_entry:

	; Set up initial stack to F000:F000

	mov	sp, 0xf000
	mov	ss, sp

	push	cs
	pop	es

	push	ax

	; The emulator requires a few control registers in memory to always be zero for correct
	; instruction decoding (in particular, register look-up operations). These are the
	; emulator's zero segment (ZS) and always-zero flag (XF). Because the emulated memory
	; space is uninitialised, we need to be sure these values are zero before doing anything
	; else. The instructions we need to use to set them must not rely on look-up operations.
	; So e.g. MOV to memory is out but string operations are fine.

	cld

	xor	ax, ax
	mov	di, 24
	stosw			; Set ZS = 0
	mov	di, 49
	stosb			; Set XF = 0

	; Now we can do whatever we want! DL starts off being the boot disk.

	mov	[cs:boot_device], dl

	; Set up CGA graphics support. We start with the adapter in text mode

	push	dx

	mov	dx, 0x3d8
	mov	al, 0x09
	out	dx, al		; Set CGA to hires text, video enabled

	mov	dx, 0x3d9
	mov	al, 0x07
	out	dx, al

	pop	dx

	pop	ax

	; Check cold boot/warm boot. We initialise disk parameters on cold boot only

	cmp	byte [cs:boot_state], 0	; Cold boot?
	jne	boot

	mov	byte [cs:boot_state], 1	; Set flag so next boot will be warm boot

	; First, set up the disk subsystem. Only do this on the very first startup, when
	; the emulator sets up the CX/AX registers with disk information.

	; Compute the cylinder/head/sector count for the HD disk image, if present.
	; Total number of sectors is in CX:AX, or 0 if there is no HD image. First,
	; we put it in DX:CX.

	mov	dx, cx
	mov	cx, ax

	mov	[cs:hd_secs_hi], dx
	mov	[cs:hd_secs_lo], cx

	cmp	cx, 0
	je	maybe_no_hd

	mov	word [cs:num_disks], 2
	jmp	calc_hd

maybe_no_hd:

	cmp	dx, 0
	je	no_hd

	mov	word [cs:num_disks], 2
	jmp	calc_hd

no_hd:

	mov	word [cs:num_disks], 1

calc_hd:

	mov	ax, cx
	mov	word [cs:hd_max_track], 1
	mov	word [cs:hd_max_head], 1

	cmp	dx, 0		; More than 63 total sectors? If so, we have more than 1 track.
	ja	sect_overflow
	cmp	ax, 63
	ja	sect_overflow

	mov	[cs:hd_max_sector], ax
	jmp	calc_heads

sect_overflow:

	mov	cx, 63		; Calculate number of tracks
	div	cx
	mov	[cs:hd_max_track], ax
	mov	word [cs:hd_max_sector], 63

calc_heads:

	mov	dx, 0		; More than 1024 tracks? If so, we have more than 1 head.
	mov	ax, [cs:hd_max_track]
	cmp	ax, 1024
	ja	track_overflow

	jmp	calc_end

track_overflow:

	mov	cx, 1024
	div	cx
	mov	[cs:hd_max_head], ax
	mov	word [cs:hd_max_track], 1024

calc_end:

	; Convert number of tracks into maximum track (0-based) and then store in INT 41
	; HD parameter table

	mov	ax, [cs:hd_max_head]
	mov	[cs:int41_max_heads], al
	mov	ax, [cs:hd_max_track]
	mov	[cs:int41_max_cyls], ax
	mov	ax, [cs:hd_max_sector]
	mov	[cs:int41_max_sect], al

	dec	word [cs:hd_max_track]
	dec	word [cs:hd_max_head]

; Main BIOS entry point. Zero the flags, and set up registers.

boot:	mov	ax, 0
	push	ax
	popf

	push	cs
	push	cs
	pop	ds
	pop	ss
	mov	sp, 0xf000

; Set up the IVT. First we zero out the table

	cld

	mov	ax, 0
	mov	es, ax
	mov	di, 0
	mov	cx, 512
	rep	stosw

; Then we load in the pointers to our interrupt handlers

	mov	di, 0
	mov	si, int_table
	mov	cx, [itbl_size]
	rep	movsb

; Set pointer to INT 41 table for hard disk

	mov	cx, int41
	mov	word [es:4*0x41], cx
	mov	cx, 0xf000
	mov	word [es:4*0x41 + 2], cx

; Set int 43 to the 8x8 double dot char table
	mov	cx, cga_glyphs
	mov	word [es:4*0x43], cx
	mov	cx, 0xf000
	mov	word [es:4*0x43 + 2], cx

; Set up last 16 bytes of memory, including boot jump, BIOS date, machine ID byte

	mov	ax, 0xffff
	mov	es, ax
	mov	di, 0x0
	mov	si, mem_top
	mov	cx, 16
	rep	movsb

; Set up the BIOS data area

	mov	ax, 0x40
	mov	es, ax
	mov	di, 0
	mov	si, bios_data
	mov	cx, 0x100
	rep	movsb

; Clear video memory

	mov	ax, 0xb800
	mov	es, ax
	mov	di, 0
	mov	cx, 80*25
	mov	ax, 0x0700
	rep	stosw

; copy cga font to the correct bios location
	mov	ax, cs
	mov	es, ax
	mov	di, glyphs8x8
	mov	ds, ax
	mov	si, cga_glyphs
	mov	cx, 1024
	rep	movsb

; Add BIOS redirects for 100% compatible vectors
	mov	ax, cs
	mov	es, ax
	mov	di, int09_bios_redirect
	mov	ds, ax
	mov	si, int09_redirect_start
	mov	cx, (int09_redirect_end - int09_redirect_start)
	rep	movsb


; Set initial CRTC register values and CGA registers for 80x25 colour mode
	mov	si, 0
init_crtc_loop:
	mov	dx, 0x3d4
	mov	ax, si
	out	dx, al
	mov	dx, 0x3d5
	mov	al, [cs:si+video_init_table+16]
        out	dx, al

	inc	si
	cmp	si, 16
	jl	init_crtc_loop	


	mov	dx, 0x3DA	; CGA status port
	mov	al, 0
	out	dx, al

	mov	dx, 0x3d8	; CGA Mode Control port
	mov	al, 0x09
	out	dx, al

	mov	dx, 0x3d9	; CGA Color Control port
	mov	al, 0x07
	out	dx, al

	mov	dx, 0x3BC	; LPT1
	mov	al, 0
	out	dx, al

	mov	dx, 0x40	; PIT channel 0
	mov	al, 0
	out	dx, al          ; Write LSB
	out	dx, al          ; Write MSB

	mov	dx, 0x61	; Keyboard ctrl / speaker ctrl
	mov	al, 0x50        ; kbd clock enable, RAM parity enable
	out	dx, al

	mov	dx, 0x62	; PPI - needed for memory parity checks
	mov	al, 0
	out	dx, al

; Enable interrupts
	sti

; Read boot sector from FDD, and load it into 0:7C00

	mov	ax, 0
	mov	es, ax

	mov	ax, 0x0201
	mov	dh, 0
	mov	dl, [cs:boot_device]
	mov	cx, 1
	mov	bx, 0x7c00
	int	13h

; Jump to boot sector

	jmp	0:0x7c00

; ************************* INT 7h handler - no longer in use

int7:
	iret

; ************************* INT 8h handler - timer

int8:	
	; timer interupt evert 1/18.2 seconds

	push	ax
	push	bx
	push	dx
	push	bp
	push	es

	push	cx
	push	di
	push	ds
	push	si

	mov	bx, 0x40
	mov	es, bx

	; Increment 32-bit BIOS timer tick counter, once every 8 calls
	; NOT Required. I call this at the right rate!
	;cmp	byte [cs:int8_call_cnt], 8
	;jne	skip_timer_increment

	add	word [es:0x6C], 1
	adc	word [es:0x6E], 0

i8_end:	

	pop	si
	pop	ds
	pop	di
	pop	cx

	pop	es
	pop	bp
	pop	dx
	pop	bx
	pop	ax

	int	0x1c

	iret

; ************************* INT 09h handler - keyboard data ready

int9:

	push	ax
	push	bx
	push	cx
	push	dx
	push	ds
	push	es
	push	bp

	; get cs into ds for xlatb
	push	cs
	pop	ds

	mov	bx, 0x40
	mov	es, bx

	; Tail of the BIOS keyboard buffer goes in BP. This is where we add new keystrokes
	mov	bp, [es:kbbuf_tail-bios_data]

	; get the next scan code into AL
	mov	dx, 0x0060
	in 	al, dx

	; if it is a key down code then process the key down
	test	al, 0x80
	je	int09_key_down

	; It is a key up, always use the normal scan to ascii table.
	mov	ah, al
	mov	bx, scan2a_tbl
	xlatb

	test	al, 0x80
	je	int09_done
	mov	ah, al
	and	ah, 0xf0
	and	al, 0x0f

	cmp	ah, 0x80
	je	int09_shift_up

	jmp	int09_done

int09_shift_up:
	mov	cl, al
	mov	bl, 1
	shl	bl, cl
	not 	bl
	and	[es:keyflags1-bios_data], bl
	jmp	int09_done


int09_key_down:
	mov	ah, al ; Scan code into ah

	test	byte [es:keyflags1-bios_data], 0x03  ; Shift key : bits 0 | 1
	jne	int09_key_down_shift 
	test	byte [es:keyflags1-bios_data], 0x04  ; Ctrl key  : bit 2
	jne	int09_key_down_ctrl
	test	byte [es:keyflags1-bios_data], 0x08  ; Alt key   : bit 3
	jne	int09_key_down_alt

	mov	bx, scan2a_tbl
	jmp	int09_key_down_cont

int09_key_down_shift:
	mov	bx, sscan2a_tbl
	jmp	int09_key_down_cont
		
int09_key_down_ctrl:
	mov	bx, cscan2a_tbl
	jmp	int09_key_down_cont

int09_key_down_alt:
	mov	bx, ascan2a_tbl

int09_key_down_cont:
	xlatb
	mov	bx, ax             ; bh = scan code, bl = translated scan code
	test	al, 0x80
	je	int09_use_keydown
	mov	ah, al
	and	ah, 0xf0           ; ah = special function
	and	al, 0x0f           ; al = function data

	cmp	ah, 0x80
	je	int09_shift_down

	cmp	ah, 0x90
	je	int09_shift_toggle

	cmp	ah, 0xa0
	je	int09_scancode_mod

	jmp	int09_done

int09_shift_down:
	mov	cl, al
	mov	bl, 1
	shl	bl, cl
	or	[es:keyflags1-bios_data], bl

	jmp int09_done

int09_shift_toggle:
	mov	cl, al
	mov	bl, 1
	shl	bl, cl
	xor	[es:keyflags1-bios_data], bl

	jmp int09_done

int09_scancode_mod:
	cmp al, 0x00
	je	int09_scancode_mod_altnum
	cmp	al, 0x01
	je	int09_scancode_mod_altfn
	cmp	al, 0x02
	je	int09_scancode_mod_ctrlfn
	cmp	al, 0x03
	jne	int09_done

	; Shift0-fn
	add	bh, 0x19
	mov	bl, 0x00
	jmp	int09_store_key	

int09_scancode_mod_altnum:
	add	bh, 0x76
	mov	bl, 0x00
	jmp	int09_store_key	

int09_scancode_mod_altfn:
	add	bh, 0x2d
	mov	bl, 0x00
	jmp	int09_store_key	

int09_scancode_mod_ctrlfn:
	add	bh, 0x23
	mov	bl, 0x00
	jmp	int09_store_key	

int09_use_keydown:
	test	byte [es:keyflags1-bios_data], 0x20
	jne	int09_num_lock
	jmp	int09_test_capslock

int09_num_lock:
	cmp	bh, 0x47
	jl	int09_test_capslock
	cmp	bh, 0x53
	jg	int09_test_capslock
	mov ax, bx
	mov al, ah
	mov	bx, sscan2a_tbl 
	xlatb
	mov	bx, ax

int09_test_capslock:
	test	byte [es:keyflags1-bios_data], 0x40
	jne	int09_caps_lock

	jmp	int09_store_key

int09_caps_lock:
	cmp	bl, 0x61
	jl	int09_store_key
	cmp	bl, 0x7a
	jg	int09_store_key
	sub	bl, 0x20
	jmp	int09_store_key

int09_store_key:
	mov	[es:bp], bx

	add	word [es:kbbuf_tail-bios_data], 2
	call	kb_adjust_buf ; Wrap the tail around the head if the buffer gets too large
        
int09_done:
	pop	bp
	pop	es
	pop	ds
	pop	dx
	pop	cx
	pop	bx
	pop	ax

	iret

; ************************* INT 10h handler - video services

int10: 
	cmp	ah, 0x00 ; Set video mode
	je	int10_set_vm
	cmp	ah, 0x01 ; Set cursor shape
	je	int10_set_cshape
	cmp	ah, 0x02 ; Set cursor position
	je	int10_set_cursor
	cmp	ah, 0x03 ; Get cursor position
	je	int10_get_cursor
	cmp	ah, 0x05 ; Set active display page
	je	int10_set_disp_page
	cmp	ah, 0x06 ; Scroll up window
	je	int10_scrollup
	cmp	ah, 0x07 ; Scroll down window
	je	int10_scrolldown
	cmp	ah, 0x08 ; Get character at cursor
	je	int10_charatcur
	cmp	ah, 0x09 ; Write char and attribute
	je	int10_write_char_attrib
	cmp	ah, 0x0a ; Write char only
	je	int10_write_char
	cmp	ah, 0x0b ; set colour
	je	int10_set_colour
	cmp	ah, 0x0e ; Write character at cursor position, tty mode
	je	int10_write_char_tty
	cmp	ah, 0x0f ; Get video mode
	je	int10_get_vm
	cmp	ah, 0x10 ; Get/Set Palette registers
	je	int10_palette
	cmp	ax, 0x1123 ; Set int43 to 8x8 double dot font
	je	int10_set_int43_8x8dd
	cmp	ax, 0x1130 ; get font information
	je	int10_get_font_info 
	cmp	ah, 0x12 ; Video sub-system configure
	je	int10_video_subsystem_cfg
	cmp	ah, 0x1a ; Video combination
	je	int10_vid_combination
	cmp	ah, 0x1b
	je	int10_get_state_info


        ; debug code for unsupported int 10h calls

	push	ax
	push	bx
	mov	bx, ax
	mov	al, 'I'
	extended_putchar_al
	mov	al, 'N'
	extended_putchar_al
	mov	al, 'T'
	extended_putchar_al
	mov	al, '1'
	extended_putchar_al
	mov	al, '0'
	extended_putchar_al
	mov	al, ' '
	extended_putchar_al
	mov	al, bh
	call	puts_hex_al
	mov	al, bl
	call	puts_hex_al
	mov	al, ' '
	extended_putchar_al
	pop	bx
	mov	al, bh
	call	puts_hex_al
	mov	al, bl
	call	puts_hex_al
	mov	al, ' '
	extended_putchar_al
	mov	al, ch
	call	puts_hex_al
	mov	al, cl
	call	puts_hex_al	
	mov	al, ' '
	extended_putchar_al
	mov	al, dh
	call	puts_hex_al
	mov	al, dl
	call	puts_hex_al
	mov	al, 0x0a
	extended_putchar_al
	mov	al, 0x0d
	extended_putchar_al
	pop	ax

	iret

int10_set_vm:

	; AL contains the requested mode.
	; CGA supports modes 0 to 6 only
	; MCGA adds modes 11h and 13h

	push	ds
	push	dx
	push	cx
	push	bx
	push	ax

	push ax
	; Get the bios data segment into ds
	mov	ax, 0x40
	mov	ds, ax

	; Disable video out when changing modes
	mov	dx, 0x3d8
	mov	al, 0
	out	dx, al
	pop	ax

;debug - display requested video mode
	push	ax
	mov	al, 'V'
	extended_putchar_al
	mov	al, 'I'
	extended_putchar_al
	mov	al, 'D'
	extended_putchar_al
	pop	ax
	call	puts_hex_al
	push	ax
	mov	al, 0x0a
	extended_putchar_al
	mov	al, 0x0d
	extended_putchar_al
	pop	ax

	mov	ah, al
	and	ax, 0x807f
	push	ax

	; Store the video mode. This if fixed later for invalid modes.
	mov	byte [vid_mode-bios_data], al

	; Check MCGA only video modes
	cmp	al, 0x11
	je	int10_set_vm_11
	cmp	al, 0x13
	je	int10_set_vm_13

	; Get back into CGA emulation mode

	push	ax

	; Set sequencer registers
	mov	ax, 0x03c4
	mov	dx, ax
	mov	al, 0x01
	out	dx, al
	mov	ax, 0x03c5
	mov	dx, ax
	mov	al, 0x01
	out	dx, al

	mov	ax, 0x03c4
	mov	dx, ax
	mov	al, 0x03
	out	dx, al
	mov	ax, 0x03c5
	mov	dx, ax
	mov	al, 0x00
	out	dx, al

	mov	ax, 0x03c4
	mov	dx, ax
	mov	al, 0x04
	out	dx, al
	mov	ax, 0x03c5
	mov	dx, ax
	mov	al, 0x04
	out	dx, al

	; Set the Graphics mode register 3 to 0x00.
	mov	ax, 0x03ce
	mov	dx, ax
	mov	al, 0x03
	out	dx, al
	mov	ax, 0x03cf
	mov	dx, ax
	mov	al, 0x00
	out	dx, al

	; Set the Graphics mode register 5 to 0x10.
	mov	ax, 0x03ce
	mov	dx, ax
	mov	al, 0x05
	out	dx, al
	mov	ax, 0x03cf
	mov	dx, ax
	mov	al, 0x10
	out	dx, al

	; Set the Graphics mode register 8 to 0xff.
	mov	ax, 0x03ce
	mov	dx, ax
	mov	al, 0x08
	out	dx, al
	mov	ax, 0x03cf
	mov	dx, ax
	mov	al, 0xff
	out	dx, al

	pop	ax

	; Check CGA video modes
	cmp	al, 0
	je	int10_set_vm_0
	cmp	al, 1
	je	int10_set_vm_1
	cmp	al, 2
	je	int10_set_vm_2
	cmp	al, 3
	je	int10_set_vm_3
	cmp	al, 4
	je	int10_set_vm_4
	cmp	al, 5
	je	int10_set_vm_5
	cmp	al, 6
	je	int10_set_vm_6

	; All other video modes set mode 3
	mov	al, 3
	mov	byte [vid_mode-bios_data], al
	jmp	int10_set_vm_3

    int10_set_vm_0:
	; BW40
	mov	word [vid_page_size-bios_data], 0x0800
	mov	ax, 0x000c
	mov	bx, 40		; BX = video columns
	mov	cx, 0x0818	; CL = video rows, CH = scan lines per character
	jmp	int10_set_vm_write

    int10_set_vm_1:
	; CO40
	mov	word [vid_page_size-bios_data], 0x0800
	mov	ax, 0x0008
	mov	bx, 40		; BX = video columns
	mov	cx, 0x0818	; CL = video rows, CH = scan lines per character
	jmp	int10_set_vm_write

    int10_set_vm_2:
	; BW80
	mov	word [vid_page_size-bios_data], 0x1000
	mov	ax, 0x000d
	mov	bx, 80		; BX = video columns
	mov	cx, 0x0818	; CL = video rows, CH = scan lines per character
	jmp	int10_set_vm_write

    int10_set_vm_3:
	; CO80
	mov	word [vid_page_size-bios_data], 0x1000
	mov	ax, 0x0009
	mov	bx, 80		; BX = video columns
	mov	cx, 0x0818	; CL = video rows, CH = scan lines per character
	jmp	int10_set_vm_write

    int10_set_vm_4:
	; 320x200, 4 colour
	mov	word [vid_page_size-bios_data], 0x4000
	mov	ax, 0x000a
	mov	bx, 40		; BX = video columns
	mov	cx, 0x0818	; CL = video rows, CH = scan lines per character
	jmp	int10_set_vm_write

    int10_set_vm_5:
	; 320x200 grey
	mov	word [vid_page_size-bios_data], 0x4000
	mov	ax, 0x000e
	mov	bx, 40		; BX = video columns
	mov	cx, 0x0818	; CL = video rows, CH = scan lines per character
	jmp	int10_set_vm_write

    int10_set_vm_6:
	; 640x200 mono

	; Set int 43 to the 8x8 single dot char table
	push	es
	mov		ax, 0
	mov		es, ax
	mov	cx, cga_glyphs
	mov	word [es:4*0x43], cx
	mov	cx, 0xf000
	mov	word [es:4*0x43 + 2], cx
	pop		es

	mov	word [vid_page_size-bios_data], 0x4000
	mov	ax, 0x071a
	mov	bx, 80		; BX = video columns
	mov	cx, 0x0818	; CL = video rows, CH = scan lines per character
	jmp	int10_set_vm_write

    int10_set_vm_11:
	; 640x480, 2 colour
	; Reset Attribute Controller to index mode
	mov	ax, 0x03da
	mov	dx, ax
	in	al, dx

	; Update Attribute controller registers
	mov	ax, 0x03c0
	mov	dx, ax

	mov	al, 0x10
	out	dx, al
	mov	al, 0x01
	out	dx, al

	mov	al, 0x11
	out	dx, al
	mov	al, 0x00
	out	dx, al

	mov	al, 0x12
	out	dx, al
	mov	al, 0x0f
	out	dx, al

	mov	al, 0x13
	out	dx, al
	mov	al, 0x00
	out	dx, al

	mov	al, 0x14
	out	dx, al
	mov	al, 0x00
	out	dx, al

	; set Misc Output Register
	mov	ax, 0x03c2
	mov	dx, ax
	mov	al, 0x63
	out	dx, al

	; Set Sequence Registers
	mov	ax, 0x03c4
	mov	dx, ax
	mov	al, 0x01
	out	dx, al
	mov	ax, 0x03c5
	mov	dx, ax
	mov	al, 0x01
	out	dx, al

	mov	ax, 0x03c4
	mov	dx, ax
	mov	al, 0x03
	out	dx, al
	mov	ax, 0x03c5
	mov	dx, ax
	mov	al, 0x00
	out	dx, al

	mov	ax, 0x03c4
	mov	dx, ax
	mov	al, 0x04
	out	dx, al
	mov	ax, 0x03c5
	mov	dx, ax
	mov	al, 0x02
	out	dx, al

	; Set Graphics controller registers
	mov	ax, 0x03ce
	mov	dx, ax
	mov	al, 0x05
	out	dx, al
	mov	ax, 0x03cf
	mov	dx, ax
	mov	al, 0x00
	out	dx, al

	mov	ax, 0x03ce
	mov	dx, ax
	mov	al, 0x06
	out	dx, al
	mov	ax, 0x03cf
	mov	dx, ax
	mov	al, 0x05
	out	dx, al

	; Set int 43 to the 8x16 single dot char table
	push	es
	mov		ax, 0
	mov		es, ax
	mov	cx, vga_glyphs
	mov	word [es:4*0x43], cx
	mov	cx, 0xf000
	mov	word [es:4*0x43 + 2], cx
	pop		es


	mov	bx, 80	; BX = video columns
	mov	cx, 0x101d	; CL = video rows, CH = scan lines per character


	jmp	int10_set_vm_upd

    int10_set_vm_13:
	; 320x200, 256 colour
	; Reset Attribute Controller to index mode
	mov	ax, 0x03da
	mov	dx, ax
	in	al, dx

	; Update Attribute controller registers
	mov	ax, 0x03c0
	mov	dx, ax

	mov	al, 0x10
	out	dx, al
	mov	al, 0x41
	out	dx, al

	mov	al, 0x11
	out	dx, al
	mov	al, 0x00
	out	dx, al

	mov	al, 0x12
	out	dx, al
	mov	al, 0x0f
	out	dx, al

	mov	al, 0x13
	out	dx, al
	mov	al, 0x00
	out	dx, al

	mov	al, 0x14
	out	dx, al
	mov	al, 0x00
	out	dx, al

	; set Misc Output Register
	mov	ax, 0x03c2
	mov	dx, ax
	mov	al, 0x63
	out	dx, al

	; Set Sequence Registers
	mov	ax, 0x03c4
	mov	dx, ax
	mov	al, 0x01
	out	dx, al
	mov	ax, 0x03c5
	mov	dx, ax
	mov	al, 0x01
	out	dx, al

	mov	ax, 0x03c4
	mov	dx, ax
	mov	al, 0x03
	out	dx, al
	mov	ax, 0x03c5
	mov	dx, ax
	mov	al, 0x00
	out	dx, al

	mov	ax, 0x03c4
	mov	dx, ax
	mov	al, 0x04
	out	dx, al
	mov	ax, 0x03c5
	mov	dx, ax
	mov	al, 0x02
	out	dx, al

	; Set Graphics controller registers
	mov	ax, 0x03ce
	mov	dx, ax
	mov	al, 0x05
	out	dx, al
	mov	ax, 0x03cf
	mov	dx, ax
	mov	al, 0x40
	out	dx, al

	mov	ax, 0x03ce
	mov	dx, ax
	mov	al, 0x06
	out	dx, al
	mov	ax, 0x03cf
	mov	dx, ax
	mov	al, 0x05
	out	dx, al

	mov	bx, 40		; BX = video columns
	mov	cx, 0x0818	; CL = video rows, CH = scan lines per character

	jmp	int10_set_vm_upd

    int10_set_vm_write:
	; CGA video modes
	; write the video data to the video mode registers and bios data area
	; al = port 0x3d8 value, ah = port 0x3d9 value
	; bx = video text columns
	xchg	ah, al
	mov	dx, 0x3d9
	out	dx, al
	mov	byte [vid_3x9-bios_data], al

	xchg	ah, al
	mov	dx, 0x3d8
	out	dx, al
	mov	byte [vid_3x8-bios_data], al


    int10_set_vm_upd:
	mov	word [vid_cols-bios_data], bx
	mov	word [vid_rows-bios_data], cx
	mov	byte [disp_page-bios_data], 0x00
	mov	word [vid_page_offset-bios_data], 0x0000

	; Set all page cursors back to 0, 0
	mov	word [curpos_x-bios_data], 0
	mov	word [curpos_x+2-bios_data], 0
	mov	word [curpos_x+4-bios_data], 0
	mov	word [curpos_x+6-bios_data], 0
	mov	word [curpos_x+8-bios_data], 0
	mov	word [curpos_x+10-bios_data], 0
	mov	word [curpos_x+12-bios_data], 0
	mov	word [curpos_x+14-bios_data], 0

    int10_set_vm_cls:
	pop	ax
	cmp	ah, 0x80
	je	int10_set_vm_nocls

	mov	bh, 7		        ; Black background, white foreground
	call	clear_screen

    int10_set_vm_nocls:
	pop	ax
	pop	bx
	pop	cx
	pop	dx
	pop	ds
	iret

int10_set_cshape:
	; CH = cursor start line (bits 0-4) and options (bits 5-7).
	; CL = bottom cursor line (bits 0-4).

	push	ds
	push	ax
	push	cx
	push	dx

	mov	ax, 0x40
	mov	ds, ax

	mov	ax, cx
	and	ch, 01100000b
	cmp	ch, 00100000b
	jne	cur_visible

    cur_not_visible:
	mov	byte [cursor_visible-bios_data], 0	; Hide cursor
	mov	cx, ax
	jmp	cur_vischk_done

    cur_visible:
	; Only store cursor shape if visible.
	and	ax, 0x1f1f
	mov	[cur_v_end-bios_data], ax
	or	ax, 0x6000
	mov	cx, ax
	mov	byte [cursor_visible-bios_data], 1	; Show cursor
    
    cur_vischk_done:
	; Set CRTC registers for cursor shape
	mov	al, 0x0a
	mov	dx, 0x3d4
	out	dx, al

	mov	al, ch
	mov	dx, 0x3d5
	out	dx, al

	mov	al, 0x0b
	mov	dx, 0x3d4
	out	dx, al

	mov	al, cl
	mov	dx, 0x3d5
	out	dx, al

    set_cshape_done:
	pop	dx
	pop	cx
	pop	ax
	pop	ds
	iret

int10_set_cursor:
	; DH = row.
	; DL = column.
	; BH = page number (0..7).

	push	ds
	push	si
	push	ax
	push	dx

	mov	ax, 0x40
	mov	ds, ax

	cmp	bh, 0x07
	jg	int10_set_cursor_done

	push	bx
	mov	bl, bh
	mov	bh, 0
	shl	bx, 1
	mov	si, bx
	pop	bx

	mov	[si+curpos_y-bios_data], dh
	mov	[si+curpos_x-bios_data], dl

	cmp	bh, [disp_page-bios_data]
	jne	int10_set_cursor_done

	mov	[crt_curpos_y-bios_data], dh
	mov	[crt_curpos_x-bios_data], dl
	call	set_crtc_cursor_pos

    int10_set_cursor_done:
	pop	dx
	pop	ax
	pop	si
	pop	ds
	iret

int10_get_cursor:
	; On entry:
	;   BH = page number (0..7).
	; At exit:
	;   CH, CL = beginning line of cursor, ending line of cursor
	;   DH, DL = row, column

	push	ds
	push	si
	push	ax

	mov	ax, 0x40
	mov	ds, ax

	mov	cx, [cur_v_end-bios_data]

	cmp	bh, 0x07
	jg	int10_get_cursor_done

	push	bx
	mov	bl, bh
	mov	bh, 0
	shl	bx, 1
	mov	si, bx
	pop	bx

	mov	dh, [si+curpos_y-bios_data]
	mov	dl, [si+curpos_x-bios_data]

    int10_get_cursor_done:
	pop	ax
	pop	si
	pop	ds

	iret

int10_set_disp_page:
	; On entry:
	;   AL = page number

	push	ds
	push	si
	push	dx
	push	bx	
	push	ax

	push	ax
	mov	al, 'P'
	extended_putchar_al
	mov	al, 'G'
	extended_putchar_al
	mov	al, ' '
	extended_putchar_al
	pop	ax
	call	puts_hex_al
	push	ax
	mov	al, 0x0a
	extended_putchar_al
	mov	al, 0x0d
	extended_putchar_al
	pop	ax

	; SI = display page cursor pos offset
	mov	bl, al
	mov	bh, 0
	shl	bx, 1
	mov	si, bx

	mov	bx, 0x40
	mov	ds, bx

	cmp	byte [vid_mode-bios_data], 0x00
	je	int10_set_disp_page_t40

	cmp	byte [vid_mode-bios_data], 0x01
	je	int10_set_disp_page_t40

	cmp	byte [vid_mode-bios_data], 0x02
	je	int10_set_disp_page_t80

	cmp	byte [vid_mode-bios_data], 0x03
	je	int10_set_disp_page_t80

	cmp	byte [vid_mode-bios_data], 0x04
	je	int10_set_disp_page_done

	cmp	byte [vid_mode-bios_data], 0x05
	je	int10_set_disp_page_done

	cmp	byte [vid_mode-bios_data], 0x06
	je	int10_set_disp_page_done

	jmp	int10_set_disp_page_done

    int10_set_disp_page_t40:
	cmp	al, 8
	jge	int10_set_disp_page_done

	mov	[disp_page-bios_data], al
	mov	bl, 0x08
	mul	byte bl
	mov	bh, al
	mov	bl, 0

	jmp	int10_set_disp_page_upd

    int10_set_disp_page_t80:
	cmp	al, 8
	jge	int10_set_disp_page_done	

	mov	[disp_page-bios_data], al
	mov	bl, 0x10
	mul	byte bl
	mov	bh, al
	mov	bl, 0

    int10_set_disp_page_upd:
	; bx contains page offset, so store it in the BIOS data area
	mov	[vid_page_offset-bios_data], bx

	; update CRTC page offset
	mov	dx, 0x03d4
	mov	al, 0x0c
	out	dx, al

	mov	dx, 0x03d5
	mov	al, bh
	out	dx, al

	mov	dx, 0x03d4
	mov	al, 0x0d
	out	dx, al

	mov	dx, 0x03d5
	mov	al, bl
	out	dx, al
	
	; update CRTC cursor pos
	mov	dl, [si+curpos_x-bios_data]
	mov	[crt_curpos_x-bios_data], dl
	mov	dl, [si+curpos_y-bios_data]
	mov	[crt_curpos_y-bios_data], dl
	call	set_crtc_cursor_pos

    int10_set_disp_page_done:
	pop	ax
	pop	bx
	pop	dx
	pop	si
	pop	ds
	iret

int10_scrollup:
	; AL = number of lines by which to scroll up (00h = clear entire window)
	; BH = attribute used to write blank lines at bottom of window
	; CH,CL = row,column of window's upper left corner
	; DH,DL = row,column of window's lower right corner
	push	bp

	push	ax
	push	ds
	mov	ax, 0x40
	mov	ds, ax
	mov	bp, [vid_page_offset-bios_data]
	pop	ds
	pop	ax

	cmp	al, 0
	jne	int10_scroll_up_window

	call	clear_window
	jmp	int10_scrollup_done

    int10_scroll_up_window:
	
	call	scroll_up_window

    int10_scrollup_done:
	pop	bp
	iret
 
int10_scrolldown:
	; AL = number of lines by which to scroll down (00h = clear entire window)
	; BH = attribute used to write blank lines at bottom of window
	; CH,CL = row,column of window's upper left corner
	; DH,DL = row,column of window's lower right corner

	push	bp

	push	ax
	push	ds
	mov	ax, 0x40
	mov	ds, ax
	mov	bp, [vid_page_offset-bios_data]
	pop	ds
	pop	ax

	cmp	al, 0
	jne	int10_scroll_down_window

	call	clear_window
	jmp	int10_scrolldown_done

    int10_scroll_down_window:
	call	scroll_down_window

    int10_scrolldown_done:
	pop	bp
	iret


int10_charatcur:
	; This returns the character at the cursor.
	;   BH = display page
	; On exit:
	;   AH, AL = attribute code, ASCII character code
  
	push	ds
	push	es
	push	si
	push	di
	push	bx

	mov	ax, 0x40
	mov	ds, ax

	cmp	byte [vid_mode-bios_data], 0x00
	je	int10_charatcur_t40
	cmp	byte [vid_mode-bios_data], 0x01
	je	int10_charatcur_t40
	cmp	byte [vid_mode-bios_data], 0x02
	je	int10_charatcur_t80
	cmp	byte [vid_mode-bios_data], 0x03
	je	int10_charatcur_t80

	; Can't do this in graphics mode
	cmp	byte [vid_mode-bios_data], 0x04
	jge	int10_charatcur_done

    int10_charatcur_t40:
	cmp	bh, 0x07
	jg	int10_charatcur_done
	jmp	int10_charatcur_page_ok

    int10_charatcur_t80:
	cmp	bh, 0x07
	jg	int10_charatcur_done

    int10_charatcur_page_ok:
	; Get bios data cursor position offset for this page into SI
	push	bx
	mov	bl, bh
	mov	bh, 0
	shl	bx, 1
	mov	si, bx
	pop	bx

	; Get video page offset into DI
	mov	al, [vid_page_size+1-bios_data]
	mul	byte bh
	mov	ah, al
	mov	al, 0
	mov	di, ax

	; Get video RAM offset for the cursor position into BX
	mov	al, [si+curpos_y-bios_data]
	mul	byte [vid_cols-bios_data]
	add	al, [si+curpos_x-bios_data]
	adc	ah, 0
	shl	ax, 1
	mov	bx, ax

	; Get video RAM segment into ES
	mov	ax, 0xb800
	mov	es, ax

	mov	ax, [es:bx+di]

    int10_charatcur_done:
	pop	bx
	pop	di
	pop	si
	pop	es
	pop	ds

	iret


int10_write_char_tty:
  ; AL = character to display
  ; BH = page number
  ; BL = foreground pixel colour (gfx mode only)

	push	ds
	push	es
	push	ax
	push	bx
	push    cx
	push	si
	push	di
	push	bp

	push	ax
	mov	ax, 0x40
	mov	ds, ax
	pop	ax

	cmp	byte [vid_mode-bios_data], 0x00
	je	int10_write_char_tty_t40
	cmp	byte [vid_mode-bios_data], 0x01
	je	int10_write_char_tty_t40
	cmp	byte [vid_mode-bios_data], 0x02
	je	int10_write_char_tty_t80
	cmp	byte [vid_mode-bios_data], 0x03
	je	int10_write_char_tty_t80

	; Not a text mode, so only page 1 is valid.
	cmp	bh, 0
	jne	int10_write_char_tty_done
	jmp	int10_write_char_tty_page_ok

    int10_write_char_tty_t40:
	cmp	bh, 0x07
	jg	int10_write_char_tty_done
	jmp	int10_write_char_tty_page_ok

    int10_write_char_tty_t80:
	cmp	bh, 0x07
	jg	int10_write_char_tty_done

    int10_write_char_tty_page_ok:
	; Get bios data cursor position offset for this page into SI
	push	bx
	mov	bl, bh
	mov	bh, 0
	shl	bx, 1
	mov	si, bx
	pop	bx

	push	ax

	; Get video page offset into BP
	mov	al, [vid_page_size+1-bios_data]
	mul	byte bh
	mov	ah, al
	mov	al, 0
	mov	bp, ax

	; Get offset for the cursor position within the page into DI
	mov	al, [si+curpos_y-bios_data]
	mul	byte [vid_cols-bios_data]
	add	al, [si+curpos_x-bios_data]
	adc	ah, 0
	shl	ax, 1
	mov	di, ax

	; Get video RAM segment into ES
	mov	ax, 0xb800
	mov	es, ax

	pop	ax

	cmp	al, 0x08
	je	int10_write_char_tty_bs

	cmp     al, 0x0A
	je	int10_write_char_tty_nl

	cmp     al, 0x0D
	je	int10_write_char_tty_cr

	jmp     int10_write_char_tty_printable

    int10_write_char_tty_bs:
	mov	byte [es:bp+di], 0x20
	dec	byte [si+curpos_x-bios_data]
	cmp	byte [si+curpos_x-bios_data], 0
	jg	int10_write_char_tty_set_crtc_cursor

	mov	byte [si+curpos_x-bios_data], 0    
	jmp	int10_write_char_tty_set_crtc_cursor

    int10_write_char_tty_nl:
	mov	byte [si+curpos_x-bios_data], 0
	inc	byte [si+curpos_y-bios_data]

	mov	cl, [vid_rows-bios_data]
	cmp	byte [si+curpos_y-bios_data], cl
	jbe	int10_write_char_tty_done
	mov	byte [si+curpos_y-bios_data], cl

	push    ax
	push	cx
	push	dx

	mov     al, 1
	mov	bx, 0x0700
	mov	cx, 0
	mov	dh, [vid_rows-bios_data]
	mov	dl, [vid_cols-bios_data]
	dec	dl

	call	scroll_up_window

	pop	dx
	pop	cx
	pop	ax

	jmp	int10_write_char_tty_set_crtc_cursor

    int10_write_char_tty_cr:
	mov	byte [si+curpos_x-bios_data],0
	jmp	int10_write_char_tty_set_crtc_cursor

    int10_write_char_tty_printable:
	cmp 	byte [vid_mode-bios_data], 4
	je 	int10_write_char_tty_cga320
	cmp 	byte [vid_mode-bios_data], 5
	je 	int10_write_char_tty_cga320
	cmp 	byte [vid_mode-bios_data], 6
	je 	int10_write_char_tty_cga640
	cmp	byte [vid_mode-bios_data], 0x11
	je	int10_write_char_tty_mode11
	cmp	byte [vid_mode-bios_data], 0x13
	je	int10_write_char_tty_mode13

	jmp	int10_write_char_tty_text

    int10_write_char_tty_cga320:
	call	put_cga320_char
	jmp	int10_write_char_tty_upd_cursor

    int10_write_char_tty_cga640:
	call	put_cga640_char 
	jmp	int10_write_char_tty_upd_cursor

    int10_write_char_tty_mode11:
	call	put_mode11_char 
	jmp	int10_write_char_tty_upd_cursor

    int10_write_char_tty_mode13:
	call	put_mode13_char 
	jmp	int10_write_char_tty_upd_cursor

    int10_write_char_tty_text:
	mov	[es:bp+di], al
    int10_write_char_tty_upd_cursor:
	inc	byte [si+curpos_x-bios_data]
	mov	ah, [vid_cols-bios_data]
	cmp	byte [si+curpos_x-bios_data], ah
	jge	int10_write_char_tty_nl

    int10_write_char_tty_set_crtc_cursor:
	cmp	bh, [disp_page-bios_data]
	jne	int10_write_char_tty_done

	mov	ax, [si+curpos_x-bios_data]
	mov	[crt_curpos_x-bios_data], ax
	call	set_crtc_cursor_pos

    int10_write_char_tty_done:
	pop	bp
	pop	di
	pop	si
	pop	cx
	pop	bx
	pop	ax
	pop	es
	pop	ds

	iret

int10_write_char_attrib:
  ; AL = character to display
  ; BH = page number
  ; BL = attribute (text mode) or foreground colour (gfx mode)
  ; CX = number of times to write character
 
	push	si
	push	di
	push	ds
	push	es
	push	ax
	push	bx
	push    cx
	push	bp

	push	ax
	mov	ax, 0x40
	mov	ds, ax
	pop	ax

	cmp	byte [vid_mode-bios_data], 0x00
	je	int10_write_char_attrib_t40
	cmp	byte [vid_mode-bios_data], 0x01
	je	int10_write_char_attrib_t40
	cmp	byte [vid_mode-bios_data], 0x02
	je	int10_write_char_attrib_t80
	cmp	byte [vid_mode-bios_data], 0x03
	je	int10_write_char_attrib_t80

	; Not a text mode, so only page 1 is valid.
	cmp	bh, 0
	jne	int10_write_char_attrib_done
	jmp	int10_write_char_attrib_page_ok

    int10_write_char_attrib_t40:
	cmp	bh, 0x07
	jg	int10_write_char_attrib_done
	jmp	int10_write_char_attrib_page_ok

    int10_write_char_attrib_t80:
	cmp	bh, 0x07
	jg	int10_write_char_attrib_done

    int10_write_char_attrib_page_ok:

	; Get bios data cursor position offset for this page into SI
	push	bx
	mov	bl, bh
	mov	bh, 0
	shl	bx, 1
	mov	si, bx
	pop	bx

	push	ax

	; Get video page offset into BP
	mov	al, [vid_page_size+1-bios_data]
	mul	byte bh
	mov	ah, al
	mov	al, 0
	mov	bp, ax

	; Get offset for the cursor position within the page into DI
	mov	al, [si+curpos_y-bios_data]
	mul	byte [vid_cols-bios_data]
	add	al, [si+curpos_x-bios_data]
	adc	ah, 0
	shl	ax, 1
	mov	di, ax

	; Get video RAM segment into ES
	mov	ax, 0xb800
	mov	es, ax

	pop	ax

	mov	ah, bl	; ax = char + attr to write

	mov	bx, 0xb800
	mov	es, bx

    int10_write_char_attrib_repeat:
	cmp     cx, 0
	je      int10_write_char_attrib_done        

	cmp	byte [vid_mode-bios_data], 4
	je	int10_write_char_attrib_cga320
	cmp	byte [vid_mode-bios_data], 5
	je	int10_write_char_attrib_cga320
	cmp	byte [vid_mode-bios_data], 6
	je	int10_write_char_attrib_cga640
	cmp	byte [vid_mode-bios_data], 0x11
	je	int10_write_char_attrib_mode11
	cmp	byte [vid_mode-bios_data], 0x13
	je	int10_write_char_attrib_mode13

	jmp	int10_write_char_attrib_text

    int10_write_char_attrib_cga320:
	call	put_cga320_char
	jmp	int10_write_char_attrib_next

    int10_write_char_attrib_cga640:
	call	put_cga640_char 
	jmp	int10_write_char_attrib_next

    int10_write_char_attrib_mode11:
	call	put_mode11_char 
	jmp	int10_write_char_attrib_next

    int10_write_char_attrib_mode13:
	call	put_mode13_char 
	jmp	int10_write_char_attrib_next

    int10_write_char_attrib_text:
	mov	[es:bp+di], ax
 
    int10_write_char_attrib_next:
	dec	cx
	add	di, 2
	jmp	int10_write_char_attrib_repeat    

    int10_write_char_attrib_done:
	pop	bp
	pop	cx
	pop	bx
	pop	ax
	pop	es
	pop	ds
	pop	di
	pop	si

	iret

int10_write_char:
  ; AL = character to display
  ; BH = page number
  ; BL = foreground colour (gfx mode only)
  ; CX = number of times to write character
	push	si
	push	di
	push	ds
	push	es
	push	ax
	push	bx
	push    cx
	push	bp

	push	ax
	mov	ax, 0x40
	mov	ds, ax
	pop	ax

	cmp	byte [vid_mode-bios_data], 0x00
	je	int10_write_char_t40
	cmp	byte [vid_mode-bios_data], 0x01
	je	int10_write_char_t40
	cmp	byte [vid_mode-bios_data], 0x02
	je	int10_write_char_t80
	cmp	byte [vid_mode-bios_data], 0x03
	je	int10_write_char_t80

	; Not a text mode, so only page 1 is valid.
	cmp	bh, 0
	jne	int10_write_char_done
	jmp	int10_write_char_page_ok

    int10_write_char_t40:
	cmp	bh, 0x07
	jg	int10_write_char_done
	jmp	int10_write_char_page_ok

    int10_write_char_t80:
	cmp	bh, 0x07
	jg	int10_write_char_done

    int10_write_char_page_ok:

	; Get bios data cursor position offset for this page into SI
	push	bx
	mov	bl, bh
	mov	bh, 0
	shl	bx, 1
	mov	si, bx
	pop	bx

	push	ax

	; Get video page offset into BP
	mov	al, [vid_page_size+1-bios_data]
	mul	byte bh
	mov	ah, al
	mov	al, 0
	mov	bp, ax

	; Get offset for the cursor position within the page into DI
	mov	al, [si+curpos_y-bios_data]
	mul	byte [vid_cols-bios_data]
	add	al, [si+curpos_x-bios_data]
	adc	ah, 0
	shl	ax, 1
	mov	di, ax

	; Get video RAM segment into ES
	mov	ax, 0xb800
	mov	es, ax

	pop	ax

	mov	bx, 0xb800
	mov	es, bx

    int10_write_char_repeat:
	cmp     cx, 0
	je      int10_write_char_done        

	cmp	byte [vid_mode-bios_data], 4
	je	int10_write_char_cga320
	cmp	byte [vid_mode-bios_data], 5
	je	int10_write_char_cga320
	cmp	byte [vid_mode-bios_data], 6
	je	int10_write_char_cga640
	cmp	byte [vid_mode-bios_data], 0x11
	je	int10_write_char_mode11
	cmp	byte [vid_mode-bios_data], 0x13
	je	int10_write_char_mode13

	jmp	int10_write_char_text

    int10_write_char_cga320:
	call	put_cga320_char
	jmp	int10_write_char_next

    int10_write_char_cga640:
	call put_cga640_char 
	jmp int10_write_char_next

    int10_write_char_mode11:
	call put_mode11_char 
	jmp int10_write_char_next

    int10_write_char_mode13:
	call put_mode13_char 
	jmp int10_write_char_next

    int10_write_char_text:
	mov	[es:bp+di], al
 
    int10_write_char_next:
	dec	cx
	add	di, 2
	jmp	int10_write_char_repeat    

    int10_write_char_done:
	pop	bp
	pop	cx
	pop	bx
	pop	ax
	pop	es
	pop	ds
	pop	di
	pop	si

	iret


int10_set_colour:
  ; BH = 0 => set background colour
  ;           BL = background/border colour (5 bits)
  ; BH = 1 => set palette
  ;           BL = palette id	
  ;              0 = red/green/brown
  ;              1 = cyan/magenta/white

	push	dx
	push	bx
	push	ax

	cmp	bh, 1
	je	int10_set_palette

	; Set BG colour

	and	bl, 0x1f
	mov	dx, 0x3d9
	in	al, dx
	and	al, 0xf0
	or	al, bl
	out	dx, al

	jmp	int10_set_colour_done

    int10_set_palette:

	cmp	bl, 0x00
	jne	int10_set_palette1

	jmp 	int10_set_palette_update

    int10_set_palette1:
	mov	bl, 0x20

    int10_set_palette_update:	
	mov	dx, 0x3d9
	in	al, dx
	and	al, 0xdF
	or	al, bl
	out	dx, al

    int10_set_colour_done:
	pop	ax
	pop	bx
	pop	dx

	iret


int10_get_vm:
  ; int 10, 0f
  ; On exit:
  ;  AH = number of screen columns
  ;  AL = mode currently active
  ;  BH = current display page
	push	ds
	mov	ax, 0x40
	mov	ds, ax

	mov	ah, [vid_cols-bios_data]
	mov	al, [vid_mode-bios_data]
	mov	bh, [disp_page-bios_data]

	pop	ds
	iret

int10_palette:
  ; int 10, 10
  ; Supported functions:
  ;   AL = 10 : set DAC color register
  ;   AL = 12 : set block of DAC color registers
  ;   AL = 15 : get individual colour register
  ;   Al = 17 : get block of DAC colour registers

	cmp	al, 0x10
	je	int10_palette_set1
	cmp	al, 0x12
	je	int10_palette_setblock
	cmp	al, 0x15
	je	int10_palette_get1
	cmp	al, 0x17
	je	int10_palette_getblock
	jmp	int10_palette_done

    int10_palette_set1:
	; BX = register to set
	; CH = green value
	; CL = blue value
	; DH = red value

	push	ax
	push	bx
	push	dx

	push	dx
	mov	ax, 0x03c8
	mov	dx, ax
	mov	ax, bx
	out	dx, al
	pop	bx

	mov	ax, 0x03c9
	mov	dx, ax
	mov	al, bh
	out	dx, al
	mov	al, ch
	out	dx, al
	mov	al, cl
	out	dx, al

	pop	dx
	pop	bx
	pop	ax
	jmp	int10_palette_done 

    int10_palette_setblock:
	; BX = first colour register to set
	; CX = number of colour registers to set
	; ES:DX = pointer to table of colour values to set
	push	ax
	push	cx
	push	dx
	push	di

	mov	di, dx

	mov	ax, 0x03c8
	mov	dx, ax
	mov	ax, bx
	out	dx, al

	mov	ax, 0x03c9
	mov	dx, ax

    int10_palette_setblock_loop:
	cmp	cx, 0
	je	int10_palette_setblock_done

	mov	al, [es:di]
	out	dx, al	
	mov	al, [es:di+1]
	out	dx, al	
	mov	al, [es:di+2]
	out	dx, al	

	add	di, 3
	dec	cx
	jmp	int10_palette_setblock_loop

    int10_palette_setblock_done:	

	pop	di
	pop	dx
	pop	cx
	pop	ax

	jmp	int10_palette_done 

   int10_palette_get1:
	; On entry:
	;   BL = register to get
	; On exit:
	;   CH = green value
	;   CL = blue value
	;   DH = red value

	push	ax
	push	dx

	mov	ax, 0x03c7
	mov	dx, ax
	mov	al, bl
	out	dx, al

	mov	ax, 0x03c9
	mov	dx, ax
	in	al, dx
	mov	ah, dl
	in	al, dx
	mov	ch, al
	in	al, dx
	mov	cl, al

	pop	dx
	pop	ax
	jmp	int10_palette_done 

    int10_palette_getblock:
	; BX = first colour register to set
	; CX = number of colour registers to set
	; ES:DX = pointer to table of colour values to get
	push	ax
	push	cx
	push	dx
	push	di

	mov	di, dx

	mov	ax, 0x03c7
	mov	dx, ax
	mov	ax, bx
	out	dx, al

	mov	ax, 0x03c9
	mov	dx, ax

    int10_palette_getblock_loop:
	cmp	cx, 0
	je	int10_palette_getblock_done

	in	al, dx	
	mov	[es:di], al
	in	al, dx	
	mov	[es:di+1], al
	in	al, dx	
	mov	[es:di+2],al

	add	di, 3
	dec	cx
	jmp	int10_palette_getblock_loop

    int10_palette_getblock_done:	

	pop	di
	pop	dx
	pop	cx
	pop	ax

    int10_palette_done:
	iret

int10_set_int43_8x8dd:

	push	ax
	mov	al, '8'
	extended_putchar_al
	mov	al, 'x'
	extended_putchar_al
	mov	al, '8'
	extended_putchar_al
	mov	al, 0x0a
	extended_putchar_al
	mov	al, 0x0d
	extended_putchar_al
	pop	ax

	; Set int 43 to the 8x8 double dot char table
	push	es
	push	ax
	mov		ax, 0
	mov		es, ax
	mov		ax, cga_glyphs
	mov		word [es:4*0x43], ax
	mov		ax, 0xf000
	mov		word [es:4*0x43 + 2], ax
	pop		ax
	pop		es
	iret

int10_get_font_info:
  ; int 10, 1130
  ; BH = information desired:
  ;      = 0  INT 1F pointer
  ;      = 1  INT 44h pointer
  ;      = 2  ROM 8x14 pointer  
  ;      = 3  ROM 8x8 double dot pointer (base)
  ;      = 4  ROM 8x8 double dot pointer (top)
  ;      = 5  ROM 9x14 alpha alternate pointer
  ;      = 6  ROM 8x16 character table pointer
  ;      = 7  ROM 9x16 alternate character table pointer
  ; NOTE: Currently only BH = 0 is supported

	push	ax
	push	ds
	
	cmp	bh, 0x00
	je	int10_get_font_info_int1f_vector

	jmp	int10_get_font_info_done

    int10_get_font_info_int1f_vector:
	mov	ax, 0x0000
	mov	ds, ax
	mov	ax, [ds:0x007e]
	mov	es, ax
	mov	bp, [ds:0x007c]

	mov	cx, 0x0008
	mov	dl, 24

    int10_get_font_info_done:
	pop	ds
	pop	ax
	iret

int10_video_subsystem_cfg:
  ; int 10, 12
  ; Not supported on CGA or MCGA
  ; On entry
  ;   BL = 10 => return video configuration information
  ; on return:
  ;   BH = 0 if color mode in effect
  ;      = 1 if mono mode in effect
  ;   BL = 0 if 64k EGA memory
  ;      = 1 if 128k EGA memory
  ;      = 2 if 192k EGA memory
  ;      = 3 if 256k EGA memory
  ;   CH = feature bits
  ;   CL = switch settings
  ;
  ;   BL = other = unsupported
	iret

int10_vid_combination:
  ; int 10, 1a
  ; On entry:
  ;   AL = 0 => Get vidio display
  ;   AL = 1 => set video display (unsupported)
  ; On exit:
  ;   AL = 1A, if a valid function was requested
  ;   BL = active video display
  ;   BH = inactive video display
  ;
  ; Valid display codes:
  ;   FF  Unrecognized video system
  ;   00  No display
  ;   01  MDA with monochrome display
  ;   02  CGA with color display
  ;   03  Reserved
  ;   04  EGA with color display
  ;   05  EGA with monochrome display
  ;   06  Professional graphics controller
  ;   07  VGA with analog monochrome display
  ;   08  VGA with analog color display
  ;   09  Reserved
  ;   0A  MCGA with digital color display
  ;   0B  MCGA with analog monochrome display
  ;   0C  MCGA with analog color display	
	cmp	al, 0
	jne	int10_vid_combination_done
	
	mov	bl, 0x0C	; MCGA with colour display
	mov	bh, 0	; no inactive display
	mov	al, 0x1a
	
  int10_vid_combination_done:
	iret

int10_get_state_info:
  ; int 10, 1b
  ; On entry:
  ;   BX = implementation type (must be zero)
  ;   ES:DI = pointer to 64 byte buffer
  ; On exit:
  ;   AL = 1B
  ;   ES:DI = pointer to updated buffer
	push	ax
	push	ds

	mov	ax, 0x40
	mov	ds, ax

	mov	word [es:di], vid_static_table
	mov	word [es:di+2], 0xf000

	; Copy video state from bios data area (30 bytes)
	mov	ax, [vid_mode-bios_data]
	mov	[es:di+0x04], ax
	mov	ax, [vid_mode-bios_data+2]
	mov	[es:di+0x06], ax
	mov	ax, [vid_mode-bios_data+4]
	mov	[es:di+0x08], ax 
	mov	ax, [vid_mode-bios_data+6]
	mov	[es:di+0x0a], ax
	mov	ax, [vid_mode-bios_data+8]
	mov	[es:di+0x0c], ax
	mov	ax, [vid_mode-bios_data+10]
	mov	[es:di+0x0e], ax
	mov	ax, [vid_mode-bios_data+12]
	mov	[es:di+0x10], ax
	mov	ax, [vid_mode-bios_data+14]
	mov	[es:di+0x12],ax
	mov	ax, [vid_mode-bios_data+16]
	mov	[es:di+0x14], ax
	mov	ax, [vid_mode-bios_data+18]
	mov	[es:di+0x16], ax
	mov	ax, [vid_mode-bios_data+20]
	mov	[es:di+0x18], ax
	mov	ax, [vid_mode-bios_data+22]
	mov	[es:di+0x1a], ax
	mov	ax, [vid_mode-bios_data+24]
	mov	[es:di+0x1c], ax
	mov	ax, [vid_mode-bios_data+26]
	mov	[es:di+0x1d], ax
	mov	ax, [vid_mode-bios_data+28]
	mov	[es:di+0x20], ax

	mov	al, [vid_rows-bios_data]
	mov	byte [es:di+0x22], al
	mov	word [es:di+0x23], 0x0008
	mov	byte [es:di+0x25], 0x0c		; active display combination code (0x0c=MCGA)
	mov	byte [es:di+0x26], 0x00		; inactive display combination code
	mov	word [es:di+0x27], 0x0010	; Number of dislayed colours

	cmp	byte [vid_mode-bios_data], 0x01
	jle	int10_get_state_info_pages_8
	cmp	byte [vid_mode-bios_data], 0x03
	jle	int10_get_state_info_pages_4
	mov	byte [es:di+0x29], 0x0001	; number of supported video pages in gfx modes
	jmp	int10_get_state_info_pages_done
int10_get_state_info_pages_8:
	mov	byte [es:di+0x29], 0x0008	; number of supported video pages in modes 0, 1
	jmp	int10_get_state_info_pages_done
int10_get_state_info_pages_4:
	mov	byte [es:di+0x29], 0x0004	; number of supported video pages in modes 2, 3
int10_get_state_info_pages_done:
	mov	byte [es:di+0x2a], 0x00		; Number of raster scan lines, 0 = 200, 1 = 350, 2 = 400, 3 = 480
	mov	byte [es:di+0x2b], 0x00		; text character table used
	mov	byte [es:di+0x2c], 0x00		; text character table used
	mov	byte [es:di+0x2d], 0x00		; other state information
	
	pop	ds
	pop	ax
	mov	al, 0x1b
	iret

; ************************* INT 11h - get equipment list

int11:	
	mov	ax, [cs:equip]
	iret

; ************************* INT 12h - return memory size

int12:	
	mov	ax, 0x280 ; 640K conventional memory
	iret

; ************************* INT 13h handler - disk services

int13:
	; HACK ALERT
	; ==========
	; Make sure IF is set in the return flags,
	; Normally disk access does lots of stuff that involves interrupts
	; and IF will be set at the end.
	; FreeDOS doesn't seem to need this but MS DOS 3.3 to 5.0 do as they 
	; do not re-enable interrupts themselves, but rely on the flags returned
	; by this bios call instead.
	sti
	push	bp
	mov	bp, sp
	or	word [bp+6], 0x0200
	pop	bp

	; Now check which operation was requested.
	cmp	ah, 0x00 ; Reset disk
	je	int13_reset_disk
	cmp	ah, 0x01 ; Get last status
	je	int13_last_status

	cmp	dl, 0x80 ; Hard disk being queried?
	jne	i13_diskok

	; Now, need to check an HD is installed
	cmp	word [cs:num_disks], 2
	jge	i13_diskok

	; No HD, so return an error
	mov	ah, 15 ; Report no such drive
	jmp	reach_stack_stc

  i13_diskok:

	cmp	ah, 0x02 ; Read disk
	je	int13_read_disk
	cmp	ah, 0x03 ; Write disk
	je	int13_write_disk
	cmp	ah, 0x04 ; Verify disk
	je	int13_verify
	cmp	ah, 0x05 ; Format track - does nothing here
	je	int13_format
	cmp	ah, 0x08 ; Get drive parameters (hard disk)
	je	int13_getparams
	cmp	ah, 0x0c ; Seek (hard disk)
	je	int13_seek
	cmp	ah, 0x10 ; Check if drive ready (hard disk)
	je	int13_hdready
	cmp	ah, 0x15 ; Get disk type
	je	int13_getdisktype
	cmp	ah, 0x16 ; Detect disk change
	je	int13_diskchange

	mov	ah, 1 ; Invalid function
	jmp	reach_stack_stc

	iret

  int13_reset_disk:

	jmp	reach_stack_clc

  int13_last_status:

	mov	ah, [cs:disk_laststatus]
	je	ls_no_error

	stc
	iret

    ls_no_error:

	clc
	iret

  int13_read_disk:
	; AH	=	02h
	; AL	=	Sectors To Read Count
	; CX	=	Cylinder + Sector
	; DH	=	Head
	; DL	=	Drive
	; ES:BX	=	Buffer Address Pointer

	push	dx

	cmp	dl, 0 ; Floppy 0
	je	i_flop_rd
	cmp	dl, 0x80 ; HD
	je	i_hd_rd

	pop	dx
	mov	ah, 1
	jmp	reach_stack_stc

    i_flop_rd:

	push	si
	push	bp

	cmp	cl, [cs:int1e_spt]
	ja	rd_error

	pop	bp
	pop	si

	mov	dl, 1		; Floppy disk file handle is stored at j[1] in emulator
	jmp	i_rd

    i_hd_rd:

	mov	dl, 0		; Hard disk file handle is stored at j[0] in emulator

    i_rd: 

	push	si
	push	bp

	; Convert head/cylinder/sector number to byte offset in disk image

	call	chs_to_abs

	; Now, SI:BP contains the absolute sector offset of the block. We then multiply by 512 to get the offset into the disk image

	mov	ah, 0
	cpu	186
	shl	ax, 9
	extended_read_disk
	shr	ax, 9
	cpu	8086
	mov	ah, 0x02	; Put read code back

	cmp	al, 0
	je	rd_error

	; Read was successful. Now, check if we have read the boot sector. If so, we want to update
	; our internal table of sectors/track to match the disk format

	cmp	dx, 1		; FDD?
	jne	rd_noerror
	cmp	cx, 1		; First sector?
	jne	rd_noerror

	push	ax

	mov	al, [es:bx+24]	; Number of SPT in floppy disk BPB

	; cmp	al, 0		; If disk is unformatted, do not update the table
	; jne	rd_update_spt
	cmp	al, 9		; 9 SPT, i.e. 720K disk, so update the table
	je	rd_update_spt
	cmp	al, 18
	je	rd_update_spt	; 18 SPT, i.e. 1.44MB disk, so update the table

	pop	ax

	jmp	rd_noerror

    rd_update_spt:

	mov	[cs:int1e_spt], al
	pop	ax

    rd_noerror:

	clc
	mov	ah, 0 ; No error
	jmp	rd_finish

    rd_error:

	stc
	mov	ah, 4 ; Sector not found

    rd_finish:

	pop	bp
	pop	si
	pop	dx

	mov	[cs:disk_laststatus], ah
	jmp	reach_stack_carry

  int13_write_disk:

	push	dx

	cmp	dl, 0 ; Floppy 0
	je	i_flop_wr
	cmp	dl, 0x80 ; HD
	je	i_hd_wr

	pop	dx
	mov	ah, 1
	jmp	reach_stack_stc

    i_flop_wr:

	mov	dl, 1		; Floppy disk file handle is stored at j[1] in emulator
	jmp	i_wr

    i_hd_wr:

	mov	dl, 0		; Hard disk file handle is stored at j[0] in emulator

    i_wr:

	push	si
	push	bp
	push	cx
	push	di

	; Convert head/cylinder/sector number to byte offset in disk image

	call	chs_to_abs

	; Signal an error if we are trying to write beyond the end of the disk

	cmp	dl, 0 ; Hard disk?
	jne	wr_fine ; No - no need for disk sector valid check - NOTE: original submission was JNAE which caused write problems on floppy disk

	; First, we add the number of sectors we are trying to write from the absolute
	; sector number returned by chs_to_abs. We need to have at least this many
	; sectors on the disk, otherwise return a sector not found error.

	mov	cx, bp
	mov	di, si

	mov	ah, 0
	add	cx, ax
	adc	di, 0

	cmp	di, [cs:hd_secs_hi]
	ja	wr_error
	jb	wr_fine
	cmp	cx, [cs:hd_secs_lo]
	ja	wr_error

wr_fine:

	mov	ah, 0
	cpu	186
	shl	ax, 9
	extended_write_disk
	shr	ax, 9
	cpu	8086
	mov	ah, 0x03	; Put write code back

	cmp	al, 0
	je	wr_error

	clc
	mov	ah, 0 ; No error
	jmp	wr_finish

    wr_error:

	stc
	mov	ah, 4 ; Sector not found

    wr_finish:

	pop	di
	pop	cx
	pop	bp
	pop	si
	pop	dx

	mov	[cs:disk_laststatus], ah
	jmp	reach_stack_carry

  int13_verify:

	mov	ah, 0
	jmp	reach_stack_clc

  int13_getparams:

	cmp 	dl, 0
	je	i_gp_fl
	cmp	dl, 0x80
	je	i_gp_hd

	mov	ah, 0x01
	mov	[cs:disk_laststatus], ah
	jmp	reach_stack_stc

    i_gp_fl:

	push	cs
	pop	es
	mov	di, int1e	; ES:DI now points to floppy parameters table (INT 1E)

	mov	ax, 0
	mov	bx, 4
	mov	ch, 0x4f
	mov	cl, [cs:int1e_spt]
	mov	dx, 0x0101

	mov	byte [cs:disk_laststatus], 0
	jmp	reach_stack_clc

    i_gp_hd:

	mov	ax, 0
	mov	bx, 0
	mov	dl, 1
	mov	dh, [cs:hd_max_head]
	mov	cx, [cs:hd_max_track]
	ror	ch, 1
	ror	ch, 1
	add	ch, [cs:hd_max_sector]
	xchg	ch, cl

	mov	byte [cs:disk_laststatus], 0
	jmp	reach_stack_clc

  int13_seek:

	mov	ah, 0
	jmp	reach_stack_clc

  int13_hdready:

	cmp	byte [cs:num_disks], 2	; HD present?
	jne	int13_hdready_nohd
	cmp	dl, 0x80		; Checking first HD?
	jne	int13_hdready_nohd

	mov	ah, 0
	jmp	reach_stack_clc

    int13_hdready_nohd:

	jmp	reach_stack_stc

  int13_format:

	mov	ah, 0
	jmp	reach_stack_clc

  int13_getdisktype:

	cmp	dl, 0 ; Floppy
	je	gdt_flop
	cmp	dl, 0x80 ; HD
	je	gdt_hd

	mov	ah, 15 ; Report no such drive
	mov	[cs:disk_laststatus], ah
	jmp	reach_stack_stc

    gdt_flop:

	mov	ah, 1
	jmp	reach_stack_clc

    gdt_hd:

	mov	ah, 3
	mov	cx, [cs:hd_secs_hi]
	mov	dx, [cs:hd_secs_lo]
	jmp	reach_stack_clc

  int13_diskchange:

	mov	ah, 0 ; Disk not changed
	jmp	reach_stack_clc

; ************************* INT 14h - serial port functions

; baud rate divisors
baud_rate_div:
	dw	0x0417	; 110
	dw	0x0300	; 150
	dw	0x0180	; 300
	dw	0x00c0	; 600
	dw	0x0060	; 1200
	dw	0x0030	; 2400
	dw	0x0018	; 4800
	dw	0x000c	; 9600

int14:
	cmp	ah, 0
	je	int14_init
	cmp	ah, 1
	je	int14_write_char
	cmp	ah, 2
	je	int14_read_char
	cmp	ah, 3
	je	int14_get_port_status

	iret

  int14_init:
	; On entry:
	;   AL = port parameters
  	;        7-5    data rate (110,150,300,600,1200,2400,4800,9600 bps)
	;        4-3    parity (00 or 10 = none, 01 = odd, 11 = even)
	;        2      stop bits (set = 2, clear = 1)
	;        1-0    data bits (00 = 5, 01 = 6, 10 = 7, 11 = 8)
	;   DX = Port number (0x00 - 0x03)
	; On return:
	;   AH = line status
	;        7 timeout 
	;        6 transmit shift register empty 
	;        5 transmit holding register empty 
	;        4 break detected 
	;        3 framing error 
	;        2 parity error 
	;        1 overrun error 
	;        0 receive data ready 
	;   AL = modem status
	;        7      carrier detect
	;        6      ring indicator
	;        5      data set ready
	;        4      clear to send
	;        3      delta carrier detect
	;        2      trailing edge of ring indicator
	;        1      delta data set ready
	;        0      delta clear to send

	push	bx
	push	cx
	push	dx
	push	ds

	; Get the bios data area into ds
	mov	bx, 0x0040
	mov	ds, bx

	; Get the serial port base address for this port
	and	dx, 0x0003
	shl	dx, 1
	mov	bx, dx
	mov	dx, [com1addr-bios_data+bx]

	; set baud rate divisor
	mov	bx, 0
	mov	bl, al
	and	bl, 0xe0
	shr	bl, 1
	shr	bl, 1
	shr	bl, 1
	shr	bl, 1
	shr	bl, 1
	shl	bl, 1
	mov	cx, [cs:baud_rate_div+bx]

	push	ax
	add	dx, 3		; dx = line control register
	mov	al, 0x80	; turn on bit 7 (divisor latch)
	out	dx, al
	sub	dx, 2		; MSB of divisor latch
	mov	al, ch
	out	dx, al
	dec	dx		; LSB of divisor latch
	mov	al, cl
	out	dx, al
	pop	ax

	; Initialise the Line Control Register
	mov	bl, al		; Set stop bits and data bits into bl
	and	bl, 0x07
	mov	cl, al		; Set parity bits and into bl
	and	cl, 0x18
	shl	cl, 1
	or	bl, cl
	add	dx, 3		; write Line Control register
	mov	bl, al
	out	dx, al

	; Assert RTS and DTR in the modme control register
	inc	dx
	mov	al, 0x03
	out	dx, al

	; Disable all interrupts
	sub	dx, 3		; Set Interrupt Control Register
	mov	al, 0
	out	dx, al

	; Get status
	add	dx, 2		; Get Line status Register
	in	al, dx
	mov	ah, al
	inc	dx		; get Modem Status Register
	in	al, dx

	pop	ds
	pop	dx
	pop	cx
	pop	bx
	iret

  int14_write_char:
	; On entry:
	;   AL = character to write
	;   DX = Port number
	; On return
	;   AH bit 7 clear on success, set on error
	;          as per 6..0 line status 
	push	bx
	push	cx
	push	dx
	push	ds

	; re-enable interrupts as we need the timer.
	sti

	; Get the bios data area into ds
	mov	bx, 0x0040
	mov	ds, bx

	; Get the serial port timeout and base address for this port
	;   cl = timeout ticks
	;   ch = character to send
	;   dx = com port address
	;   bh = initial timer tick LSB
	and	dx, 0x0003
	mov	bx, dx
	mov	cl, [com1_timeout-bios_data+bx]
	shl	dx, 1
	mov	bx, dx
	mov	dx, [com1addr-bios_data+bx]

	mov	ch, al

	add	dx, 5	; dx = line status register
	mov	bh, [clk_dtimer-bios_data]

	; Loop until either:
	;   transmit holding empty
	;   timeout
    int14_write_loop:
	in	al, dx
	test	al, 0x20	
	jnz	int14_write_send

	mov	bl, [clk_dtimer-bios_data]
	sub	bl, bh
	cmp	bl, cl
	jle	int14_write_loop

	mov	ah, al
	or	ah, 0x80
	jmp	int14_write_done

    int14_write_send:
	sub	dx, 5
	mov	al, ch
	out	dx, al	
	
	; get line status
	add	dx, 5
	in	al, dx
	mov	al, ah
	or	al, 0x7f
	mov	al, ch
	
    int14_write_done:
	pop	ds
	pop	dx
	pop	cx
	pop	bx
	iret

  int14_read_char:
	; On entry:
	;   DX = port number
	; On return:
	;   AH = line status
	;   AL = received character if AH bit 7 is clear
	push	bx
	push	cx
	push	dx
	push	ds

	; re-enable interrupts as we need the timer.
	sti

	; Get the bios data area into ds
	mov	bx, 0x0040
	mov	ds, bx

	; Get the serial port timeout and base address for this port
	;   cl = timeout ticks
	;   dx = com port address
	;   bh = initial timer tick LSB
	and	dx, 0x0003
	mov	bx, dx
	mov	cl, [com1_timeout-bios_data+bx]
	shl	dx, 1
	mov	bx, dx
	mov	dx, [com1addr-bios_data+bx]

	add	dx, 5	; dx = line status register
	mov	bh, [clk_dtimer-bios_data]

	; Loop until either:
	;   receive holding is not empty
	;   timeout
    int14_read_loop:
	in	al, dx
	test	al, 0x01	
	jnz	int14_read_get

	mov	bl, [clk_dtimer-bios_data]
	sub	bl, bh
	cmp	bl, cl
	jle	int14_read_loop

	mov	ah, al
	or	ah, 0x80
	jmp	int14_read_done

    int14_read_get:
	sub	dx, 5
	in	al, dx
	mov	ch, al	
	
	; get line status
	add	dx, 5
	in	al, dx
	mov	ah, al
	or	ah, 0x7f
	mov	al, ch
	
    int14_read_done:
	pop	ds
	pop	dx
	pop	cx
	pop	bx
	iret

int14_get_port_status:
	; On entry:
	;   DX = port number
	; On return:
	;   AH = line status
	;   AL = modem status
	push	bx
	push	cx
	push	dx
	push	ds

	; Get the bios data area into ds
	mov	bx, 0x0040
	mov	ds, bx

	; Get the serial base address for this port
	;   dx = com port address
	and	dx, 0x0003
	shl	dx, 1
	mov	bx, dx
	mov	dx, [com1addr-bios_data+bx]

	; read line status and modem status registers
	add	dx, 5	; dx = line status register
	in	al, dx
	mov	ah, al
	inc	dx	; dx = modem status register
	in	al, dx

	pop	ds
	pop	dx
	pop	cx
	pop	bx
	iret

; ************************* INT 15h - get system configuration

int15:	; Here we do not support any of the functions, and just return
	; a function not supported code - like the original IBM PC/XT does.

	; cmp	ah, 0xc0
	; je	int15_sysconfig
	; cmp	ah, 0x41
	; je	int15_waitevent
	; cmp	ah, 0x4f
	; je	int15_intercept
	; cmp	ah, 0x88
	; je	int15_getextmem

; Otherwise, function not supported

	mov	ah, 0x86

	jmp	reach_stack_stc

;  int15_sysconfig: ; Return address of system configuration table in ROM
;
;	mov	bx, 0xf000
;	mov	es, bx
;	mov	bx, rom_config
;	mov	ah, 0
;
;	jmp	reach_stack_clc
;
;  int15_waitevent: ; Events not supported
;
;	mov	ah, 0x86
;
;	jmp	reach_stack_stc
;
;  int15_intercept: ; Keyboard intercept
;
;	jmp	reach_stack_stc
;
;  int15_getextmem: ; Extended memory not supported
;
;	mov	ah,0x86
;
;	jmp	reach_stack_stc

; ************************* INT 16h handler - keyboard

int16:
	cmp	ah, 0x00 ; Get keystroke (remove from buffer)
	je	kb_getkey
	cmp	ah, 0x01 ; Check for keystroke (do not remove from buffer)
	je	kb_checkkey
	cmp	ah, 0x02 ; Check shift flags
	je	kb_shiftflags
	cmp	ah, 0x12 ; Check shift flags
	je	kb_extshiftflags

	iret

  kb_getkey:

	push	es
	push	bx
	push	cx
	push	dx

	sti

	mov	bx, 0x40
	mov	es, bx

    kb_gkblock:

	mov	cx, [es:kbbuf_tail-bios_data]
	mov	bx, [es:kbbuf_head-bios_data]
	mov	dx, [es:bx]

	; Wait until there is a key in the buffer
	cmp	cx, bx
	je	kb_gkblock

	add	word [es:kbbuf_head-bios_data], 2
	call	kb_adjust_buf

	mov	ah, dh
	mov	al, dl

	pop	dx
	pop	cx
	pop	bx
	pop	es	

	iret

  kb_checkkey:

	push	es
	push	bx
	push	cx
	push	dx

	sti

	mov	bx, 0x40
	mov	es, bx

	mov	cx, [es:kbbuf_tail-bios_data]
	mov	bx, [es:kbbuf_head-bios_data]
	mov	dx, [es:bx]

	; Check if there is a key in the buffer. ZF is set if there is none.
	cmp	cx, bx

	mov	ah, dh
	mov	al, dl

	pop	dx
	pop	cx
	pop	bx
	pop	es	

	retf	2	; NEED TO FIX THIS!!

    kb_shiftflags:

	push	es
	push	bx

	mov	bx, 0x40
	mov	es, bx

	mov	al, [es:keyflags1-bios_data]

	pop	bx
	pop	es

	iret

    kb_extshiftflags:

	push	es
	push	bx

	mov	bx, 0x40
	mov	es, bx

	mov	al, [es:keyflags1-bios_data]
	mov	ah, al

	pop	bx
	pop	es

	iret

; ************************* INT 17h handler - printer

int17:
	cmp	ah, 0x01
	je	int17_initprint ; Initialise printer

	iret

  int17_initprint:

	mov	ah, 0
	iret

; ************************* INT 19h = reboot

int19:
	jmp	boot

; ************************* INT 1Ah - clock

int1a:
	cmp	ah, 0
	je	int1a_getsystime ; Get ticks since midnight (used for RTC time)
	cmp	ah, 2
	je	int1a_gettime ; Get RTC time (not actually used by DOS)
	cmp	ah, 4
	je	int1a_getdate ; Get RTC date
	cmp	ah, 0x0f
	je	int1a_init    ; Initialise RTC

	iret

  int1a_getsystime:

	push	ax
	push	bx
	push	ds
	push	es

	push	cs
	push	cs
	pop	ds
	pop	es

	mov	bx, timetable

	extended_get_rtc

	mov	ax, 182  ; Clock ticks in 10 seconds
	mul	word [tm_msec]
	mov	bx, 10000
	div	bx ; AX now contains clock ticks in milliseconds counter
	mov	[tm_msec], ax

	mov	ax, 182  ; Clock ticks in 10 seconds
	mul	word [tm_sec]
	mov	bx, 10
	mov	dx, 0
	div	bx ; AX now contains clock ticks in seconds counter
	mov	[tm_sec], ax

	mov	ax, 1092 ; Clock ticks in a minute
	mul	word [tm_min] ; AX now contains clock ticks in minutes counter
	mov	[tm_min], ax
	
	mov	ax, 65520 ; Clock ticks in an hour
	mul	word [tm_hour] ; DX:AX now contains clock ticks in hours counter

	add	ax, [tm_msec] ; Add milliseconds in to AX
	adc	dx, 0 ; Carry into DX if necessary
	add	ax, [tm_sec] ; Add seconds in to AX
	adc	dx, 0 ; Carry into DX if necessary
	add	ax, [tm_min] ; Add minutes in to AX
	adc	dx, 0 ; Carry into DX if necessary

	push	dx
	push	ax
	pop	dx
	pop	cx

	pop	es
	pop	ds
	pop	bx
	pop	ax

	mov	al, 0
	iret

  int1a_gettime:

	; Return the system time in BCD format. DOS doesn't use this, but we need to return
	; something or the system thinks there is no RTC.

	push	ds
	push	es
	push	ax
	push	bx

	push	cs
	push	cs
	pop	ds
	pop	es

	mov	bx, timetable

	extended_get_rtc

	mov	ax, 0
	mov	cx, [tm_hour]
	call	hex_to_bcd
	mov	bh, al		; Hour in BCD is in BH

	mov	ax, 0
	mov	cx, [tm_min]
	call	hex_to_bcd
	mov	bl, al		; Minute in BCD is in BL

	mov	ax, 0
	mov	cx, [tm_sec]
	call	hex_to_bcd
	mov	dh, al		; Second in BCD is in DH

	mov	dl, 0		; Daylight saving flag = 0 always

	mov	cx, bx		; Hour:minute now in CH:CL

	pop	bx
	pop	ax
	pop	es
	pop	ds

	jmp	reach_stack_clc

  int1a_getdate:

	; Return the system date in BCD format.

	push	ds
	push	es
	push	bx
	push	ax

	push	cs
	push	cs
	pop	ds
	pop	es

	mov	bx, timetable

	extended_get_rtc

	mov	ax, 0x1900
	mov	cx, [tm_year]
	call	hex_to_bcd
	mov	cx, ax
	push	cx

	mov	ax, 1
	mov	cx, [tm_mon]
	call	hex_to_bcd
	mov	dh, al

	mov	ax, 0
	mov	cx, [tm_mday]
	call	hex_to_bcd
	mov	dl, al

	pop	cx
	pop	ax
	pop	bx
	pop	es
	pop	ds

	jmp	reach_stack_clc

  int1a_init:

	jmp	reach_stack_clc

; ************************* INT 1Ch - the other timer interrupt

int1c:

	iret

; ************************* INT 1Eh - diskette parameter table

int1e:

		db 0xdf ; Step rate 2ms, head unload time 240ms
		db 0x02 ; Head load time 4 ms, non-DMA mode 0
		db 0x25 ; Byte delay until motor turned off
		db 0x02 ; 512 bytes per sector
int1e_spt	db 18	; 18 sectors per track (1.44MB)
		db 0x1B ; Gap between sectors for 3.5" floppy
		db 0xFF ; Data length (ignored)
		db 0x54 ; Gap length when formatting
		db 0xF6 ; Format filler byte
		db 0x0F ; Head settle time (1 ms)
		db 0x08 ; Motor start time in 1/8 seconds

; ************************* INT 41h - hard disk parameter table

int41:

int41_max_cyls	dw 0
int41_max_heads	db 0
		dw 0
		dw 0
		db 0
		db 11000000b
		db 0
		db 0
		db 0
		dw 0
int41_max_sect	db 0
		db 0

; ************************* ROM configuration table

rom_config	dw 16		; 16 bytes following
		db 0xfe		; Model
		db 'A'		; Submodel
		db 'C'		; BIOS revision
		db 0b00100000   ; Feature 1
		db 0b00000000   ; Feature 2
		db 0b00000000   ; Feature 3
		db 0b00000000   ; Feature 4
		db 0b00000000   ; Feature 5
		db 0, 0, 0, 0, 0, 0

; Internal state variables

num_disks	dw 0	; Number of disks present
hd_secs_hi	dw 0	; Total sectors on HD (high word)
hd_secs_lo	dw 0	; Total sectors on HD (low word)
hd_max_sector	dw 0	; Max sector number on HD
hd_max_track	dw 0	; Max track number on HD
hd_max_head	dw 0	; Max head number on HD
drive_tracks_temp dw 0
drive_sectors_temp dw 0
drive_heads_temp  dw 0
drive_num_temp    dw 0
boot_state	db 0
cga_refresh_reg	db 0

; Default interrupt handlers

int0:
int1:
int2:
int3:
int4:
int5:
int6:
inta:
intb:
intc:
intd:
inte:
intf:
int18:
int1b:
int1d:

iret

; ************ Function call library ************

; Hex to BCD routine. Input is AX in hex (can be 0), and adds CX in hex to it, forming a BCD output in AX.

hex_to_bcd:

	push	bx

	jcxz	h2bfin

  h2bloop:

	inc	ax

	; First process the low nibble of AL
	mov	bh, al
	and	bh, 0x0f
	cmp	bh, 0x0a
	jne	c1
	add	ax, 0x0006

	; Then the high nibble of AL
  c1:
	mov	bh, al
	and	bh, 0xf0
	cmp	bh, 0xa0
	jne	c2
	add	ax, 0x0060

	; Then the low nibble of AH
  c2:	
	mov	bh, ah
	and	bh, 0x0f
	cmp	bh, 0x0a
	jne	c3
	add	ax, 0x0600

  c3:	
	loop	h2bloop
  h2bfin:
	pop	bx
	ret

puts_hex_al:
	push	ax

	mov	ah, al
	shr	al, 1
	shr	al, 1
	shr	al, 1
	shr	al, 1
	and	al, 0x0f
	cmp	al, 10
	jge	puts_hex_d1_letter
	add	al, '0'
	jmp	puts_hex_d1
  puts_hex_d1_letter:
	sub	al, 10
	add	al, 'A'
  puts_hex_d1:
	extended_putchar_al	; Print first digit

	mov	al, ah
	and	al, 0x0f
	cmp	al, 10
	jge	puts_hex_d2_letter
	add	al, '0'
	jmp	puts_hex_d2
  puts_hex_d2_letter:
	sub	al, 10
	add	al, 'A'
  puts_hex_d2:
	extended_putchar_al	; Print second digit

	pop	ax
	ret

; Takes a number in AL (from 0 to 99), and outputs the value in decimal using extended_putchar_al.

puts_decimal_al:

	push	ax

	aam
	add	ax, 0x3030	; '00'

	xchg	ah, al		; First digit is now in AL
	cmp	al, 0x30
	je	pda_2nd		; First digit is zero, so print only 2nd digit

	extended_putchar_al	; Print first digit

  pda_2nd:
	xchg	ah, al		; Second digit is now in AL

	extended_putchar_al	; Print second digit

	pop	ax
	ret

; Keyboard adjust buffer head and tail. If either head or the tail are at the end of the buffer, reset them
; back to the start, since it is a circular buffer.

kb_adjust_buf:

	push	ax
	push	bx

	; Check to see if the head is at the end of the buffer (or beyond). If so, bring it back
	; to the start

	mov	ax, [es:kbbuf_end_ptr-bios_data]
	cmp	[es:kbbuf_head-bios_data], ax
	jnge	kb_adjust_tail

	mov	bx, [es:kbbuf_start_ptr-bios_data]
	mov	[es:kbbuf_head-bios_data], bx	

  kb_adjust_tail:

	; Check to see if the tail is at the end of the buffer (or beyond). If so, bring it back
	; to the start

	mov	ax, [es:kbbuf_end_ptr-bios_data]
	cmp	[es:kbbuf_tail-bios_data], ax
	jnge	kb_adjust_done

	mov	bx, [es:kbbuf_start_ptr-bios_data]
	mov	[es:kbbuf_tail-bios_data], bx	

  kb_adjust_done:

	pop	bx
	pop	ax
	ret

; Convert CHS disk position (in CH, CL and DH) to absolute sector number in BP:SI
; Floppy disks have 512 bytes per sector, 9/18 sectors per track, 2 heads. DH is head number (1 or 0), CH bits 5..0 is
; sector number, CL7..6 + CH7..0 is 10-bit cylinder/track number. Hard disks have 512 bytes per sector, but a variable
; number of tracks and heads.

chs_to_abs:

	push	ax	
	push	bx
	push	cx
	push	dx

	mov	[cs:drive_num_temp], dl

	; First, we extract the cylinder number from CH and CL into BX.
	; CX =       ---CH--- ---CL---
	; cylinder : 76543210 98
	; sector   :            543210

	push	cx
	mov	bh, cl
	mov	cl, 6
	shr	bh, cl
	mov	bl, ch

	; Multiply cylinder number (now in BX) by the number of heads to get the track number of the first head

	cmp	byte [cs:drive_num_temp], 1 ; Floppy disk?

	push	dx

	mov	dx, 0
	xchg	ax, bx

	jne	chs_hd

	shl	ax, 1 ; Multiply by 2 (number of heads on FD)
	push	ax
	xor	ax, ax
	mov	al, [cs:int1e_spt]
	mov	[cs:drive_sectors_temp], ax ; Retrieve sectors per track from INT 1E table
	pop	ax

	jmp	chs_continue

chs_hd:

	mov	bp, [cs:hd_max_head]
	inc	bp
	mov	[cs:drive_heads_temp], bp

	mul	word [cs:drive_heads_temp] ; HD, so multiply by computed head count

	mov	bp, [cs:hd_max_sector] ; We previously calculated maximum HD track, so number of tracks is 1 more
	mov	[cs:drive_sectors_temp], bp

chs_continue:

	xchg	ax, bx

	pop	dx

	xchg	dh, dl
	mov	dh, 0
	add	bx, dx

	mov	ax, [cs:drive_sectors_temp]
	mul	bx

	; Now we extract the sector number (from 1 to 63) - for some reason they start from 1

	pop	cx
	mov	ch, 0
	and	cl, 0x3F
	dec	cl

	add	ax, cx
	adc	dx, 0
	mov	bp, ax
	mov	si, dx

	; Now, SI:BP contains offset into disk image file (FD or HD)

	pop	dx
	pop	cx
	pop	bx
	pop	ax
	ret

; CRTC cursor position helper
set_crtc_cursor_pos:
	push	es
	push	ax
	push	bx
	push	dx

	mov	ax, 0x40
	mov	es, ax

	mov	bh, [es:crt_curpos_y-bios_data]
	mov	bl, [es:crt_curpos_x-bios_data]

	; Set CRTC cursor address
	mov	al, [es:vid_cols-bios_data]
	mul	bh
	add	al, bl
	adc	ah, 0
	mov	bx, ax

	mov	al, 0x0e
	mov	dx, 0x3d4
	out	dx, al

	mov	al, bh
	mov	dx, 0x3d5
	out	dx, al

	mov	al, 0x0f
	mov	dx, 0x3d4
	out	dx, al

	mov	al, bl
	mov	dx, 0x3d5
	out	dx, al

	pop	dx
	pop	bx
	pop	ax
	pop	es

	ret

clear_window:
  ; BH = attribute used to write blank lines at bottom of window
  ; CH,CL = row,column of window's upper left corner
  ; DH,DL = row,column of window's lower right corner
  ; BP = video page offset

	push	ax
	push	bx
	push	cx
	push	dx

	push	ds
	push	es
	push	di

	mov	ax, 0x40
	mov	ds, ax

	cmp	byte [vid_mode-bios_data], 0x04
	je	clear_window_gfx320
	cmp	byte [vid_mode-bios_data], 0x05
	je	clear_window_gfx320
	cmp	byte [vid_mode-bios_data], 0x06
	je	clear_window_gfx640
	cmp	byte [vid_mode-bios_data], 0x11
	je	clear_window_mode11
	cmp	byte [vid_mode-bios_data], 0x13
	je	clear_window_mode13

	push	bx

	mov	bx, 0xb800
	mov	es, bx

	pop	bx

	; Get the byte offset of the top left corner of the window into DI	

	mov	ah, 0
	mov	al, [vid_cols-bios_data]	; ax = characters per row
	mul	ch	; ax = top row screen offset
	add	al, cl	
	adc	ah, 0	; ax = top left screen offset
	shl	ax, 1	; convert to byte address
	add	ax, bp	; add display page offset
  
	mov	di, ax

	; bl = number of rows in the window to clear
	mov	bl, dh
	sub	bl, ch
	inc	bl

	; cx = number of words to write
	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	mov	cx, ax

	; ax = clear screen value
	mov	al, 0
	mov	ah, bh

	cld

    clear_window_1_row:
	cmp	bl, 0
	je	clear_window_done

	push	cx
	push	di

	rep	stosw

	pop	di
	add	di, [vid_cols-bios_data]
	add	di, [vid_cols-bios_data]

	pop	cx

	dec	bl
	jmp	clear_window_1_row

    clear_window_gfx320:
	; Get the address offset in video ram for the top left character into DI
	mov	al, 80 ; bytes per row
	mul	byte ch
	shl	ax, 1
	shl	ax, 1
	add	al, cl
	adc	ah, 0
	add	al, cl
	adc	ah, 0
	mov	di, ax

	; bl = number of rows in the window to clear
	mov	bl, dh
	sub	bl, ch
	shl	bl, 1
	shl	bl, 1

	; cx = number of words to clear
	push	ax
	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	mov	cx, ax
	pop	ax

	cld

    clear_window_gfx320_loop:
	push	cx
	push	di
	mov	ax, 0xb800
	mov	es, ax
	mov	ax, 0
	rep	stosw
	pop	di
	pop	cx

	push	cx
	push	di
	mov	ax, 0xba00
	mov	es, ax
	mov	ax, 0
	rep	stosw
	pop	di
	pop	cx

	add	di, 80

	dec	bl
	jnz	clear_window_gfx320_loop

	jmp	clear_window_done

    clear_window_gfx640:
	; Get the address offset in video ram for the top left character into DI
	mov	al, 80 ; bytes per row
	mul	byte ch
	shl	ax, 1
	shl	ax, 1
	add	al, cl
	adc	ah, 0
	mov	di, ax

	; bl = number of rows in the window to clear
	mov	bl, dh
	sub	bl, ch
	shl	bl, 1
	shl	bl, 1

	; cx = number of bytes to clear
	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	mov	cx, ax

	cld

    clear_window_gfx640_loop:
	push	cx
	push	di
	mov	ax, 0xb800
	mov	es, ax
	mov	ax, 0
	rep	stosb
	pop	di
	pop	cx

	push	cx
	push	di
	mov	ax, 0xba00
	mov	es, ax
	mov	ax, 0
	rep	stosb
	pop	di
	pop	cx

	add	di, 80

	dec	bl
	jnz	clear_window_gfx640_loop

	jmp	clear_window_done

    clear_window_mode11:
	; Get the address offset in video ram for the top left character into DI
	mov	al, 80 ; bytes per row
	mul	byte ch
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	add	al, cl
	adc	ah, 0
	mov	di, ax
  
	; bx = number of rows in the window to clear
	mov	bl, dh
	sub	bl, ch
	mov	bh, 0
	shl	bx, 1
	shl	bx, 1
	shl	bx, 1
	shl	bx, 1

	; cx = number of bytes to clear
	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	mov	cx, ax

	mov	ax, 0xa000
	mov	ds, ax
	mov	es, ax

	cld

   clear_window_mode11_loop:
	push	cx
	push	di
	mov	ax, 0
	rep	stosb
	pop	di
	pop	cx

	add	di, 80

	dec	bx
	jnz	clear_window_mode11_loop

	jmp	clear_window_done

    clear_window_mode13:
	; Get the address offset in video ram for the top left character into DI
	mov	al, 40 ; characters per row
	mul	byte ch
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	add	al, cl
	adc	ah, 0
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	mov	di, ax
  
	add	ax, 2560
	mov	si, ax

	; bx = number of rows in the window to clear
	mov	bl, dh
	sub	bl, ch
	mov	bh, 0
	shl	bx, 1
	shl	bx, 1
	shl	bx, 1

	; cx = number of bytes to clear
	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	mov	cx, ax

	mov	ax, 0xa000
	mov	ds, ax
	mov	es, ax

	cld

    clear_window_mode13_loop:
	push	cx
	push	di
	mov	ax, 0
	rep	stosb
	pop	di
	pop	cx

	add	di, 320

	dec	bl
	jnz	clear_window_mode13_loop

	jmp	clear_window_done

    clear_window_done:
	pop	di
	pop	es
	pop	ds

	pop	dx
	pop	cx
	pop	bx
	pop	ax
	ret

scroll_up_window:
  ; AL = number of lines by which to scroll up
  ; BH = attribute used to write blank lines at bottom of window
  ; CH,CL = row,column of window's upper left corner
  ; DH,DL = row,column of window's lower right corner 
  ; BP = video page offset

	push	ax
	push	bx
	push	cx
	push	dx

	push	ds
	push	es
	push	di
	push	si

	push	ax
	mov	ax, 0x40
	mov	ds, ax
	pop	ax

	cmp	byte [vid_mode-bios_data], 0x04
	je	scroll_up_gfx320
	cmp	byte [vid_mode-bios_data], 0x05
	je	scroll_up_gfx320
	cmp	byte [vid_mode-bios_data], 0x06
	je	scroll_up_gfx640
	cmp	byte [vid_mode-bios_data], 0x11
	je	scroll_up_mode11
	cmp	byte [vid_mode-bios_data], 0x13
	je	scroll_up_mode13

	; Get screen RAM address into es and ds
	push	ax

	mov	ax, 0xb800
	mov	es, ax
	mov	ds, ax

	pop	ax

	; Get the first and second window row offsets into DI and SI
  	push	ax
	push	ds

	mov	ax, 0x40
	mov	ds, ax

	mov	ah, 0
	mov	al, [vid_cols-bios_data]	; ax = characters per row
	mul	ch	; ax = window top row screen word offset
	add	al, cl	
	adc	ah, 0	; ax = window top left screen word offset
	shl	ax, 1	; convert to byte address
   	add	ax, bp  ; Add video page offset

	mov	di, ax	
	add	al, [vid_cols-bios_data]
	adc	ah, 0
	add	al, [vid_cols-bios_data]
	adc	ah, 0
	mov	si, ax

	pop	ds
	pop	ax

	; bl = number of rows in the window to scroll
	mov bl, dh
	sub bl, ch

	; cx = number of words to move
	push	ax
	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	mov	cx, ax
	pop	ax

	cld

    scroll_up_1_row:
	cmp	al, 0
	je	scroll_up_done

	push	di
	push	si
	push	bx

    scroll_up_loop:
	cmp 	bl, 0
	je 	scroll_up_1_row_done

	push 	cx
	push 	di
	push	si

	rep	movsw

	pop	si
	pop	di
	pop	cx

	push	ax
	push	ds

	mov	ax, 0x40
	mov	ds, ax

	mov	ax, si
	add	al, [vid_cols-bios_data]
	adc	ah, 0
	add	al, [vid_cols-bios_data]
	adc	ah, 0
	mov	si, ax

	mov	ax, di
	add	al, [vid_cols-bios_data]
	adc	ah, 0
	add	al, [vid_cols-bios_data]
	adc	ah, 0
	mov	di, ax

	pop	ds
	pop	ax

	dec	bl
	jmp	scroll_up_loop

    scroll_up_1_row_done:

	; fill the last row with the bh attribute, null character
	push	ax
	push	cx
	mov	al, 0
	mov	ah, bh
	rep	stosw
	pop	cx
	pop	ax

	pop	bx
	pop	si
	pop	di

	dec	al
	jmp	scroll_up_1_row


    scroll_up_gfx320:

	; Get the first and second window row offsets into DI and SI
  	push	ax

	; Get the address offset in video ram for the top left character into DI
	mov	al, 80 ; bytes per row
	mul	byte ch
	shl	ax, 1
	shl	ax, 1
	add	al, cl
	adc	ah, 0
	add	al, cl
	adc	ah, 0
	mov	di, ax
  
	add	ax, 320
	mov	si, ax

	pop	ax

	; bl = number of rows in the window to scroll
	mov bl, dh
	sub bl, ch
	shl	bl, 1
	shl	bl, 1

	; cx = number of words to move
	push	ax
	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	mov	cx, ax
	pop	ax

	cld

    scroll_up_gfx320_1_row:
	cmp	al, 0
	je	scroll_up_done

	push	di
	push	si
	push	bx

    scroll_up_gfx320_loop:
	cmp 	bl, 0
	je 	scroll_up_gfx320_1_row_done

	push	ax

	push 	cx
	push 	di
	push	si

	mov	ax, 0xb800
	mov	ds, ax
	mov	es, ax
	rep	movsw

	pop	si
	pop	di
	pop	cx

	push 	cx
	push 	di
	push	si

	mov	ax, 0xba00
	mov	ds, ax
	mov	es, ax
	rep	movsw

	pop	si
	pop	di
	pop	cx

	add	si, 80
	add	di, 80

	pop	ax

	dec	bl
	jmp	scroll_up_gfx320_loop

    scroll_up_gfx320_1_row_done:

	; fill the last row with zeros
	push	ax
	push	bx
	mov	bl, 4

    scroll_up_gfx320_last_row_loop:
	push	cx
	push	di
	mov	ax, 0xb800
	mov	es, ax
	mov	ax, 0
	rep	stosw
	pop	di
	pop	cx

	push	cx
	push	di
	mov	ax, 0xba00
	mov	es, ax
	mov	ax, 0
	rep	stosw
	pop	di
	pop	cx

	add	di, 80

	dec	bl
	jnz	scroll_up_gfx320_last_row_loop

	pop	bx
	pop	ax

	pop	bx
	pop	si
	pop	di

	dec	al
	jmp	scroll_up_gfx320_1_row


    scroll_up_gfx640:

	; Get the first and second window row offsets into DI and SI
  	push	ax

	; Get the address offset in video ram for the top left character into DI
	mov	al, 80 ; bytes per row
	mul	byte ch
	shl	ax, 1
	shl	ax, 1
	add	al, cl
	adc	ah, 0
	mov	di, ax
  
	add	ax, 320
	mov	si, ax

	pop	ax

	; bl = number of rows in the window to scroll
	mov bl, dh
	sub bl, ch
	shl	bl, 1
	shl	bl, 1

	; cx = number of bytes to move
	push	ax
	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	mov	cx, ax
	pop	ax

	cld

    scroll_up_gfx640_1_row:
	cmp	al, 0
	je	scroll_up_done

	push	di
	push	si
	push	bx

    scroll_up_gfx640_loop:
	cmp 	bl, 0
	je 	scroll_up_gfx640_1_row_done

	push	ax

	push 	cx
	push 	di
	push	si

	mov	ax, 0xb800
	mov	ds, ax
	mov	es, ax
	rep	movsb

	pop	si
	pop	di
	pop	cx

	push 	cx
	push 	di
	push	si

	mov	ax, 0xba00
	mov	ds, ax
	mov	es, ax
	rep	movsb

	pop	si
	pop	di
	pop	cx

	add	si, 80
	add	di, 80

	pop	ax

	dec	bl
	jmp	scroll_up_gfx640_loop

    scroll_up_gfx640_1_row_done:

	; fill the last row with zeros
	push	ax
	push	bx
	mov	bl, 4

    scroll_up_gfx640_last_row_loop:
	push	cx
	push	di
	mov	ax, 0xb800
	mov	es, ax
	mov	ax, 0
	rep	stosb
	pop	di
	pop	cx

	push	cx
	push	di
	mov	ax, 0xba00
	mov	es, ax
	mov	ax, 0
	rep	stosb
	pop	di
	pop	cx

	add	di, 80

	dec	bl
	jnz	scroll_up_gfx640_last_row_loop

	pop	bx
	pop	ax

	pop	bx
	pop	si
	pop	di

	dec	al
	jmp	scroll_up_gfx640_1_row

    scroll_up_mode11:

	; Get the first and second window row offsets into DI and SI
  	push	ax

	; Get the address offset in video ram for the top left character into DI
	mov	al, 80 ; bytes per row
	mul	byte ch
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	add	al, cl
	adc	ah, 0
	mov	di, ax
  
	add	ax, 1280
	mov	si, ax

	pop	ax

	; bx = number of rows in the window to scroll
	mov	bl, dh
	sub	bl, ch
	mov	bh, 0
	shl	bx, 1
	shl	bx, 1
	shl	bx, 1
	shl	bx, 1

	; cx = number of bytes to move
	push	ax

	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	mov	cx, ax

	mov	ax, 0xa000
	mov	ds, ax
	mov	es, ax

	pop	ax

	cld

    scroll_up_mode11_1_row:
	cmp	al, 0
	je	scroll_up_done

	push	di
	push	si
	push	bx

    scroll_up_mode11_loop:
	cmp 	bx, 0
	je 	scroll_up_mode11_1_row_done

	push 	cx
	push 	di
	push	si

	rep	movsb

	pop	si
	pop	di
	pop	cx

	add	si, 80
	add	di, 80

	dec	bx
	jmp	scroll_up_mode11_loop

    scroll_up_mode11_1_row_done:

	; fill the last row with zeros
	push	ax
	push	bx
	mov	bl, 16

    scroll_up_mode11_last_row_loop:
	push	cx
	push	di
	mov	ax, 0
	rep	stosb
	pop	di
	pop	cx

	add	di, 80

	dec	bl
	jnz	scroll_up_mode11_last_row_loop

	pop	bx
	pop	ax

	pop	bx
	pop	si
	pop	di

	dec	al
	jmp	scroll_up_mode11_1_row


    scroll_up_mode13:

	; Get the first and second window row offsets into DI and SI
  	push	ax

	; Get the address offset in video ram for the top left character into DI
	mov	al, 40 ; characters per row
	mul	byte ch
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	add	al, cl
	adc	ah, 0
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	mov	di, ax
  
	add	ax, 2560
	mov	si, ax

	pop	ax

	; bx = number of rows in the window to scroll
	mov	bl, dh
	sub	bl, ch
	mov	bh, 0
	shl	bx, 1
	shl	bx, 1
	shl	bx, 1

	; cx = number of bytes to move
	push	ax

	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	mov	cx, ax

	mov	ax, 0xa000
	mov	ds, ax
	mov	es, ax

	pop	ax

	cld

    scroll_up_mode13_1_row:
	cmp	al, 0
	je	scroll_up_done

	push	di
	push	si
	push	bx

    scroll_up_mode13_loop:
	cmp 	bx, 0
	je 	scroll_up_mode13_1_row_done

	push 	cx
	push 	di
	push	si

	rep	movsb

	pop	si
	pop	di
	pop	cx

	add	si, 320
	add	di, 320

	dec	bx
	jmp	scroll_up_mode13_loop

    scroll_up_mode13_1_row_done:

	; fill the last row with zeros
	push	ax
	push	bx
	mov	bl, 8

    scroll_up_mode13_last_row_loop:
	push	cx
	push	di
	mov	ax, 0
	rep	stosb
	pop	di
	pop	cx

	add	di, 320

	dec	bl
	jnz	scroll_up_mode13_last_row_loop

	pop	bx
	pop	ax

	pop	bx
	pop	si
	pop	di

	dec	al
	jmp	scroll_up_mode13_1_row

    scroll_up_done:
	pop	si
	pop	di
	pop	es
	pop	ds

	pop	dx
	pop	cx
	pop	bx
	pop	ax
	ret

scroll_down_window:
  ; AL = number of lines by which to scroll down (00h = clear entire window)
  ; BH = attribute used to write blank lines at top of window
  ; CH,CL = row,column of window's upper left corner
  ; DH,DL = row,column of window's lower right corner
  ; BP = video page offset

	push	ax
	push	bx
	push	cx
	push	dx

	push	ds
	push	es
	push	di
	push	si

	push	ax
	mov	ax, 0x40
	mov	ds, ax
	pop	ax

	cmp	byte [vid_mode-bios_data], 0x04
	je	scroll_dn_gfx320
	cmp	byte [vid_mode-bios_data], 0x05
	je	scroll_dn_gfx320
	cmp	byte [vid_mode-bios_data], 0x06
	je	scroll_dn_gfx640
	cmp	byte [vid_mode-bios_data], 0x11
	je	scroll_dn_mode11
	cmp	byte [vid_mode-bios_data], 0x13
	je	scroll_dn_mode13

	; Get screen RAM address into es and ds
	push	ax

	mov	ax, 0xb800
	mov	es, ax
	mov	ds, ax

	pop	ax

	; Get the last and second last window row offsets into DI and SI
  	push	ax
	push	ds

	mov	ax, 0x40
	mov	ds, ax

	mov	ah, 0
	mov	al, [vid_cols-bios_data]	; ax = bytes per row
	mul	dh	; ax = window bottom row screen word offset
	add	al, dl	
	adc	ah, 0	; ax = window bottom right screen word offset
	shl	ax, 1	; convert to byte address
   	add	ax, bp  ; add video page offset

	mov	di, ax	
	sub	al, [vid_cols-bios_data]
	sbb	ah, 0
	sub	al, [vid_cols-bios_data]
	sbb	ah, 0
	mov	si, ax

	pop	ds
	pop	ax

	; bl = number of rows in the window to scroll
	mov	bl, dh
	sub	bl, ch

	; cx = number of words to move
	push	ax
	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	mov	cx, ax
	pop	ax

	std

    scroll_down_1_row:
	cmp	al, 0
	je	scroll_down_done

	push	di
	push	si
	push	bx

    scroll_down_loop:
	cmp 	bl, 0
	je 	scroll_down_1_row_done

	push 	cx
	push 	di
	push	si

	rep movsw

	pop	si
	pop	di
	pop	cx

	push	ax
	push	ds

	mov	ax, 0x40
	mov	ds, ax

	mov	ax, si
	sub	al, [vid_cols-bios_data]
	sbb	ah, 0
	sub	al, [vid_cols-bios_data]
	sbb	ah, 0
	mov	si, ax

	mov	ax, di
	sub	al, [vid_cols-bios_data]
	sbb	ah, 0
	sub	al, [vid_cols-bios_data]
	sbb	ah, 0
	mov	di, ax

	pop	ds
	pop	ax

	dec	bl
	jmp	scroll_down_loop

    scroll_down_1_row_done:

	; fill the last row with the bh attribute, null character
	push	ax
	push	cx
	mov	al, 0
	mov	ah, bh
	rep	stosw
	pop	cx
	pop	ax

	pop	bx
	pop	si
	pop	di

	dec	al
	jmp	scroll_down_1_row


	scroll_dn_gfx320:

	; Get the last and second last window row offsets into DI and SI
  	push	ax

	; Get the address offset in video ram for the top left character into DI
	mov	al, 80 ; bytes per row
	mul	byte dh
	shl	ax, 1
	shl	ax, 1
	add	al, dl
	adc	ah, 0
	add	al, dl
	adc	ah, 0
	add	ax, 240
	mov	di, ax
  
	sub	ax, 320
	mov	si, ax

	pop	ax

	; bl = number of rows in the window to scroll
	mov	bl, dh
	sub	bl, ch
	shl	bl, 1
	shl	bl, 1

	; cx = number of words to move
	push	ax
	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	mov	cx, ax
	pop	ax

	std

    scroll_dn_gfx320_1_row:
	cmp	al, 0
	je	scroll_down_done

	push	di
	push	si
	push	bx

    scroll_dn_gfx320_loop:
	cmp 	bl, 0
	je 	scroll_dn_gfx320_1_row_done

	push	ax

	push 	cx
	push 	di
	push	si

	mov	ax, 0xb800
	mov	ds, ax
	mov	es, ax
	rep	movsw

	pop	si
	pop	di
	pop	cx

	push 	cx
	push 	di
	push	si

	mov	ax, 0xba00
	mov	ds, ax
	mov	es, ax
	rep	movsw

	pop	si
	pop	di
	pop	cx

	sub	si, 80
	sub	di, 80

	pop	ax

	dec	bl
	jmp	scroll_dn_gfx320_loop

    scroll_dn_gfx320_1_row_done:

	; fill the last row with zeros
	push	ax
	push	bx
	mov	bl, 4

    scroll_dn_gfx320_last_row_loop:
	push	cx
	push	di
	mov	ax, 0xb800
	mov	es, ax
	mov	ax, 0
	rep	stosw
	pop	di
	pop	cx

	push	cx
	push	di
	mov	ax, 0xba00
	mov	es, ax
	mov	ax, 0
	rep	stosw
	pop	di
	pop	cx

	sub	di, 80

	dec	bl
	jnz	scroll_dn_gfx320_last_row_loop

	pop	bx
	pop	ax

	pop	bx
	pop	si
	pop	di

	dec	al
	jmp	scroll_dn_gfx320_1_row


    scroll_dn_gfx640:

	; Get the first and second window row offsets into DI and SI
  	push	ax

	; Get the address offset in video ram for the bottom right character into DI
	mov	al, 80 ; bytes per row
	mul	byte dh
	shl	ax, 1
	shl	ax, 1
	add	al, dl
	adc	ah, 0
	add	ax, 240
	mov	di, ax
  
	sub	ax, 320
	mov	si, ax

	pop	ax

	; bl = number of rows in the window to scroll
	mov	bl, dh
	sub	bl, ch
	shl	bl, 1
	shl	bl, 1

	; cx = number of bytes to move
	push	ax
	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	mov	cx, ax
	pop	ax

	std

    scroll_dn_gfx640_1_row:
	cmp	al, 0
	je	scroll_down_done

	push	di
	push	si
	push	bx

    scroll_dn_gfx640_loop:
	cmp 	bl, 0
	je 	scroll_dn_gfx640_1_row_done

	push	ax

	push 	cx
	push 	di
	push	si

	mov	ax, 0xb800
	mov	ds, ax
	mov	es, ax
	rep	movsb

	pop	si
	pop	di
	pop	cx

	push 	cx
	push 	di
	push	si

	mov	ax, 0xba00
	mov	ds, ax
	mov	es, ax
	rep	movsb

	pop	si
	pop	di
	pop	cx

	sub	si, 80
	sub	di, 80

	pop	ax

	dec	bl
	jmp	scroll_dn_gfx640_loop

    scroll_dn_gfx640_1_row_done:

	; fill the last row with zeros
	push	ax
	push	bx
	mov	bl, 4

	scroll_dn_gfx640_last_row_loop:
	push	cx
	push	di
	mov	ax, 0xb800
	mov	es, ax
	mov	ax, 0
	rep	stosb
	pop	di
	pop	cx

	push	cx
	push	di
	mov	ax, 0xba00
	mov	es, ax
	mov	ax, 0
	rep	stosb
	pop	di
	pop	cx

	sub	di, 80

	dec	bl
	jnz	scroll_dn_gfx640_last_row_loop

	pop	bx
	pop	ax

	pop	bx
	pop	si
	pop	di

	dec	al
	jmp	scroll_dn_gfx640_1_row

    scroll_dn_mode11:

	; Get the first and second window row offsets into DI and SI
  	push	ax

	; Get the address offset in video ram for the bottom right character into DI
	mov	al, 80 ; bytes per row
	mul	byte dh
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	add	al, dl
	adc	ah, 0
	add	ax, 1200
	mov	di, ax
  
	sub	ax, 1280
	mov	si, ax

	pop	ax

	; bl = number of rows in the window to scroll
	mov	bl, dh
	sub	bl, ch
	mov	bh, 0
	shl	bx, 1
	shl	bx, 1
	shl	bx, 1
	shl	bx, 1

	; cx = number of bytes to move
	push	ax

	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	mov	cx, ax

	mov	ax, 0xa000
	mov	ds, ax
	mov	es, ax

	pop	ax

	std

    scroll_dn_mode11_1_row:
	cmp	al, 0
	je	scroll_down_done

	push	di
	push	si
	push	bx

    scroll_dn_mode11_loop:
	cmp 	bx, 0
	je 	scroll_dn_mode11_1_row_done

	push 	cx
	push 	di
	push	si

	rep	movsb

	pop	si
	pop	di
	pop	cx

	sub	si, 80
	sub	di, 80

	dec	bx
	jmp	scroll_dn_mode11_loop

    scroll_dn_mode11_1_row_done:

	; fill the last row with zeros
	push	ax
	push	bx
	mov	bl, 16

	scroll_dn_mode11_last_row_loop:
	push	cx
	push	di
	mov	ax, 0
	rep	stosb
	pop	di
	pop	cx

	sub	di, 80

	dec	bl
	jnz	scroll_dn_mode11_last_row_loop

	pop	bx
	pop	ax

	pop	bx
	pop	si
	pop	di

	dec	al
	jmp	scroll_dn_mode11_1_row

    scroll_dn_mode13:

	; Get the first and second window row offsets into DI and SI
  	push	ax

	; Get the address offset in video ram for the top left character into DI
	mov	al, 40 ; characters per row
	mul	byte dh
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	add	al, dl
	adc	ah, 0
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	add	ax, 2240
	mov	di, ax
  
	sub	ax, 2560
	mov	si, ax

	pop	ax

	; bx = number of rows in the window to scroll
	mov	bl, dh
	sub	bl, ch
	mov	bh, 0
	shl	bx, 1
	shl	bx, 1
	shl	bx, 1

	; cx = number of bytes to move
	push	ax

	mov	ax, 0
	mov	al, dl
	sub	al, cl
	inc	al
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	mov	cx, ax

	mov	ax, 0xa000
	mov	ds, ax
	mov	es, ax

	pop	ax

	std

    scroll_dn_mode13_1_row:
	cmp	al, 0
	je	scroll_down_done

	push	di
	push	si
	push	bx

    scroll_dn_mode13_loop:
	cmp 	bx, 0
	je 	scroll_dn_mode13_1_row_done

	push 	cx
	push 	di
	push	si

	rep	movsb

	pop	si
	pop	di
	pop	cx

	sub	si, 320
	sub	di, 320

	dec	bx
	jmp	scroll_dn_mode13_loop

    scroll_dn_mode13_1_row_done:

	; fill the last row with zeros
	push	ax
	push	bx
	mov	bl, 8

    scroll_dn_mode13_last_row_loop:
	push	cx
	push	di
	mov	ax, 0
	rep	stosb
	pop	di
	pop	cx

	sub	di, 320

	dec	bl
	jnz	scroll_dn_mode13_last_row_loop

	pop	bx
	pop	ax

	pop	bx
	pop	si
	pop	di

	dec	al
	jmp	scroll_dn_mode13_1_row


    scroll_down_done:
	pop	si
	pop	di
	pop	es
	pop	ds

	pop	dx
	pop	cx
	pop	bx
	pop	ax
	ret

clear_screen:
; Clear video memory with attribute in BH

	push	ax
	push	bx
	push	cx
	push	ds
	push	es
	push	di
	push	si

	mov	ax, 0x40
	mov	ds, ax

	mov	al, [disp_page-bios_data]
	mov	ah, 0
	shl	ax, 1
	mov	si, ax

	mov	byte [si+curpos_x-bios_data], 0
	mov	byte [crt_curpos_x-bios_data], 0
	mov	byte [si+curpos_y-bios_data], 0
	mov	byte [crt_curpos_y-bios_data], 0

	cmp	byte [vid_mode-bios_data], 4
	je	clear_gfx
	cmp	byte [vid_mode-bios_data], 5
	je	clear_gfx
	cmp	byte [vid_mode-bios_data], 6
	je	clear_gfx
	cmp	byte [vid_mode-bios_data], 0x11
	je	clear_gfx2
	cmp	byte [vid_mode-bios_data], 0x13
	je	clear_gfx2

	cld
	mov	al, [vid_page_size+1-bios_data]
	mul	byte [disp_page-bios_data]
	mov	ah, al
	mov	al, 0
	shr	ax, 1
	shr	ax, 1
	shr	ax, 1
	shr	ax, 1
	add	ax, 0xb800
	mov	es, ax
	mov	di, 0
	mov	al, 0
	mov	ah, bh
	mov	cx, [vid_page_size-bios_data]
	rep	stosw
	jmp	clear_done

  clear_gfx:
	cld
	mov	ax, 0xb800
	mov	es, ax
	mov	di, 0
	mov	ax, 0
	mov	cx, 4000
	rep	stosw

	mov	ax, 0xba00
	mov	es, ax
	mov	di, 0
	mov	ax, 0
	mov	cx, 4000
	rep	stosw
	jmp	clear_done

  clear_gfx2:
	cld
	mov	ax, 0xa000
	mov	es, ax
	mov	di, 0
	mov	ax, 0
	mov	cx, 0x8000
	rep	stosw

	mov	ax, 0xa800
	mov	es, ax
	mov	di, 0
	mov	ax, 0
	mov	cx, 0x8000
	rep	stosw
	jmp	clear_done

  clear_done:
	call	set_crtc_cursor_pos

	pop	si
	pop	di
	pop	es
	pop	ds
	pop	cx
	pop	bx
	pop	ax

	ret

put_cga320_char:
	; Character is in AL
	; Colour is in AH
	push	ax
	push	bx
	push	cx
	push	ds	
	push	es
	push	di
	push	bp

	; Get the colour mask into BH
	cmp	ah, 1
	jne	put_cga320_char_c2
	mov	bh, 0x55
	jmp	put_cga320_char_cdone
    put_cga320_char_c2:
	cmp	ah, 2
	jne	put_cga320_char_c3
	mov	bh, 0xAA
	jmp	put_cga320_char_cdone
    put_cga320_char_c3:
	mov	bh, 0xFF

    put_cga320_char_cdone:
	; Get glyph character top offset into bp and character segment into cx
	test	al, 0x80
	jne	put_cga320_char_high

	; Characters 0 .. 127 are always in ROM
	mov	ah, 0
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	add	ax, cga_glyphs
	mov	bp, ax

	mov	cx, cs

	jmp	put_cga320_char_vidoffset

    put_cga320_char_high:
	; Characters 128 .. 255 get their address from interrupt vector 1F
	and	al, 0x7F
	mov	ah, 0
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	mov	bp, ax

	mov	ax, 0
	mov	ds, ax
	mov	ax, [ds:0x7c]
	add	bp, ax 

	mov	cx, [ds:0x7e]

    put_cga320_char_vidoffset:
	mov	ax, 0x40
	mov	ds, ax

	; Get the address offset in video ram for the top of the character into DI
	mov	al, 80 ; bytes per row
	mul	byte [ds:curpos_y-bios_data]
	shl	ax, 1
	shl	ax, 1
	add	al, [ds:curpos_x-bios_data]
	adc	ah, 0
	add	al, [ds:curpos_x-bios_data]
	adc	ah, 0
	mov	di, ax

	; get segment for character data into ds
	mov	ds, cx

	; get video RAM address for even lines into es
	mov	ax, 0xb800
	mov	es, ax

	push	di

	mov	bl, byte [ds:bp]
	; Translate character glyph into CGA 320 format
	call	put_cga320_char_double
	stosw
	add	di, 78

	mov	bl, byte [ds:bp+2]
	; Translate character glyph into CGA 320 format
	call put_cga320_char_double
	stosw
	add	di, 78

	mov	bl, byte [ds:bp+4]
	; Translate character glyph into CGA 320 format
	call put_cga320_char_double
	stosw
	add	di, 78

	mov	bl, byte [ds:bp+6]
	; Translate character glyph into CGA 320 format
	call put_cga320_char_double
	stosw

	; get video RAM address for odd lines into es
	mov ax, 0xba00
	mov es, ax

	pop	di

	mov	bl, byte [ds:bp+1]
	; Translate character glyph into CGA 320 format
	call put_cga320_char_double
	stosw
	add	di, 78

	mov	bl, byte [ds:bp+3]
	; Translate character glyph into CGA 320 format
	call put_cga320_char_double
	stosw
	add	di, 78

	mov	bl, byte [ds:bp+5]
	; Translate character glyph into CGA 320 format
	call put_cga320_char_double
	stosw
	add	di, 78

	mov	bl, byte [ds:bp+7]
	; Translate character glyph into CGA 320 format
	call put_cga320_char_double
	stosw	

    put_cga320_char_done:
	pop	bp
	pop	di
	pop	es
	pop	ds
	pop	cx
	pop	bx
	pop	ax
	ret

put_cga320_char_double:
	; BL = character bit pattern
	; BH = colour mask
	; AX is set to double width character bit pattern

	mov	ax, 0
	test	bl, 0x80
	je	put_chachar_bit6
	or	al, 0xc0
    put_chachar_bit6:
	test	bl, 0x40
	je	put_chachar_bit5
	or	al, 0x30
    put_chachar_bit5:
	test	bl, 0x20
	je	put_chachar_bit4
	or	al, 0x0c
    put_chachar_bit4:
	test	bl, 0x10
	je	put_chachar_bit3
	or	al, 0x03
    put_chachar_bit3:
	test	bl, 0x08
	je	put_chachar_bit2
	or	ah, 0xc0
    put_chachar_bit2:
	test	bl, 0x04
	je	put_chachar_bit1
	or	ah, 0x30
    put_chachar_bit1:
	test	bl, 0x02
	je	put_chachar_bit0
	or	ah, 0x0c
    put_chachar_bit0:
	test	bl, 0x01
	je	put_chachar_done
	or	ah, 0x03
    put_chachar_done:
	and	al, bh
	and	ah, bh
	ret

put_cga640_char:
    ; Character is in AL

	push	ax
	push	bx
	push	cx
	push	ds	
	push	es
	push	di
	push	bp

	; Get glyph character top offset into bp and character segment into cx
	test	al, 0x80
	jne	put_cga640_char_high

	; Characters 0 .. 127 are always in ROM
	mov	ah, 0
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	add	ax, cga_glyphs
	mov	bp, ax

	mov	cx, cs

	jmp	put_cga640_char_vidoffset

    put_cga640_char_high:
	; Characters 128 .. 255 get their address from interrupt vector 1F
	and	al, 0x7f
	mov	ah, 0
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	mov	bp, ax

	mov	ax, 0
	mov	ds, ax
	mov	ax, [ds:0x7c]
	add	bp, ax 

	mov	cx, [ds:0x7e]

    put_cga640_char_vidoffset:
	mov	ax, 0x40
	mov	ds, ax

	; Get the address offset in video ram for the top of the character into DI
	mov	al, 80 ; bytes per row
	mul	byte [ds:curpos_y-bios_data]
	shl	ax, 1
	shl	ax, 1
	add	al, [ds:curpos_x-bios_data]
	adc	ah, 0
	mov	di, ax

	; get segment for character data into ds
	mov	ds, cx

	; get video RAM address for even lines into ds
	mov	ax, 0xb800
	mov	es, ax

	push	di

	mov	al, byte [ds:bp]
	stosb
	add	di, 79

	mov	al, byte [ds:bp+2]
	stosb
	add	di, 79

	mov	al, byte [ds:bp+4]
	stosb
	add	di, 79

	mov	al, byte [ds:bp+6]
	stosb

	; get video RAM address for odd lines into ds
	mov ax, 0xba00
	mov es, ax

	pop	di

	mov	al, byte [ds:bp+1]
	stosb
	add	di, 79

	mov	al, byte [ds:bp+3]
	stosb
	add	di, 79

	mov	al, byte [ds:bp+5]
	stosb
	add	di, 79

	mov	al, byte [ds:bp+7]
	stosb

	pop	bp
	pop	di
	pop	es
	pop	ds
	pop	cx
	pop	bx
	pop	ax
	ret

put_mode11_char:
    ; Character is in AL
	push	ax
	push	bx
	push	cx
	push	ds	
	push	es
	push	di
	push	bp

	; Get glyph character top offset into bp and character segment into cx
	test	al, 0x80
	jne	put_mode11_char_high

	; Characters 0 .. 127 are always in ROM
	mov	ah, 0
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	add	ax, cga_glyphs
	mov	bp, ax

	mov	cx, cs

	jmp	put_mode11_char_vidoffset

    put_mode11_char_high:
	; Characters 128 .. 255 get their address from interrupt vector 1F
	and	al, 0x7f
	mov	ah, 0
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	mov	bp, ax

	mov	ax, 0
	mov	ds, ax
	mov	ax, [ds:0x7c]
	add	bp, ax 

	mov	cx, [ds:0x7e]

    put_mode11_char_vidoffset:
	mov	ax, 0x40
	mov	ds, ax

	; Get the address offset in video ram for the top of the character into DI
	mov	al, 80 ; bytes per row
	mul	byte [ds:curpos_y-bios_data]
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	add	al, [ds:curpos_x-bios_data]
	adc	ah, 0
	mov	di, ax

	; get segment for character data into ds
	mov	ds, cx

	; get video RAM address for even lines into ds
	mov	ax, 0xa000
	mov	es, ax

	mov	al, byte [ds:bp]
	stosb
	add	di, 79
	stosb
	add	di, 79

	mov	al, byte [ds:bp+1]
	stosb
	add	di, 79
	stosb
	add	di, 79

	mov	al, byte [ds:bp+2]
	stosb
	add	di, 79
	stosb
	add	di, 79

	mov	al, byte [ds:bp+3]
	stosb
	add	di, 79
	stosb
	add	di, 79

	mov	al, byte [ds:bp+4]
	stosb
	add	di, 79
	stosb
	add	di, 79

	mov	al, byte [ds:bp+5]
	stosb
	add	di, 79
	stosb
	add	di, 79

	mov	al, byte [ds:bp+6]
	stosb
	add	di, 79
	stosb
	add	di, 79

	mov	al, byte [ds:bp+7]
	stosb
	add	di, 79
	stosb
	add	di, 79


	pop	bp
	pop	di
	pop	es
	pop	ds
	pop	cx
	pop	bx
	pop	ax
	ret

put_mode13_char:
	; Character is in AL
	; Colour is in AH
	push	ax
	push	bx
	push	cx
	push	dx
	push	ds	
	push	es
	push	di
	push	bp

	mov	bh, ah
	push	bx

	; Get glyph character top offset into BP and character segment into CX
	test	al, 0x80
	jne	put_mode13_char_high

	; Characters 0 .. 127 are always in ROM
	mov	ah, 0
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	add	ax, cga_glyphs
	mov	bp, ax

	mov	cx, cs

	jmp	put_mode13_char_vidoffset

    put_mode13_char_high:
	; Characters 128 .. 255 get their address from interrupt vector 1F
	and	al, 0x7F
	mov	ah, 0
	shl	ax, 1
	shl	ax, 1
	shl	ax, 1
	mov	bp, ax

	mov	ax, 0
	mov	ds, ax
	mov	ax, [ds:0x7c]
	add	bp, ax 

	mov	cx, [ds:0x7e]

    put_mode13_char_vidoffset:
	mov	ax, 0x40
	mov	ds, ax

	; Get the address offset in video ram for the top of the character into DI
	mov	ax, 2560 ; bytes per row
	mov	bl, [ds:curpos_y-bios_data]
	mov	bh, 0
	mul	bx
	mov	bl, [ds:curpos_x-bios_data]
	shl	bx, 1
	shl	bx, 1
	shl	bx, 1
	add	ax, bx
	mov	di, ax

	; get segment for character data into ds
	mov	ds, cx

	; get video RAM address for even lines into es
	mov	ax, 0xa000
	mov	es, ax

	pop	bx

	mov	cl, 8
    put_mode13_char_loop:
	mov	bl, byte [ds:bp]

	mov	ch, 0x80
    put_mode13_char_line_loop:
	test	bl, ch
	jz	put_mode13_char_clear
	mov	al, bh
	jmp	put_mode13_char_pixel
    put_mode13_char_clear:
	mov	al, 0x00
    put_mode13_char_pixel:
	stosb

	shr	ch, 1
	jnz	put_mode13_char_line_loop

	inc	bp
	add	di, 312
	dec	cl
	jnz	put_mode13_char_loop

    put_mode13_char_done:
	pop	bp
	pop	di
	pop	es
	pop	ds
	pop	dx
	pop	cx
	pop	bx
	pop	ax
	ret


; Reaches up into the stack before the end of an interrupt handler, and sets the carry flag

reach_stack_stc:

	push	bp
	mov	bp, sp
	or	word [bp+6], 1
	pop	bp
	iret

; Reaches up into the stack before the end of an interrupt handler, and clears the carry flag

reach_stack_clc:

	push	bp
	mov	bp, sp
	and	word [bp+6], 0xfffe
	pop	bp
	iret

; Reaches up into the stack before the end of an interrupt handler, and returns with the current
; setting of the carry flag

reach_stack_carry:

	jc	reach_stack_stc
	jmp	reach_stack_clc


; BIOS interupt redirect for 100% compatible BIOS
int09_redirect_start:
	jmp [cs:int_table +9*4]
int09_redirect_end:



; ****************************************************************************************
; That's it for the code. Now, the data tables follow.
; ****************************************************************************************

; Standard PC-compatible BIOS data area - to copy to 40:0

bios_data:

com1addr	dw	0x03F8
com2addr	dw	0x02F8
com3addr	dw	0x03E8
com4addr	dw	0x02E8
lpt1addr	dw	0
lpt2addr	dw	0
lpt3addr	dw	0
lpt4addr	dw	0
equip		dw	0b0000100000000001 ; serial port at bits 11-9 (000 = none, 001 = 1, 010 = 2, 011  3, 100 = 4)
;equip		dw	0b0000000000100001
		db	0
memsize		dw	0x280
		db	0
		db	0
keyflags1	db	0
keyflags2	db	0
		db	0
kbbuf_head	dw	kbbuf-bios_data
kbbuf_tail	dw	kbbuf-bios_data
kbbuf: times 32	db	'X'
drivecal	db	0
diskmotor	db	0
motorshutoff	db	0x07
disk_laststatus	db	0
times 7		db	0

bios_data_vid_start: ; // offset 0x49
vid_mode	db	0x03
vid_cols	dw	80
vid_page_size	dw	0x1000
vid_page_offset	dw	0
curpos_x	db	0
curpos_y	db	0
times 7		dw	0 ; cursor pos for pages 
cur_v_end	db	0x07
cur_v_start	db	0x06
disp_page	db	0
crtport		dw	0x3d4
vid_3x8		db	0x09
vid_3x9		db	0
vid_rom_offset		dw	0
vid_rom_segaddr		dw	0
biod_data_vid_end:

last_intr	db	0
clk_dtimer	dd	0
clk_rollover	db	0
ctrl_break	db	0
soft_rst_flg	dw	0x1234
		db	0
num_hd		db	0
		db	0
		db	0
		dd	0
com1_timeout	db	27	; 500 ms
com2_timeout	db	27	; 500 ms
com3_timeout	db	27	; 500 ms
com4_timeout	db	27	; 500 ms
kbbuf_start_ptr	dw	0x001e
kbbuf_end_ptr	dw	0x003e
vid_rows	db	24         ; at 40:84
		db	8	; scan lines per character
		db	0
vidmode_opt	db	0x96 ; at 40:87 0x70
		db	0x09 ; 0x89
		db	0x81 ; 0x51
video_card	db	0x0c ; 0x0c
		db	0
		db	0
		db	0
		db	0
		db	0
		db	0
		db	0
		db	0
		db	0
		db	0
		db	0
kb_mode		db	0
kb_led		db	0
		db	0
		db	0
		db	0
		db	0
boot_device	db	0
crt_curpos_x	db	0
crt_curpos_y	db	0
key_now_down	db	0
next_key_fn	db	0
cursor_visible	db	1
escape_flag_last	db	0
next_key_alt	db	0
escape_flag	db	0
notranslate_flg	db	0
this_keystroke	db	0
		db	0
ending:		times (0xff-($-com1addr)) db	0

; Keyboard scan code translation tables
; < 0x80 = normal key
;   0x8n = keyboard shift flag n set/clear on key down/up
;                n = 1 : Shift
;                n = 2 : Control
;                n = 3 : Alt
;                n = 4 : Scroll Lock
;                n = 5 : Num Lock
;                n = 6 : Caps Lock
;                n = 7 : Insert 
;   0x9n = toggle shift flag n on key down
;   0xAn = ASCII = 0, scan code is modified
;                n = 0 : add 0x76 (alt number keys)
;                n = 1 : add 0x2d (alt-Fn)
;                n = 2 : add 0x23 (ctrl-Fn)
;                n = 3 ; Add 0x19 (shift-Fn)
;   0xFF = Do not report a key press

; plain key
scan2a_tbl	db	0xFF, 0x1b,  '1',  '2',  '3',  '4',  '5',  '6',  '7',  '8',  '9',  '0',  '-',  '=', 0x08, 0x09
		db	 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  'o',  'p',  '[',  ']', 0x0d, 0x82,  'a',  's'
 		db	 'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';', 0x27,  '`', 0x81,  '\',  'z',  'x',  'c',  'v'
		db	 'b',  'n',  'm',  ',',  '.',  '/', 0x80,  '*', 0x83,  ' ', 0x96, 0x00, 0x00, 0x00, 0x00, 0x00
		db	0x00, 0x00, 0x00, 0x00, 0x00, 0x95, 0x94, 0x00, 0x00, 0x00,  '-', 0x00, 0x00, 0x00,  '+', 0x00
		db	0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
		db	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
		db	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
; shift+key
sscan2a_tbl	db	0xFF, 0x1b,  '!',  '@',  '#',  '$',  '%',  '^',  '&',  '*',  '(',  ')',  '_',  '+', 0x08, 0x09
		db	 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  'O',  'P',  '{',  '}', 0x0d, 0x82,  'A',  'S'
 		db	 'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':', 0x22,  '~', 0x81,  '|',  'Z',  'X',  'C',  'V'
		db	 'B',  'N',  'M',  '<',  '>',  '?', 0x80, 0xFF, 0x83,  ' ', 0x96, 0xA3, 0xA3, 0xA3, 0xA3, 0xA3
		db	0xA3, 0xA3, 0xA3, 0xA3, 0xA3, 0x95, 0x94,  '7',  '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1'
		db	 '2', '3',   '0',  '.', 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
		db	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
		db	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
; control+key
cscan2a_tbl	db	0xFF, 0x1b, 0xFF, 0x00, 0xFF, 0xFF, 0xFF, 0x1e, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x08, 0x09
		db	0x11, 0x17, 0x05, 0x12, 0x14, 0x19, 0x15, 0x09, 0x0F, 0x10, 0x1B, 0x1D, 0x0a, 0x82, 0x01, 0x13
 		db	0x04, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0xFF, 0xFF, 0xFF, 0x81, 0x1C, 0x1A, 0x18, 0x03, 0x16
		db	0x02, 0x0E, 0x0D, 0xFF, 0xFF, 0xFF, 0x80, 0xFF, 0x83,  ' ', 0x96, 0xA2, 0xA2, 0xA2, 0xA2, 0xA2
		db	0xA2, 0xA2, 0xA2, 0xA2, 0xA2, 0x95, 0x94, 0xFF, 0x00, 0xFF, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0xFF
		db	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
		db	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
		db	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
; alt+key
ascan2a_tbl	db	0xFF, 0x1b, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xFF, 0xFF
		db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x82, 0x00, 0x00
 		db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x81, 0xFF, 0x00, 0x00, 0x00, 0x00
		db	0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x80, 0xFF, 0x83, 0xFF, 0x96, 0xA1, 0xA1, 0xA1, 0xA1, 0xA1
		db	0xA1, 0xA1, 0xA1, 0xA1, 0xA1, 0x95, 0x94, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
		db	0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
		db	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
		db	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
	 
; Interrupt vector table - to copy to 0:0

int_table	dw int0
          	dw 0xf000
          	dw int1
          	dw 0xf000
          	dw int2
          	dw 0xf000
          	dw int3
          	dw 0xf000
          	dw int4
          	dw 0xf000
          	dw int5
          	dw 0xf000
          	dw int6
          	dw 0xf000
          	dw int7
          	dw 0xf000
          	dw int8
          	dw 0xf000
          	dw int9
          	dw 0xf000
          	dw inta
          	dw 0xf000
          	dw intb
          	dw 0xf000
          	dw intc
          	dw 0xf000
          	dw intd
          	dw 0xf000
          	dw inte
          	dw 0xf000
          	dw intf
          	dw 0xf000
          	dw int10
          	dw 0xf000
          	dw int11
          	dw 0xf000
          	dw int12
          	dw 0xf000
          	dw int13
          	dw 0xf000
          	dw int14
          	dw 0xf000
          	dw int15
          	dw 0xf000
          	dw int16
          	dw 0xf000
          	dw int17
          	dw 0xf000
          	dw int18
          	dw 0xf000
          	dw int19
          	dw 0xf000
          	dw int1a
          	dw 0xf000
          	dw int1b
          	dw 0xf000
          	dw int1c
          	dw 0xf000
          	dw video_init_table
          	dw 0xf000
          	dw int1e
                dw 0xf000
                dw cga_glyphs + 1024
		dw 0xf000

itbl_size	dw $-int_table


video_init_table:
	; 6845 register values for 40x25 modes
	db	0x39, 0x28, 0x2d, 0x10, 0x1f, 0x06, 0x19, 0x1c, 0x02, 0x07, 0x66, 0x07, 0x00, 0x00, 0x00, 0x00
	; 6845 register values for 80x25 modes
	db	0x72, 0x50, 0x5a, 0x10, 0x1f, 0x06, 0x19, 0x1c, 0x02, 0x07, 0x66, 0x07, 0x00, 0x00, 0x00, 0x00
	; 6845 register values for graphics modes
	db	0x39, 0x28, 0x2d, 0x10, 0x7f, 0x06, 0x64, 0x70, 0x02, 0x07, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00
	; 6845 register values for 80x25 monochrome modes
	db	0x72, 0x50, 0x5a, 0x10, 0x1f, 0x06, 0x19, 0x1c, 0x02, 0x07, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00
	; wSize40x25
	dw	0x3E8
	; wSize80x25
	dw	0x7d0
	; wSizeLoRes
	dw	0x3e80
	; wSizeHiRes
	dw	0x3e80
	; abClmCnts (Text columns in each mode)
	db	0x28, 0x28, 0x50, 0x50, 0x28, 0x28, 0x50, 0x00
	; abModeCodes (port 3d8 values for each mode)
	db	0x0c, 0x08, 0x02, 0x09, 0x0a, 0x0e, 0x1a, 0x00 

vid_static_table:
	db 0x7f ; bits 0 .. 7 = modes 00h .. 07h supported
	db 0x00 ; bits 0 .. 7 = modes 08h .. 0fh supported
	db 0x0a ; bits 0 .. 3 = modes 10h .. 13h supported
	db 0x00 ; IBM reserved
	db 0x00 ; IBM reserved
	db 0x00 ; IBM reserved
	db 0x00 ; IBM reserved
	db 0x01 ; scan lines suppported: bit 0 = 200, 1 = 350, 2 = 400
	db 0x08 ; total number of character blocks in text mode
	db 0x08 ; maximum number of character blocks in text mode
	dw 0x046c ; misc support flags
	dw 0x00 ; reserved
	db 0x08 ; save pointer function flags
	db 0x00 ; reserved 


; Internal variables for VMEM driver

int8_ctr	db	0
in_update	db	0
last_attrib	db	0
int_curpos_x	db	0
int_curpos_y	db	0

; Now follow the tables for instruction decode helping

; R/M mode tables

rm_mode0_reg1	db	3, 3, 5, 5, 6, 7, 12, 3
rm_mode012_reg2	db	6, 7, 6, 7, 12, 12, 12, 12
rm_mode0_disp	db	0, 0, 0, 0, 0, 0, 1, 0
rm_mode0_dfseg	db	11, 11, 10, 10, 11, 11, 11, 11

rm_mode12_reg1	db	3, 3, 5, 5, 6, 7, 5, 3
rm_mode12_disp	db	1, 1, 1, 1, 1, 1, 1, 1
rm_mode12_dfseg	db	11, 11, 10, 10, 11, 11, 10, 11

; Opcode decode tables

xlat_ids	db	 9,  9,  9,  9,  7,  7, 25, 26,  9,  9,  9,  9,  7,  7, 25, 50 
		db	 9,  9,  9,  9,  7,  7, 25, 26,  9,  9,  9,  9,  7,  7, 25, 26 
		db	 9,  9,  9,  9,  7,  7, 27, 28,  9,  9,  9,  9,  7,  7, 27, 28 
		db	 9,  9,  9,  9,  7,  7, 27, 29,  9,  9,  9,  9,  7,  7, 27, 29 
		db	 2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2 
		db	 3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4 
		db	53, 54, 55, 70, 71, 71, 72, 72, 56, 58, 57, 58, 59, 59, 60, 60 
		db	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 
		db	 8,  8,  8,  8, 15, 15, 24, 24,  9,  9,  9,  9, 10, 10, 10, 10 
		db	16, 16, 16, 16, 16, 16, 16, 16, 30, 31, 32, 69, 33, 34, 35, 36 
		db	11, 11, 11, 11, 17, 17, 18, 18, 47, 47, 17, 17, 17, 17, 18, 18 
		db	 1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1 
		db	12, 12, 19, 19, 37, 37, 20, 20, 51, 52, 19, 19, 38, 39, 40, 19 
		db	12, 12, 12, 12, 41, 42, 43, 44, 69, 69, 69, 69, 69, 69, 69, 69 
		db	13, 13, 13, 13, 21, 21, 22, 22, 14, 14, 14, 14, 21, 21, 22, 22 
		db	48,  0, 23, 23, 49, 45,  6,  6, 46, 46, 46, 46, 46, 46,  5,  5

ex_data  	db	 0,  0,  0,  0,  0,  0,  8,  8,  1,  1,  1,  1,  1,  1,  9, 36 
		db	 2,  2,  2,  2,  2,  2, 10, 10,  3,  3,  3,  3,  3,  3, 11, 11 
		db	 4,  4,  4,  4,  4,  4,  8,  0,  5,  5,  5,  5,  5,  5,  9,  1
		db	 6,  6,  6,  6,  6,  6, 10,  2,  7,  7,  7,  7,  7,  7, 11,  0 
		db	 0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,  1,  1,  1,  1 
		db	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 
		db	 0,  0, 21, 21, 21, 21, 21, 21,  0,  0,  0,  0, 21, 21, 21, 21 
		db	21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21 
		db	 0,  0,  0,  0,  0,  0,  0,  0,  8,  8,  8,  8, 12, 12, 12, 12 
		db	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 255, 0 
		db	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  2,  2,  1,  1
		db	 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 
		db	 1,  1,  0,  0, 16, 22,  0,  0,  0,  0,  1,  1,  0, 255, 48, 2 
		db	 0,  0,  0,  0, 255, 255, 40, 11, 3, 3,  3,  3,  3,  3,  3,  3
		db	43, 43, 43, 43,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1
		db	 1, 21,  0,  0,  2, 40, 21, 21, 80, 81, 92, 93, 94, 95,  0,  0

std_flags	db	3, 3, 3, 3, 3, 3, 0, 0, 5, 5, 5, 5, 5, 5, 0, 0
		db	1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0 
		db	5, 5, 5, 5, 5, 5, 0, 1, 3, 3, 3, 3, 3, 3, 0, 1 
		db	5, 5, 5, 5, 5, 5, 0, 1, 3, 3, 3, 3, 3, 3, 0, 1 
		db	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	1, 1, 1, 1, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 5, 5, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 5, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

base_size	db	2, 2, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 2
		db	2, 2, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 1 
		db	2, 2, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 1
		db	2, 2, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 1, 1, 1, 1 
		db	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
		db	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 
		db	1, 1, 1, 1, 1, 1, 1, 1, 3, 1, 2, 1, 1, 1, 1, 1 
		db	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
		db	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2 
		db	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1 
		db	3, 3, 3, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
 		db	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 
		db	3, 3, 0, 0, 2, 2, 2, 2, 4, 1, 0, 0, 0, 0, 0, 0 
		db	2, 2, 2, 2, 2, 2, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2 
		db	2, 2, 2, 2, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1, 1, 1
		db	1, 2, 1, 1, 1, 1, 2, 2, 1, 1, 1, 1, 1, 1, 2, 2

i_w_adder	db	0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0 
		db	0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0 
		db	0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0
		db	0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0
 		db	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 
		db	0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0

i_mod_adder	db	1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0 
		db	1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0 
		db	1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0 
		db	1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 	
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0 
		db	1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 
		db	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 
		db	0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1

flags_mult	db	0, 2, 4, 6, 7, 8, 9, 10, 11

jxx_dec_a	db	48, 40, 43, 40, 44, 41, 49, 49
jxx_dec_b	db	49, 49, 49, 43, 49, 49, 49, 43
jxx_dec_c	db	49, 49, 49, 49, 49, 49, 44, 44
jxx_dec_d	db	49, 49, 49, 49, 49, 49, 48, 48

parity		db	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1 
		db	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 
		db	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 
		db	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1 
		db	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 
		db	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1 
		db	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1 
		db	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 
		db	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 
		db	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1 
		db	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1
 		db	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 
		db	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1 
		db	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 
		db	0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 
		db	1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1

; CGA Character set patterns

cga_glyphs:  
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x81, 0xa5, 0x81, 0xbd, 0x99, 0x81, 0x7e
  db	0x7e, 0xff, 0xdb, 0xff, 0xc3, 0xe7, 0xff, 0x7e, 0x6c, 0xfe, 0xfe, 0xfe, 0x7c, 0x38, 0x10, 0x00
  db	0x10, 0x38, 0x7c, 0xfe, 0x7c, 0x38, 0x10, 0x00, 0x38, 0x7c, 0x38, 0xfe, 0xfe, 0xd6, 0x10, 0x38
  db	0x10, 0x10, 0x38, 0x7c, 0xfe, 0x7c, 0x10, 0x38, 0x00, 0x00, 0x18, 0x3c, 0x3c, 0x18, 0x00, 0x00
  db	0xff, 0xff, 0xe7, 0xc3, 0xc3, 0xe7, 0xff, 0xff, 0x00, 0x3c, 0x66, 0x42, 0x42, 0x66, 0x3c, 0x00
  db	0xff, 0xc3, 0x99, 0xbd, 0xbd, 0x99, 0xc3, 0xff, 0x0f, 0x03, 0x05, 0x7d, 0x84, 0x84, 0x84, 0x78
  db	0x3c, 0x42, 0x42, 0x42, 0x3c, 0x18, 0x7e, 0x18, 0x3f, 0x21, 0x3f, 0x20, 0x20, 0x60, 0xe0, 0xc0
  db	0x3f, 0x21, 0x3f, 0x21, 0x23, 0x67, 0xe6, 0xc0, 0x18, 0xdb, 0x3c, 0xe7, 0xe7, 0x3c, 0xdb, 0x18
  db	0x80, 0xe0, 0xf8, 0xfe, 0xf8, 0xe0, 0x80, 0x00, 0x02, 0x0e, 0x3e, 0xfe, 0x3e, 0x0e, 0x02, 0x00
  db	0x18, 0x3c, 0x7e, 0x18, 0x18, 0x7e, 0x3c, 0x18, 0x24, 0x24, 0x24, 0x24, 0x24, 0x00, 0x24, 0x00
  db	0x7f, 0x92, 0x92, 0x72, 0x12, 0x12, 0x12, 0x00, 0x3e, 0x63, 0x38, 0x44, 0x44, 0x38, 0xcc, 0x78
  db	0x00, 0x00, 0x00, 0x00, 0x7e, 0x7e, 0x7e, 0x00, 0x18, 0x3c, 0x7e, 0x18, 0x7e, 0x3c, 0x18, 0xff
  db	0x10, 0x38, 0x7c, 0x54, 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x54, 0x7c, 0x38, 0x10, 0x00
  db	0x00, 0x18, 0x0c, 0xfe, 0x0c, 0x18, 0x00, 0x00, 0x00, 0x30, 0x60, 0xfe, 0x60, 0x30, 0x00, 0x00
  db	0x00, 0x00, 0x40, 0x40, 0x40, 0x7e, 0x00, 0x00, 0x00, 0x24, 0x66, 0xff, 0x66, 0x24, 0x00, 0x00
  db	0x00, 0x10, 0x38, 0x7c, 0xfe, 0xfe, 0x00, 0x00, 0x00, 0xfe, 0xfe, 0x7c, 0x38, 0x10, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x38, 0x10, 0x10, 0x00, 0x10, 0x00
  db	0x24, 0x24, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x24, 0x7e, 0x24, 0x7e, 0x24, 0x24, 0x00
  db	0x18, 0x3e, 0x40, 0x3c, 0x02, 0x7c, 0x18, 0x00, 0x00, 0x62, 0x64, 0x08, 0x10, 0x26, 0x46, 0x00
  db	0x30, 0x48, 0x30, 0x56, 0x88, 0x88, 0x76, 0x00, 0x10, 0x10, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x10, 0x20, 0x40, 0x40, 0x40, 0x20, 0x10, 0x00, 0x20, 0x10, 0x08, 0x08, 0x08, 0x10, 0x20, 0x00
  db	0x00, 0x44, 0x38, 0xfe, 0x38, 0x44, 0x00, 0x00, 0x00, 0x10, 0x10, 0x7c, 0x10, 0x10, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x20, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00
  db	0x3c, 0x42, 0x46, 0x4a, 0x52, 0x62, 0x3c, 0x00, 0x10, 0x30, 0x50, 0x10, 0x10, 0x10, 0x7c, 0x00
  db	0x3c, 0x42, 0x02, 0x0c, 0x30, 0x42, 0x7e, 0x00, 0x3c, 0x42, 0x02, 0x1c, 0x02, 0x42, 0x3c, 0x00
  db	0x08, 0x18, 0x28, 0x48, 0xfe, 0x08, 0x1c, 0x00, 0x7e, 0x40, 0x7c, 0x02, 0x02, 0x42, 0x3c, 0x00
  db	0x1c, 0x20, 0x40, 0x7c, 0x42, 0x42, 0x3c, 0x00, 0x7e, 0x42, 0x04, 0x08, 0x10, 0x10, 0x10, 0x00
  db	0x3c, 0x42, 0x42, 0x3c, 0x42, 0x42, 0x3c, 0x00, 0x3c, 0x42, 0x42, 0x3e, 0x02, 0x04, 0x38, 0x00
  db	0x00, 0x10, 0x10, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x10, 0x10, 0x00, 0x00, 0x10, 0x10, 0x20
  db	0x08, 0x10, 0x20, 0x40, 0x20, 0x10, 0x08, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x7e, 0x00, 0x00
  db	0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10, 0x00, 0x3c, 0x42, 0x02, 0x04, 0x08, 0x00, 0x08, 0x00
  db	0x3c, 0x42, 0x5e, 0x52, 0x5e, 0x40, 0x3c, 0x00, 0x18, 0x24, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x00
  db	0x7c, 0x22, 0x22, 0x3c, 0x22, 0x22, 0x7c, 0x00, 0x1c, 0x22, 0x40, 0x40, 0x40, 0x22, 0x1c, 0x00
  db	0x78, 0x24, 0x22, 0x22, 0x22, 0x24, 0x78, 0x00, 0x7e, 0x22, 0x28, 0x38, 0x28, 0x22, 0x7e, 0x00
  db	0x7e, 0x22, 0x28, 0x38, 0x28, 0x20, 0x70, 0x00, 0x1c, 0x22, 0x40, 0x40, 0x4e, 0x22, 0x1e, 0x00
  db	0x42, 0x42, 0x42, 0x7e, 0x42, 0x42, 0x42, 0x00, 0x38, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00
  db	0x0e, 0x04, 0x04, 0x04, 0x44, 0x44, 0x38, 0x00, 0x62, 0x24, 0x28, 0x30, 0x28, 0x24, 0x63, 0x00
  db	0x70, 0x20, 0x20, 0x20, 0x20, 0x22, 0x7e, 0x00, 0x63, 0x55, 0x49, 0x41, 0x41, 0x41, 0x41, 0x00
  db	0x62, 0x52, 0x4a, 0x46, 0x42, 0x42, 0x42, 0x00, 0x18, 0x24, 0x42, 0x42, 0x42, 0x24, 0x18, 0x00
  db	0x7c, 0x22, 0x22, 0x3c, 0x20, 0x20, 0x70, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x4a, 0x3c, 0x03, 0x00
  db	0x7c, 0x22, 0x22, 0x3c, 0x28, 0x24, 0x72, 0x00, 0x3c, 0x42, 0x40, 0x3c, 0x02, 0x42, 0x3c, 0x00
  db	0x7f, 0x49, 0x08, 0x08, 0x08, 0x08, 0x1c, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00
  db	0x41, 0x41, 0x41, 0x41, 0x22, 0x14, 0x08, 0x00, 0x41, 0x41, 0x41, 0x49, 0x49, 0x49, 0x36, 0x00
  db	0x41, 0x22, 0x14, 0x08, 0x14, 0x22, 0x41, 0x00, 0x41, 0x22, 0x14, 0x08, 0x08, 0x08, 0x1c, 0x00
  db	0x7f, 0x42, 0x04, 0x08, 0x10, 0x21, 0x7f, 0x00, 0x78, 0x40, 0x40, 0x40, 0x40, 0x40, 0x78, 0x00
  db	0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x78, 0x08, 0x08, 0x08, 0x08, 0x08, 0x78, 0x00
  db	0x10, 0x28, 0x44, 0x82, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff
  db	0x10, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x02, 0x3e, 0x42, 0x3f, 0x00
  db	0x60, 0x20, 0x20, 0x2e, 0x31, 0x31, 0x2e, 0x00, 0x00, 0x00, 0x3c, 0x42, 0x40, 0x42, 0x3c, 0x00
  db	0x06, 0x02, 0x02, 0x3a, 0x46, 0x46, 0x3b, 0x00, 0x00, 0x00, 0x3c, 0x42, 0x7e, 0x40, 0x3c, 0x00
  db	0x0c, 0x12, 0x10, 0x38, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x3d, 0x42, 0x42, 0x3e, 0x02, 0x7c
  db	0x60, 0x20, 0x2c, 0x32, 0x22, 0x22, 0x62, 0x00, 0x10, 0x00, 0x30, 0x10, 0x10, 0x10, 0x38, 0x00
  db	0x02, 0x00, 0x06, 0x02, 0x02, 0x42, 0x42, 0x3c, 0x60, 0x20, 0x24, 0x28, 0x30, 0x28, 0x26, 0x00
  db	0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00, 0x00, 0x00, 0x76, 0x49, 0x49, 0x49, 0x49, 0x00
  db	0x00, 0x00, 0x5c, 0x62, 0x42, 0x42, 0x42, 0x00, 0x00, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x3c, 0x00
  db	0x00, 0x00, 0x6c, 0x32, 0x32, 0x2c, 0x20, 0x70, 0x00, 0x00, 0x36, 0x4c, 0x4c, 0x34, 0x04, 0x0e
  db	0x00, 0x00, 0x6c, 0x32, 0x22, 0x20, 0x70, 0x00, 0x00, 0x00, 0x3e, 0x40, 0x3c, 0x02, 0x7c, 0x00
  db	0x10, 0x10, 0x7c, 0x10, 0x10, 0x12, 0x0c, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x46, 0x3a, 0x00
  db	0x00, 0x00, 0x41, 0x41, 0x22, 0x14, 0x08, 0x00, 0x00, 0x00, 0x41, 0x49, 0x49, 0x49, 0x36, 0x00
  db	0x00, 0x00, 0x44, 0x28, 0x10, 0x28, 0x44, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x3e, 0x02, 0x7c
  db	0x00, 0x00, 0x7c, 0x08, 0x10, 0x20, 0x7c, 0x00, 0x0c, 0x10, 0x10, 0x60, 0x10, 0x10, 0x0c, 0x00
  db	0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x00, 0x30, 0x08, 0x08, 0x06, 0x08, 0x08, 0x30, 0x00
  db	0x32, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x14, 0x22, 0x41, 0x41, 0x7f, 0x00
  db	0x3c, 0x42, 0x40, 0x42, 0x3c, 0x0c, 0x02, 0x3c, 0x00, 0x44, 0x00, 0x44, 0x44, 0x44, 0x3e, 0x00
  db	0x0c, 0x00, 0x3c, 0x42, 0x7e, 0x40, 0x3c, 0x00, 0x3c, 0x42, 0x38, 0x04, 0x3c, 0x44, 0x3e, 0x00
  db	0x42, 0x00, 0x38, 0x04, 0x3c, 0x44, 0x3e, 0x00, 0x30, 0x00, 0x38, 0x04, 0x3c, 0x44, 0x3e, 0x00
  db	0x10, 0x00, 0x38, 0x04, 0x3c, 0x44, 0x3e, 0x00, 0x00, 0x00, 0x3c, 0x40, 0x40, 0x3c, 0x06, 0x1c
  db	0x3c, 0x42, 0x3c, 0x42, 0x7e, 0x40, 0x3c, 0x00, 0x42, 0x00, 0x3c, 0x42, 0x7e, 0x40, 0x3c, 0x00
  db	0x30, 0x00, 0x3c, 0x42, 0x7e, 0x40, 0x3c, 0x00, 0x24, 0x00, 0x18, 0x08, 0x08, 0x08, 0x1c, 0x00
  db	0x7c, 0x82, 0x30, 0x10, 0x10, 0x10, 0x38, 0x00, 0x30, 0x00, 0x18, 0x08, 0x08, 0x08, 0x1c, 0x00
  db	0x42, 0x18, 0x24, 0x42, 0x7e, 0x42, 0x42, 0x00, 0x18, 0x18, 0x00, 0x3c, 0x42, 0x7e, 0x42, 0x00
  db	0x0c, 0x00, 0x7c, 0x20, 0x38, 0x20, 0x7c, 0x00, 0x00, 0x00, 0x33, 0x0c, 0x3f, 0x44, 0x3b, 0x00
  db	0x1f, 0x24, 0x44, 0x7f, 0x44, 0x44, 0x47, 0x00, 0x18, 0x24, 0x00, 0x3c, 0x42, 0x42, 0x3c, 0x00
  db	0x00, 0x42, 0x00, 0x3c, 0x42, 0x42, 0x3c, 0x00, 0x20, 0x10, 0x00, 0x3c, 0x42, 0x42, 0x3c, 0x00
  db	0x18, 0x24, 0x00, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x20, 0x10, 0x00, 0x42, 0x42, 0x42, 0x3c, 0x00
  db	0x00, 0x42, 0x00, 0x42, 0x42, 0x3e, 0x02, 0x3c, 0x42, 0x18, 0x24, 0x42, 0x42, 0x24, 0x18, 0x00
  db	0x42, 0x00, 0x42, 0x42, 0x42, 0x42, 0x3c, 0x00, 0x08, 0x08, 0x3e, 0x40, 0x40, 0x3e, 0x08, 0x08
  db	0x18, 0x24, 0x20, 0x70, 0x20, 0x42, 0x7c, 0x00, 0x44, 0x28, 0x7c, 0x10, 0x7c, 0x10, 0x10, 0x00
  db	0xf8, 0x4c, 0x78, 0x44, 0x4f, 0x44, 0x45, 0xe6, 0x1c, 0x12, 0x10, 0x7c, 0x10, 0x10, 0x90, 0x60
  db	0x0c, 0x00, 0x38, 0x04, 0x3c, 0x44, 0x3e, 0x00, 0x0c, 0x00, 0x18, 0x08, 0x08, 0x08, 0x1c, 0x00
  db	0x04, 0x08, 0x00, 0x3c, 0x42, 0x42, 0x3c, 0x00, 0x00, 0x04, 0x08, 0x42, 0x42, 0x42, 0x3c, 0x00
  db	0x32, 0x4c, 0x00, 0x7c, 0x42, 0x42, 0x42, 0x00, 0x34, 0x4c, 0x00, 0x62, 0x52, 0x4a, 0x46, 0x00
  db	0x3c, 0x44, 0x44, 0x3e, 0x00, 0x7e, 0x00, 0x00, 0x38, 0x44, 0x44, 0x38, 0x00, 0x7c, 0x00, 0x00
  db	0x10, 0x00, 0x10, 0x20, 0x40, 0x42, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x40, 0x40, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x7e, 0x02, 0x02, 0x00, 0x00, 0x42, 0xc4, 0x48, 0xf6, 0x29, 0x43, 0x8c, 0x1f
  db	0x42, 0xc4, 0x4a, 0xf6, 0x2a, 0x5f, 0x82, 0x02, 0x00, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 0x00
  db	0x00, 0x12, 0x24, 0x48, 0x24, 0x12, 0x00, 0x00, 0x00, 0x48, 0x24, 0x12, 0x24, 0x48, 0x00, 0x00
  db	0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x22, 0x88, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa
  db	0xdb, 0x77, 0xdb, 0xee, 0xdb, 0x77, 0xdb, 0xee, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10
  db	0x10, 0x10, 0x10, 0x10, 0xf0, 0x10, 0x10, 0x10, 0x10, 0x10, 0xf0, 0x10, 0xf0, 0x10, 0x10, 0x10
  db	0x14, 0x14, 0x14, 0x14, 0xf4, 0x14, 0x14, 0x14, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x14, 0x14, 0x14
  db	0x00, 0x00, 0xf0, 0x10, 0xf0, 0x10, 0x10, 0x10, 0x14, 0x14, 0xf4, 0x04, 0xf4, 0x14, 0x14, 0x14
  db	0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x00, 0xfc, 0x04, 0xf4, 0x14, 0x14, 0x14
  db	0x14, 0x14, 0xf4, 0x04, 0xfc, 0x00, 0x00, 0x00, 0x14, 0x14, 0x14, 0x14, 0xfc, 0x00, 0x00, 0x00
  db	0x10, 0x10, 0xf0, 0x10, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf0, 0x10, 0x10, 0x10
  db	0x10, 0x10, 0x10, 0x10, 0x1f, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0xff, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0xff, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f, 0x10, 0x10, 0x10
  db	0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0xff, 0x10, 0x10, 0x10
  db	0x10, 0x10, 0x1f, 0x10, 0x1f, 0x10, 0x10, 0x10, 0x14, 0x14, 0x14, 0x14, 0x17, 0x14, 0x14, 0x14
  db	0x14, 0x14, 0x17, 0x10, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x10, 0x17, 0x14, 0x14, 0x14
  db	0x14, 0x14, 0xf7, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf7, 0x14, 0x14, 0x14
  db	0x14, 0x14, 0x17, 0x10, 0x17, 0x14, 0x14, 0x14, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00
  db	0x14, 0x14, 0xf7, 0x00, 0xf7, 0x14, 0x14, 0x14, 0x10, 0x10, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00
  db	0x14, 0x14, 0x14, 0x14, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0x10, 0x10, 0x10
  db	0x00, 0x00, 0x00, 0x00, 0xff, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x1f, 0x00, 0x00, 0x00
  db	0x10, 0x10, 0x1f, 0x10, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x10, 0x1f, 0x10, 0x10, 0x10
  db	0x00, 0x00, 0x00, 0x00, 0x1f, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0xff, 0x14, 0x14, 0x14
  db	0x10, 0x10, 0xff, 0x10, 0xff, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0xf0, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x1f, 0x10, 0x10, 0x10, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
  db	0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0
  db	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x31, 0x4a, 0x44, 0x4a, 0x31, 0x00, 0x00, 0x3c, 0x42, 0x7c, 0x42, 0x7c, 0x40, 0x40
  db	0x00, 0x7e, 0x42, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x3f, 0x54, 0x14, 0x14, 0x14, 0x14, 0x00
  db	0x7e, 0x42, 0x20, 0x18, 0x20, 0x42, 0x7e, 0x00, 0x00, 0x00, 0x3e, 0x48, 0x48, 0x48, 0x30, 0x00
  db	0x00, 0x44, 0x44, 0x44, 0x7a, 0x40, 0x40, 0x80, 0x00, 0x33, 0x4c, 0x08, 0x08, 0x08, 0x08, 0x00
  db	0x7c, 0x10, 0x38, 0x44, 0x44, 0x38, 0x10, 0x7c, 0x18, 0x24, 0x42, 0x7e, 0x42, 0x24, 0x18, 0x00
  db	0x18, 0x24, 0x42, 0x42, 0x24, 0x24, 0x66, 0x00, 0x1c, 0x20, 0x18, 0x3c, 0x42, 0x42, 0x3c, 0x00
  db	0x00, 0x62, 0x95, 0x89, 0x95, 0x62, 0x00, 0x00, 0x02, 0x04, 0x3c, 0x4a, 0x52, 0x3c, 0x40, 0x80
  db	0x0c, 0x10, 0x20, 0x3c, 0x20, 0x10, 0x0c, 0x00, 0x3c, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x00
  db	0x00, 0x7e, 0x00, 0x7e, 0x00, 0x7e, 0x00, 0x00, 0x10, 0x10, 0x7c, 0x10, 0x10, 0x00, 0x7c, 0x00
  db	0x10, 0x08, 0x04, 0x08, 0x10, 0x00, 0x7e, 0x00, 0x08, 0x10, 0x20, 0x10, 0x08, 0x00, 0x7e, 0x00
  db	0x0c, 0x12, 0x12, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x90, 0x90, 0x60
  db	0x18, 0x18, 0x00, 0x7e, 0x00, 0x18, 0x18, 0x00, 0x00, 0x32, 0x4c, 0x00, 0x32, 0x4c, 0x00, 0x00
  db	0x30, 0x48, 0x48, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x0f, 0x08, 0x08, 0x08, 0x08, 0xc8, 0x28, 0x18
  db	0x78, 0x44, 0x44, 0x44, 0x44, 0x00, 0x00, 0x00, 0x30, 0x48, 0x10, 0x20, 0x78, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x3c, 0x3c, 0x3c, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

vga_glyphs:
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x7e, 0x81, 0xa5, 0x81, 0x81, 0xbd, 0x99, 0x81, 0x81, 0x7e, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x7e, 0xff, 0xdb, 0xff, 0xff, 0xc3, 0xe7, 0xff, 0xff, 0x7e, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x6c, 0xfe, 0xfe, 0xfe, 0xfe, 0x7c, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x7c, 0xfe, 0x7c, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x18, 0x3c, 0x3c, 0xe7, 0xe7, 0xe7, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x18, 0x3c, 0x7e, 0xff, 0xff, 0x7e, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x3c, 0x3c, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xe7, 0xc3, 0xc3, 0xe7, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x3c, 0x66, 0x42, 0x42, 0x66, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0xff, 0xff, 0xff, 0xff, 0xff, 0xc3, 0x99, 0xbd, 0xbd, 0x99, 0xc3, 0xff, 0xff, 0xff, 0xff, 0xff
  db	0x00, 0x00, 0x1e, 0x0e, 0x1a, 0x32, 0x78, 0xcc, 0xcc, 0xcc, 0xcc, 0x78, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x3c, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x18, 0x7e, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x3f, 0x33, 0x3f, 0x30, 0x30, 0x30, 0x30, 0x70, 0xf0, 0xe0, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x7f, 0x63, 0x7f, 0x63, 0x63, 0x63, 0x63, 0x67, 0xe7, 0xe6, 0xc0, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x18, 0x18, 0xdb, 0x3c, 0xe7, 0x3c, 0xdb, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x80, 0xc0, 0xe0, 0xf0, 0xf8, 0xfe, 0xf8, 0xf0, 0xe0, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x02, 0x06, 0x0e, 0x1e, 0x3e, 0xfe, 0x3e, 0x1e, 0x0e, 0x06, 0x02, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x18, 0x3c, 0x7e, 0x18, 0x18, 0x18, 0x7e, 0x3c, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x7f, 0xdb, 0xdb, 0xdb, 0x7b, 0x1b, 0x1b, 0x1b, 0x1b, 0x1b, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x7c, 0xc6, 0x60, 0x38, 0x6c, 0xc6, 0xc6, 0x6c, 0x38, 0x0c, 0xc6, 0x7c, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xfe, 0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x18, 0x3c, 0x7e, 0x18, 0x18, 0x18, 0x7e, 0x3c, 0x18, 0x7e, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x18, 0x3c, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x3c, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x0c, 0xfe, 0x0c, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x60, 0xfe, 0x60, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xc0, 0xc0, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x66, 0xff, 0x66, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x38, 0x7c, 0x7c, 0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0xfe, 0xfe, 0x7c, 0x7c, 0x38, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x18, 0x3c, 0x3c, 0x3c, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x66, 0x66, 0x66, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x6c, 0x6c, 0xfe, 0x6c, 0x6c, 0x6c, 0xfe, 0x6c, 0x6c, 0x00, 0x00, 0x00, 0x00
  db	0x18, 0x18, 0x7c, 0xc6, 0xc2, 0xc0, 0x7c, 0x06, 0x06, 0x86, 0xc6, 0x7c, 0x18, 0x18, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0xc2, 0xc6, 0x0c, 0x18, 0x30, 0x60, 0xc6, 0x86, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x38, 0x6c, 0x6c, 0x38, 0x76, 0xdc, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x30, 0x30, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x0c, 0x18, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x18, 0x0c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x66, 0x3c, 0xff, 0x3c, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x7e, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x02, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x80, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x3c, 0x66, 0xc3, 0xc3, 0xdb, 0xdb, 0xc3, 0xc3, 0x66, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x18, 0x38, 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x7c, 0xc6, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0xc6, 0xfe, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x7c, 0xc6, 0x06, 0x06, 0x3c, 0x06, 0x06, 0x06, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x0c, 0x1c, 0x3c, 0x6c, 0xcc, 0xfe, 0x0c, 0x0c, 0x0c, 0x1e, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xfe, 0xc0, 0xc0, 0xc0, 0xfc, 0x06, 0x06, 0x06, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x38, 0x60, 0xc0, 0xc0, 0xfc, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xfe, 0xc6, 0x06, 0x06, 0x0c, 0x18, 0x30, 0x30, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0x7e, 0x06, 0x06, 0x06, 0x0c, 0x78, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x7c, 0xc6, 0xc6, 0x0c, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xde, 0xde, 0xde, 0xdc, 0xc0, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x10, 0x38, 0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xfc, 0x66, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x66, 0x66, 0xfc, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x3c, 0x66, 0xc2, 0xc0, 0xc0, 0xc0, 0xc0, 0xc2, 0x66, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xf8, 0x6c, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x6c, 0xf8, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xfe, 0x66, 0x62, 0x68, 0x78, 0x68, 0x60, 0x62, 0x66, 0xfe, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xfe, 0x66, 0x62, 0x68, 0x78, 0x68, 0x60, 0x60, 0x60, 0xf0, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x3c, 0x66, 0xc2, 0xc0, 0xc0, 0xde, 0xc6, 0xc6, 0x66, 0x3a, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xc6, 0xc6, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x1e, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0xcc, 0xcc, 0xcc, 0x78, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xe6, 0x66, 0x66, 0x6c, 0x78, 0x78, 0x6c, 0x66, 0x66, 0xe6, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xf0, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x62, 0x66, 0xfe, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xc3, 0xe7, 0xff, 0xff, 0xdb, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xc6, 0xe6, 0xf6, 0xfe, 0xde, 0xce, 0xc6, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xfc, 0x66, 0x66, 0x66, 0x7c, 0x60, 0x60, 0x60, 0x60, 0xf0, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xd6, 0xde, 0x7c, 0x0c, 0x0e, 0x00, 0x00
  db	0x00, 0x00, 0xfc, 0x66, 0x66, 0x66, 0x7c, 0x6c, 0x66, 0x66, 0x66, 0xe6, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x7c, 0xc6, 0xc6, 0x60, 0x38, 0x0c, 0x06, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xff, 0xdb, 0x99, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0x66, 0x3c, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xdb, 0xdb, 0xff, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xc3, 0xc3, 0x66, 0x3c, 0x18, 0x18, 0x3c, 0x66, 0xc3, 0xc3, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xc3, 0xc3, 0xc3, 0x66, 0x3c, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xff, 0xc3, 0x86, 0x0c, 0x18, 0x30, 0x60, 0xc1, 0xc3, 0xff, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x3c, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x80, 0xc0, 0xe0, 0x70, 0x38, 0x1c, 0x0e, 0x06, 0x02, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x3c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x10, 0x38, 0x6c, 0xc6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00
  db	0x30, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x0c, 0x7c, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xe0, 0x60, 0x60, 0x78, 0x6c, 0x66, 0x66, 0x66, 0x66, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xc6, 0xc0, 0xc0, 0xc0, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x1c, 0x0c, 0x0c, 0x3c, 0x6c, 0xcc, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xc6, 0xfe, 0xc0, 0xc0, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x38, 0x6c, 0x64, 0x60, 0xf0, 0x60, 0x60, 0x60, 0x60, 0xf0, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x7c, 0x0c, 0xcc, 0x78, 0x00
  db	0x00, 0x00, 0xe0, 0x60, 0x60, 0x6c, 0x76, 0x66, 0x66, 0x66, 0x66, 0xe6, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x18, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x06, 0x06, 0x00, 0x0e, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x66, 0x66, 0x3c, 0x00
  db	0x00, 0x00, 0xe0, 0x60, 0x60, 0x66, 0x6c, 0x78, 0x78, 0x6c, 0x66, 0xe6, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xe6, 0xff, 0xdb, 0xdb, 0xdb, 0xdb, 0xdb, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xdc, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xdc, 0x66, 0x66, 0x66, 0x66, 0x66, 0x7c, 0x60, 0x60, 0xf0, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x7c, 0x0c, 0x0c, 0x1e, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xdc, 0x76, 0x66, 0x60, 0x60, 0x60, 0xf0, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0xc6, 0x60, 0x38, 0x0c, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x10, 0x30, 0x30, 0xfc, 0x30, 0x30, 0x30, 0x30, 0x36, 0x1c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0xc3, 0xc3, 0xc3, 0x66, 0x3c, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0xc3, 0xc3, 0xdb, 0xdb, 0xff, 0x66, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xc3, 0x66, 0x3c, 0x18, 0x3c, 0x66, 0xc3, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7e, 0x06, 0x0c, 0xf8, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xcc, 0x18, 0x30, 0x60, 0xc6, 0xfe, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x0e, 0x18, 0x18, 0x18, 0x70, 0x18, 0x18, 0x18, 0x18, 0x0e, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x70, 0x18, 0x18, 0x18, 0x0e, 0x18, 0x18, 0x18, 0x18, 0x70, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x76, 0xdc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x10, 0x38, 0x6c, 0xc6, 0xc6, 0xc6, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x3c, 0x66, 0xc2, 0xc0, 0xc0, 0xc0, 0xc2, 0x66, 0x3c, 0x0c, 0x06, 0x7c, 0x00, 0x00
  db	0x00, 0x00, 0xcc, 0x00, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x0c, 0x18, 0x30, 0x00, 0x7c, 0xc6, 0xfe, 0xc0, 0xc0, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x10, 0x38, 0x6c, 0x00, 0x78, 0x0c, 0x7c, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xcc, 0x00, 0x00, 0x78, 0x0c, 0x7c, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x60, 0x30, 0x18, 0x00, 0x78, 0x0c, 0x7c, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x38, 0x6c, 0x38, 0x00, 0x78, 0x0c, 0x7c, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x3c, 0x66, 0x60, 0x60, 0x66, 0x3c, 0x0c, 0x06, 0x3c, 0x00, 0x00, 0x00
  db	0x00, 0x10, 0x38, 0x6c, 0x00, 0x7c, 0xc6, 0xfe, 0xc0, 0xc0, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xc6, 0x00, 0x00, 0x7c, 0xc6, 0xfe, 0xc0, 0xc0, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x60, 0x30, 0x18, 0x00, 0x7c, 0xc6, 0xfe, 0xc0, 0xc0, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x66, 0x00, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x18, 0x3c, 0x66, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x60, 0x30, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0xc6, 0x00, 0x10, 0x38, 0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00
  db	0x38, 0x6c, 0x38, 0x00, 0x38, 0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00
  db	0x18, 0x30, 0x60, 0x00, 0xfe, 0x66, 0x60, 0x7c, 0x60, 0x60, 0x66, 0xfe, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x6e, 0x3b, 0x1b, 0x7e, 0xd8, 0xdc, 0x77, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x3e, 0x6c, 0xcc, 0xcc, 0xfe, 0xcc, 0xcc, 0xcc, 0xcc, 0xce, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x10, 0x38, 0x6c, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xc6, 0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x60, 0x30, 0x18, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x30, 0x78, 0xcc, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x60, 0x30, 0x18, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xc6, 0x00, 0x00, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7e, 0x06, 0x0c, 0x78, 0x00
  db	0x00, 0xc6, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0xc6, 0x00, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x18, 0x18, 0x7e, 0xc3, 0xc0, 0xc0, 0xc0, 0xc3, 0x7e, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x38, 0x6c, 0x64, 0x60, 0xf0, 0x60, 0x60, 0x60, 0x60, 0xe6, 0xfc, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xc3, 0x66, 0x3c, 0x18, 0xff, 0x18, 0xff, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0xfc, 0x66, 0x66, 0x7c, 0x62, 0x66, 0x6f, 0x66, 0x66, 0x66, 0xf3, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x0e, 0x1b, 0x18, 0x18, 0x18, 0x7e, 0x18, 0x18, 0x18, 0x18, 0x18, 0xd8, 0x70, 0x00, 0x00
  db	0x00, 0x18, 0x30, 0x60, 0x00, 0x78, 0x0c, 0x7c, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x0c, 0x18, 0x30, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x18, 0x30, 0x60, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x18, 0x30, 0x60, 0x00, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x76, 0xdc, 0x00, 0xdc, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00
  db	0x76, 0xdc, 0x00, 0xc6, 0xe6, 0xf6, 0xfe, 0xde, 0xce, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x3c, 0x6c, 0x6c, 0x3e, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x38, 0x6c, 0x6c, 0x38, 0x00, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x30, 0x30, 0x00, 0x30, 0x30, 0x60, 0xc0, 0xc6, 0xc6, 0x7c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0xc0, 0xc0, 0xc0, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x06, 0x06, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0xc0, 0xc0, 0xc2, 0xc6, 0xcc, 0x18, 0x30, 0x60, 0xce, 0x9b, 0x06, 0x0c, 0x1f, 0x00, 0x00
  db	0x00, 0xc0, 0xc0, 0xc2, 0xc6, 0xcc, 0x18, 0x30, 0x66, 0xce, 0x96, 0x3e, 0x06, 0x06, 0x00, 0x00
  db	0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x3c, 0x3c, 0x3c, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x36, 0x6c, 0xd8, 0x6c, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xd8, 0x6c, 0x36, 0x6c, 0xd8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x11, 0x44, 0x11, 0x44, 0x11, 0x44, 0x11, 0x44, 0x11, 0x44, 0x11, 0x44, 0x11, 0x44, 0x11, 0x44
  db	0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa, 0x55, 0xaa
  db	0xdd, 0x77, 0xdd, 0x77, 0xdd, 0x77, 0xdd, 0x77, 0xdd, 0x77, 0xdd, 0x77, 0xdd, 0x77, 0xdd, 0x77
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xf8, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0xf8, 0x18, 0xf8, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0xf6, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x18, 0xf8, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0xf6, 0x06, 0xf6, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x06, 0xf6, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0xf6, 0x06, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0xf8, 0x18, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf8, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1f, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0x1f, 0x18, 0x1f, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x37, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0x37, 0x30, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x30, 0x37, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0xf7, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xf7, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0x37, 0x30, 0x37, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0xf7, 0x00, 0xf7, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0x1f, 0x18, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x18, 0x1f, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0xff, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0xff, 0x18, 0xff, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
  db	0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0, 0xf0
  db	0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f, 0x0f
  db	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xdc, 0xd8, 0xd8, 0xd8, 0xdc, 0x76, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x78, 0xcc, 0xcc, 0xcc, 0xd8, 0xcc, 0xc6, 0xc6, 0xc6, 0xcc, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0xfe, 0xc6, 0xc6, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0xfe, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0xfe, 0xc6, 0x60, 0x30, 0x18, 0x30, 0x60, 0xc6, 0xfe, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0xd8, 0xd8, 0xd8, 0xd8, 0xd8, 0x70, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x7c, 0x60, 0x60, 0xc0, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x76, 0xdc, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x7e, 0x18, 0x3c, 0x66, 0x66, 0x66, 0x3c, 0x18, 0x7e, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x38, 0x6c, 0xc6, 0xc6, 0xfe, 0xc6, 0xc6, 0x6c, 0x38, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x38, 0x6c, 0xc6, 0xc6, 0xc6, 0x6c, 0x6c, 0x6c, 0x6c, 0xee, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x1e, 0x30, 0x18, 0x0c, 0x3e, 0x66, 0x66, 0x66, 0x66, 0x3c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x7e, 0xdb, 0xdb, 0xdb, 0x7e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x03, 0x06, 0x7e, 0xdb, 0xdb, 0xf3, 0x7e, 0x60, 0xc0, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x1c, 0x30, 0x60, 0x60, 0x7c, 0x60, 0x60, 0x60, 0x30, 0x1c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x7c, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0xc6, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x00, 0xfe, 0x00, 0x00, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x7e, 0x18, 0x18, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x30, 0x18, 0x0c, 0x06, 0x0c, 0x18, 0x30, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x0c, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0c, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x0e, 0x1b, 0x1b, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18
  db	0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0xd8, 0xd8, 0xd8, 0x70, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x7e, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xdc, 0x00, 0x76, 0xdc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x38, 0x6c, 0x6c, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x0f, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0xec, 0x6c, 0x6c, 0x3c, 0x1c, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0xd8, 0x6c, 0x6c, 0x6c, 0x6c, 0x6c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x70, 0xd8, 0x30, 0x60, 0xc8, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x7c, 0x7c, 0x7c, 0x7c, 0x7c, 0x7c, 0x7c, 0x00, 0x00, 0x00, 0x00, 0x00
  db	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00

; ROM BIOS compatability entry points:
; ===================================
;   $e05b ; POST Entry Point
;   $e2c3 ; NMI Handler Entry Point
;   $e3fe ; INT 13h Fixed Disk Services Entry Point
;   $e401 ; Fixed Disk Parameter Table
;   $e6f2 ; INT 19h Boot Load Service Entry Point
;   $e6f5 ; Configuration Data Table
;   $e729 ; Baud Rate Generator Table
;   $e739 ; INT 14h Serial Communications Service Entry Point
;   $e82e ; INT 16h Keyboard Service Entry Point
; * $e987 ; INT 09h Keyboard Service Entry Point
;   $ec59 ; INT 13h Diskette Service Entry Point
;   $ef57 ; INT 0Eh Diskette Hardware ISR Entry Point
;   $efc7 ; Diskette Controller Parameter Table
;   $efd2 ; INT 17h Printer Service Entry Point
;   $f045 ; INT 10 Functions 0-Fh Entry Point
;   $f065 ; INT 10h Video Support Service Entry Point
;   $f0a4 ; MDA/CGA Video Parameter Table (INT 1Dh)
;   $f841 ; INT 12h Memory Size Service Entry Point
;   $f84d ; INT 11h Equipment List Service Entry Point
;   $f859 ; INT 15h System Services Entry Point
; * $fa6e ; Character Font for 320x200 & 640x200 Graphics (lower 128 characters)
;   $fe6e ; INT 1Ah Time-of-day Service Entry Point
;   $fea5 ; INT 08h System Timer ISR Entry Point
;   $fef3 ; Initial Interrupt Vector Offsets Loaded by POST
;   $ff53 ; IRET Instruction for Dummy Interrupt Handler
;   $ff54 ; INT 05h Print Screen Service Entry Point
; * $fff0 ; Power-up Entry Point
; * $fff5 ; ASCII Date ROM was built - 8 characters in MM/DD/YY
; * $fffe ; System Model ID

; In a 100% compatible BIOS this is where the int09 (keyboard) handler lives.
	absolute 0xe987
int09_bios_redirect:
	resb (int09_redirect_end - int09_redirect_start)

; In a 100% compatible bios this is where the CGA ROM appears.
; The first 128 characters of the above table gets copied here when the BIOS starts.
	absolute 0xfa6e
glyphs8x8:
	resb 1024


; This is the format of the 36-byte tm structure, returned by the emulator's RTC query call

timetable:

tm_sec		equ $
tm_min		equ $+4
tm_hour		equ $+8
tm_mday		equ $+12
tm_mon		equ $+16
tm_year		equ $+20
tm_wday		equ $+24
tm_yday		equ $+28
tm_dst		equ $+32
tm_msec		equ $+36

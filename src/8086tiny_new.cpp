// =============================================================================
// File: 8086tiny_new.cpp
//
// Description:
// 8086tiny plus Revision 1.34
//
// Modified from 8086tiny to separate hardware emulation into an interface
// class and support more 80186/NEC V20 instructions.
// Copyright 2014 Julian Olds
//
// Based on:
// 8086tiny: a tiny, highly functional, highly portable PC emulator/VM
// Copyright 2013-14, Adrian Cable (adrian.cable@gmail.com) - http://www.megalith.co.uk/8086tiny
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#include <time.h>
#include <memory.h>
#include <stdio.h>

#ifndef _WIN32
#include <unistd.h>
#include <fcntl.h>
#include <io.h>
#else
#include <windows.h>
#include <sys/timeb.h>
#include <fcntl.h>
#include <conio.h>
#ifdef _MSC_VER
#include <io.h>
#endif
#endif

#include "8086tiny_interface.h"

T8086TinyInterface_t Interface;

// Emulator system constants

#define IO_PORT_COUNT 0x10000
#define RAM_SIZE 0x10FFF0
#define REGS_BASE 0xF0000

// 16-bit register decodes

#define REG_AX 0
#define REG_CX 1
#define REG_DX 2
#define REG_BX 3
#define REG_SP 4
#define REG_BP 5
#define REG_SI 6
#define REG_DI 7

#define REG_ES 8
#define REG_CS 9
#define REG_SS 10
#define REG_DS 11

#define REG_ZERO 12
#define REG_SCRATCH 13

#define REG_IP 14
#define REG_TMP 15

// 8-bit register decodes
#define REG_AL 0
#define REG_AH 1
#define REG_CL 2
#define REG_CH 3
#define REG_DL 4
#define REG_DH 5
#define REG_BL 6
#define REG_BH 7

// FLAGS register decodes
#define FLAG_CF 40
#define FLAG_PF 41
#define FLAG_AF 42
#define FLAG_ZF 43
#define FLAG_SF 44
#define FLAG_TF 45
#define FLAG_IF 46
#define FLAG_DF 47
#define FLAG_OF 48

// Lookup tables in the BIOS binary
#define TABLE_XLAT_OPCODE 8
#define TABLE_XLAT_SUBFUNCTION 9
#define TABLE_STD_FLAGS 10
#define TABLE_PARITY_FLAG 11
#define TABLE_BASE_INST_SIZE 12
#define TABLE_I_W_SIZE 13
#define TABLE_I_MOD_SIZE 14
#define TABLE_COND_JUMP_DECODE_A 15
#define TABLE_COND_JUMP_DECODE_B 16
#define TABLE_COND_JUMP_DECODE_C 17
#define TABLE_COND_JUMP_DECODE_D 18
#define TABLE_FLAGS_BITFIELDS 19

// Bitfields for TABLE_STD_FLAGS values
#define FLAGS_UPDATE_SZP 1
#define FLAGS_UPDATE_AO_ARITH 2
#define FLAGS_UPDATE_OC_LOGIC 4

// Helper macros

// Decode mod, r_m and reg fields in instruction
#define DECODE_RM_REG scratch2_uint = 4 * !i_mod, \
					  op_to_addr = rm_addr = i_mod < 3 ? SEGREG_OP(seg_override_en ? seg_override : bios_table_lookup[scratch2_uint + 3][i_rm], bios_table_lookup[scratch2_uint][i_rm], regs16[bios_table_lookup[scratch2_uint + 1][i_rm]] + bios_table_lookup[scratch2_uint + 2][i_rm] * i_data1+) : GET_REG_ADDR(i_rm), \
					  op_from_addr = GET_REG_ADDR(i_reg), \
					  i_d && (scratch_uint = op_from_addr, op_from_addr = rm_addr, op_to_addr = scratch_uint)

// Return memory-mapped register location (offset into mem array) for register #reg_id
#define GET_REG_ADDR(reg_id) (REGS_BASE + (i_w ? 2 * reg_id : (2 * reg_id + reg_id / 4) & 7))

// Returns number of top bit in operand (i.e. 8 for 8-bit operands, 16 for 16-bit operands)
#define TOP_BIT 8*(i_w + 1)

// Opcode execution unit helpers
#define OPCODE ;break; case
#define OPCODE_CHAIN ; case

// [I]MUL/[I]DIV/DAA/DAS/ADC/SBB helpers
#define MUL_MACRO(op_data_type,out_regs) (set_opcode(0x10), \
										  out_regs[i_w + 1] = (op_result = CAST(op_data_type)mem[rm_addr] * (op_data_type)*out_regs) >> 16, \
										  regs16[REG_AX] = op_result, \
										  set_OF(set_CF(op_result - (op_data_type)op_result)))
#define DIV_MACRO(out_data_type,in_data_type,out_regs) (scratch_int = CAST(out_data_type)mem[rm_addr]) && !(scratch2_uint = (in_data_type)(scratch_uint = (out_regs[i_w+1] << 16) + regs16[REG_AX]) / scratch_int, scratch2_uint - (out_data_type)scratch2_uint) ? out_regs[i_w+1] = scratch_uint - scratch_int * (*out_regs = scratch2_uint) : pc_interrupt(0)
#define DAA_DAS(op1,op2) \
                  set_AF((((scratch_uchar = regs8[REG_AL]) & 0x0F) > 9) || regs8[FLAG_AF]) && (op_result = (regs8[REG_AL] op1 6), set_CF(regs8[FLAG_CF] || (regs8[REG_AL] op2 scratch_uchar))), \
								  set_CF((regs8[REG_AL] > 0x9f) || regs8[FLAG_CF]) && (op_result = (regs8[REG_AL] op1 0x60))
#define ADC_SBB_MACRO(a) OP(a##= regs8[FLAG_CF] +), \
						 set_CF((regs8[FLAG_CF] && (op_result == op_dest)) || (a op_result < a(int)op_dest)), \
						 set_AF_OF_arith()

// Execute arithmetic/logic operations in emulator memory/registers
#define R_M_OP(dest,op,src) (i_w ? op_dest = CAST(unsigned short)dest, op_result = CAST(unsigned short)dest op (op_source = CAST(unsigned short)src) \
								 : (op_dest = dest, op_result = dest op (op_source = CAST(unsigned char)src)))

// Execute a memory move with no other operation on dest
#define R_M_MOV(dest,src) (i_w ? op_dest = CAST(unsigned short)dest, op_result = CAST(unsigned short)dest = (op_source = CAST(unsigned short)src) \
								 : (op_dest = dest, op_result = dest = (op_source = CAST(unsigned char)src)))

#ifdef INTERCEPT_VMEM

static unsigned int vmem_src, vmem_dest;

#define MEM_OP(dest,op,src) (IS_VMEM(src) ? \
                                ( (vmem_src = Interface.VMemRead(i_w, src)), \
                                  (IS_VMEM(dest) ? \
                                     (vmem_dest = Interface.VMemRead(i_w, dest), R_M_OP(vmem_dest, op, vmem_src), Interface.VMemWrite(i_w, dest, vmem_dest)) : \
                                     R_M_OP(mem[dest], op, vmem_src)) ) : \
                                (IS_VMEM(dest) ? \
                                   (vmem_dest = Interface.VMemRead(i_w, dest), R_M_OP(vmem_dest, op, mem[src]), Interface.VMemWrite(i_w, dest, vmem_dest)) : \
                                   R_M_OP(mem[dest], op, mem[src])))

#define MEM_MOV(dest,src) (IS_VMEM(src) ? \
                             ( (vmem_src = Interface.VMemRead(i_w, src)), \
                               (IS_VMEM(dest) ? \
                                  (R_M_MOV(vmem_dest, vmem_src), Interface.VMemWrite(i_w, dest, vmem_dest)) : \
                                  R_M_MOV(mem[dest], vmem_src)) ) : \
                             (IS_VMEM(dest) ? \
                                (R_M_MOV(vmem_dest, mem[src]), Interface.VMemWrite(i_w, dest, vmem_dest)) : \
                                R_M_MOV(mem[dest], mem[src])))


#else

#define MEM_OP(dest,op,src) R_M_OP(mem[dest],op,mem[src])
#define MEM_MOV(dest, src) R_M_MOV(mem[dest],mem[src])

#endif

#define OP(op) MEM_OP(op_to_addr,op,op_from_addr)

// Increment or decrement a register #reg_id (usually SI or DI), depending on direction flag and operand size (given by i_w)
#define INDEX_INC(reg_id) (regs16[reg_id] -= (2 * regs8[FLAG_DF] - 1)*(i_w + 1))

// Helpers for stack operations
#define R_M_PUSH(a) (i_w = 1, R_M_OP(mem[SEGREG_OP(REG_SS, REG_SP, --)], =, a))
#define R_M_POP(a) (i_w = 1, regs16[REG_SP] += 2, R_M_OP(a, =, mem[SEGREG_OP(REG_SS, REG_SP, -2+)]))

// Convert segment:offset to linear address in emulator memory space
#define SEGREG(reg_seg,reg_ofs) 16 * regs16[reg_seg] + (unsigned short)(regs16[reg_ofs])
#define SEGREG_OP(reg_seg,reg_ofs,op) 16 * regs16[reg_seg] + (unsigned short)(op regs16[reg_ofs])

// Returns sign bit of an 8-bit or 16-bit operand
#define SIGN_OF(a) (1 & (i_w ? CAST(short)a : a) >> (TOP_BIT - 1))

// Reinterpretation cast
#define CAST(a) *(a*)&

// Global variable definitions
unsigned char mem[RAM_SIZE], io_ports[IO_PORT_COUNT], *opcode_stream, *regs8, i_rm, i_w, i_reg, i_mod, i_mod_size, i_d, i_reg4bit, raw_opcode_id, xlat_opcode_id, extra, rep_mode, seg_override_en, rep_override_en, trap_flag, scratch_uchar;
unsigned char  bios_table_lookup[20][256];
unsigned short *regs16, reg_ip, seg_override, file_index;
unsigned int op_source, op_dest, rm_addr, op_to_addr, op_from_addr, i_data0, i_data1, i_data2, scratch_uint, scratch2_uint, set_flags_type;
int i_data1r, op_result, disk[3], scratch_int;
time_t clock_buf;
struct timeb ms_clock;

// Helper functions

// Set carry flag
char set_CF(int new_CF)
{
	return regs8[FLAG_CF] = !!new_CF;
}

// Set auxiliary flag
char set_AF(int new_AF)
{
	return regs8[FLAG_AF] = !!new_AF;
}

// Set overflow flag
char set_OF(int new_OF)
{
	return regs8[FLAG_OF] = !!new_OF;
}

// Set auxiliary and overflow flag after arithmetic operations
char set_AF_OF_arith()
{
	set_AF((op_source ^= op_dest ^ op_result) & 0x10);
	if (op_result == op_dest)
		return set_OF(0);
	else
		return set_OF(1 & (regs8[FLAG_CF] ^ op_source >> (TOP_BIT - 1)));
}

// Assemble and return emulated CPU FLAGS register in scratch_uint
void make_flags()
{
	scratch_uint = 0xF002; // 8086 has reserved and unused flags set to 1
	for (int i = 9; i--;)
		scratch_uint += regs8[FLAG_CF + i] << bios_table_lookup[TABLE_FLAGS_BITFIELDS][i];
}

// Set emulated CPU FLAGS register from regs8[FLAG_xx] values
void set_flags(int new_flags)
{
	for (int i = 9; i--;)
		regs8[FLAG_CF + i] = !!(1 << bios_table_lookup[TABLE_FLAGS_BITFIELDS][i] & new_flags);
}

// Convert raw opcode to translated opcode index. This condenses a large number of different encodings of similar
// instructions into a much smaller number of distinct functions, which we then execute
void set_opcode(unsigned char opcode)
{
	xlat_opcode_id = bios_table_lookup[TABLE_XLAT_OPCODE][raw_opcode_id = opcode];
	extra = bios_table_lookup[TABLE_XLAT_SUBFUNCTION][opcode];
	i_mod_size = bios_table_lookup[TABLE_I_MOD_SIZE][opcode];
	set_flags_type = bios_table_lookup[TABLE_STD_FLAGS][opcode];
}

// Execute INT #interrupt_num on the emulated machine
char pc_interrupt(unsigned char interrupt_num)
{
	set_opcode(0xCD); // Decode like INT

	make_flags();
	R_M_PUSH(scratch_uint);
	R_M_PUSH(regs16[REG_CS]);
	R_M_PUSH(reg_ip);
	MEM_OP(REGS_BASE + 2 * REG_CS, =, 4 * interrupt_num + 2);
	R_M_OP(reg_ip, =, mem[4 * interrupt_num]);

	return regs8[FLAG_TF] = regs8[FLAG_IF] = 0;
}

// AAA and AAS instructions - which_operation is +1 for AAA, and -1 for AAS
int AAA_AAS(char which_operation)
{
	return (regs16[REG_AX] += 262 * which_operation*set_AF(set_CF(((regs8[REG_AL] & 0x0F) > 9) || regs8[FLAG_AF])), regs8[REG_AL] &= 0x0F);
}

void Reset(void)
{
 // Fill RAM with 000
  unsigned int *mem32 = (unsigned int *) mem;
  for (int rp = 0 ; rp < RAM_SIZE ; rp += 4)
  {
    *mem32++ = 0;
  }

  // Clear bios area
  for (int f0 = 0 ; f0 < 65536 ; f0++)
  {
    mem[0xf0000+f0] = 0;
  }

  for (int i = 0 ; i < 3 ; i++)
  {
    if (disk[i] != 0)
    {
      close(disk[i]);
      disk[i] = 0;
    }
  }

  if (Interface.GetBIOSFilename() != NULL)
  {
    disk[2] = open(Interface.GetBIOSFilename(), O_BINARY | O_NOINHERIT | O_RDWR);
  }

  if (Interface.GetFDImageFilename() != NULL)
  {
    disk[1] = open(Interface.GetFDImageFilename(), O_BINARY | O_NOINHERIT | O_RDWR);
  }

  if (Interface.GetHDImageFilename() != NULL)
  {
    disk[0] = open(Interface.GetHDImageFilename(), O_BINARY | O_NOINHERIT | O_RDWR);
  }

	// Set CX:AX equal to the hard disk image size, if present
	CAST(unsigned)regs16[REG_AX] = *disk ? lseek(*disk, 0, 2) >> 9 : 0;

	// CS is initialised to F000
	regs16[REG_CS] = REGS_BASE >> 4;
	// Load BIOS image into F000:0100, and set IP to 0100
	read(disk[2], regs8 + (reg_ip = 0x100), 0xFF00);

	// Initialise CPU state variables
	seg_override_en = 0;
	rep_override_en = 0;

	// Load instruction decoding helper table vectors
	// Load instruction decoding helper table
	for (int i = 0; i < 20; i++)
		for (int j = 0; j < 256; j++)
			bios_table_lookup[i][j] = regs8[regs16[0x81 + i] + j];

}

// Emulator entry point

#if defined(_WIN32) && !defined(__SDL__)
int CALLBACK WinMain(
  HINSTANCE hInstance,
  HINSTANCE /* hPrevInstance */,
  LPSTR     /* lpCmdLine */,
  int       /* nCmdShow */)
#else
int main(int argc, char **argv)
#endif
{

#if defined(_WIN32) && !defined(__SDL__)
  Interface.SetInstance(hInstance);
#endif
  Interface.Initialise(mem);

 	// regs16 and reg8 point to F000:0, the start of memory-mapped registers
	regs16 = (unsigned short *)(regs8 = mem + REGS_BASE);

	// clear bios and disk filed
	for (file_index = 0 ; file_index < 3 ; file_index++) disk[file_index] = 0;

  // Reset, loads initial disk and bios images, clears RAM and sets CS & IP.
  Reset();

	// Instruction execution loop.
	bool ExitEmulation = false;
	while (!ExitEmulation)
	{
	  opcode_stream = mem + 16 * regs16[REG_CS] + reg_ip;

		// Set up variables to prepare for decoding an opcode
		set_opcode(*opcode_stream);

		// Extract i_w and i_d fields from instruction
		i_w = (i_reg4bit = raw_opcode_id & 7) & 1;
		i_d = i_reg4bit / 2 & 1;

		// Extract instruction data fields
		i_data0 = CAST(short)opcode_stream[1];
		i_data1 = CAST(short)opcode_stream[2];
		i_data2 = CAST(short)opcode_stream[3];

		// seg_override_en and rep_override_en contain number of instructions to hold segment override and REP prefix respectively
		if (seg_override_en)
			seg_override_en--;
		if (rep_override_en)
			rep_override_en--;

		// i_mod_size > 0 indicates that opcode uses i_mod/i_rm/i_reg, so decode them
		if (i_mod_size)
		{
			i_mod = (i_data0 & 0xFF) >> 6;
			i_rm = i_data0 & 7;
			i_reg = i_data0 / 8 & 7;

			if ((!i_mod && i_rm == 6) || (i_mod == 2))
				i_data2 = CAST(short)opcode_stream[4];
			else if (i_mod != 1)
				i_data2 = i_data1;
			else // If i_mod is 1, operand is (usually) 8 bits rather than 16 bits
				i_data1 = (char)i_data1;

			DECODE_RM_REG;
		}

		// Instruction execution unit
		switch (xlat_opcode_id)
		{
			OPCODE_CHAIN 0: // Conditional jump (JAE, JNAE, etc.)
				// i_w is the invert flag, e.g. i_w == 1 means JNAE, whereas i_w == 0 means JAE
				scratch_uchar = raw_opcode_id / 2 & 7;
				reg_ip += (char)i_data0 * (i_w ^ (regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_A][scratch_uchar]] ||
                                          regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_B][scratch_uchar]] ||
                                          regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_C][scratch_uchar]] ^ regs8[bios_table_lookup[TABLE_COND_JUMP_DECODE_D][scratch_uchar]]))
			OPCODE 1: // MOV reg, imm
				i_w = !!(raw_opcode_id & 8);
				R_M_OP(mem[GET_REG_ADDR(i_reg4bit)], =, i_data0)
			OPCODE 3: // PUSH regs16
				R_M_PUSH(regs16[i_reg4bit])
			OPCODE 4: // POP regs16
				R_M_POP(regs16[i_reg4bit])
			OPCODE 2: // INC|DEC regs16
				i_w = 1;
				i_d = 0;
				i_reg = i_reg4bit;
				DECODE_RM_REG;
				i_reg = extra
			OPCODE_CHAIN 5: // INC|DEC|JMP|CALL|PUSH
				if (i_reg < 2) // INC|DEC
					MEM_OP(op_from_addr, += 1 - 2 * i_reg +, REGS_BASE + 2 * REG_ZERO),
					op_source = 1,
					set_AF_OF_arith(),
					set_OF(op_dest + 1 - i_reg == 1 << (TOP_BIT - 1)),
					(xlat_opcode_id == 5) && (set_opcode(0x10), 0); // Decode like ADC
				else if (i_reg != 6) // JMP|CALL
					i_reg - 3 || R_M_PUSH(regs16[REG_CS]), // CALL (far)
					i_reg & 2 && R_M_PUSH(reg_ip + 2 + i_mod*(i_mod != 3) + 2*(!i_mod && i_rm == 6)), // CALL (near or far)
					i_reg & 1 && (regs16[REG_CS] = CAST(short)mem[op_from_addr + 2]), // JMP|CALL (far)
					R_M_OP(reg_ip, =, mem[op_from_addr]),
					set_opcode(0x9A); // Decode like CALL
				else // PUSH
					R_M_PUSH(mem[rm_addr])
			OPCODE 6: // TEST r/m, imm16 / NOT|NEG|MUL|IMUL|DIV|IDIV reg
				op_to_addr = op_from_addr;

				switch (i_reg)
				{
					OPCODE_CHAIN 0: // TEST
						set_opcode(0x20); // Decode like AND
						reg_ip += i_w + 1;
						R_M_OP(mem[op_to_addr], &, i_data2)
					OPCODE 2: // NOT
						OP(=~)
					OPCODE 3: // NEG
						OP(=-);
						op_dest = 0;
						set_opcode(0x28); // Decode like SUB
						set_CF(op_result > op_dest)
					OPCODE 4: // MUL
						i_w ? MUL_MACRO(unsigned short, regs16) : MUL_MACRO(unsigned char, regs8)
					OPCODE 5: // IMUL
						i_w ? MUL_MACRO(short, regs16) : MUL_MACRO(char, regs8)
					OPCODE 6: // DIV
						i_w ? DIV_MACRO(unsigned short, unsigned, regs16) : DIV_MACRO(unsigned char, unsigned short, regs8)
					OPCODE 7: // IDIV
						i_w ? DIV_MACRO(short, int, regs16) : DIV_MACRO(char, short, regs8);
				}
			OPCODE 7: // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP AL/AX, immed
				rm_addr = REGS_BASE;
				i_data2 = i_data0;
				i_mod = 3;
				i_reg = extra;
				reg_ip--;
			OPCODE_CHAIN 8: // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP reg, immed
				op_to_addr = rm_addr;
				regs16[REG_SCRATCH] = (i_d |= !i_w) ? (char)i_data2 : i_data2;
				op_from_addr = REGS_BASE + 2 * REG_SCRATCH;
				reg_ip += !i_d + 1;
				set_opcode(0x08 * (extra = i_reg));
			OPCODE_CHAIN 9: // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP|MOV reg, r/m
				switch (extra)
				{
					OPCODE_CHAIN 0: // ADD
						OP(+=),
						set_CF(op_result < op_dest)
					OPCODE 1: // OR
						OP(|=)
					OPCODE 2: // ADC
						ADC_SBB_MACRO(+)
					OPCODE 3: // SBB
						ADC_SBB_MACRO(-)
					OPCODE 4: // AND
						OP(&=)
					OPCODE 5: // SUB
						OP(-=),
						set_CF(op_result > op_dest)
					OPCODE 6: // XOR
						OP(^=)
					OPCODE 7: // CMP
						OP(-),
						set_CF(op_result > op_dest)
					OPCODE 8: // MOV
					  //OP(=);
						MEM_MOV(op_to_addr, op_from_addr);
				}
			OPCODE 10: // MOV sreg, r/m | POP r/m | LEA reg, r/m
				if (!i_w) // MOV
					i_w = 1,
					i_reg += 8,
					DECODE_RM_REG,
					OP(=);
				else if (!i_d) // LEA
					seg_override_en = 1,
					seg_override = REG_ZERO,
					DECODE_RM_REG,
					//R_M_OP(mem[op_from_addr], =, rm_addr);
					R_M_MOV(mem[op_from_addr], rm_addr);
				else // POP
					R_M_POP(mem[rm_addr])
			OPCODE 11: // MOV AL/AX, [loc]
				i_mod = i_reg = 0;
				i_rm = 6;
				i_data1 = i_data0;
				DECODE_RM_REG;
				//MEM_OP(op_from_addr, =, op_to_addr)
				MEM_MOV(op_from_addr, op_to_addr)
			OPCODE 12: // ROL|ROR|RCL|RCR|SHL|SHR|???|SAR reg/mem, 1/CL/imm (80186)
				scratch2_uint = SIGN_OF(mem[rm_addr]),
				scratch_uint = extra ? // xxx reg/mem, imm
					(char)i_data1
				: // xxx reg/mem, CL
					i_d
						? 31 & regs8[REG_CL]
				: // xxx reg/mem, 1
					1;
				if (scratch_uint)
				{
					if (i_reg < 4) // Rotate operations
						scratch_uint %= i_reg / 2 + TOP_BIT,
						R_M_OP(scratch2_uint, =, mem[rm_addr]);
					if (i_reg & 1) // Rotate/shift right operations
						R_M_OP(mem[rm_addr], >>=, scratch_uint);
					else // Rotate/shift left operations
						R_M_OP(mem[rm_addr], <<=, scratch_uint);
					if (i_reg > 3) // Shift operations
            set_flags_type = FLAGS_UPDATE_SZP; // Shift instructions affect SZP
					if (i_reg > 4) // SHR or SAR
						set_CF(op_dest >> (scratch_uint - 1) & 1);
				}

				switch (i_reg)
				{
					OPCODE_CHAIN 0: // ROL
						R_M_OP(mem[rm_addr], += , scratch2_uint >> (TOP_BIT - scratch_uint));
						set_OF(SIGN_OF(op_result) ^ set_CF(op_result & 1))
					OPCODE 1: // ROR
						scratch2_uint &= (1 << scratch_uint) - 1,
						R_M_OP(mem[rm_addr], += , scratch2_uint << (TOP_BIT - scratch_uint));
						set_OF(SIGN_OF(op_result * 2) ^ set_CF(SIGN_OF(op_result)))
					OPCODE 2: // RCL
						R_M_OP(mem[rm_addr], += (regs8[FLAG_CF] << (scratch_uint - 1)) + , scratch2_uint >> (1 + TOP_BIT - scratch_uint));
						set_OF(SIGN_OF(op_result) ^ set_CF(scratch2_uint & 1 << (TOP_BIT - scratch_uint)))
					OPCODE 3: // RCR
						R_M_OP(mem[rm_addr], += (regs8[FLAG_CF] << (TOP_BIT - scratch_uint)) + , scratch2_uint << (1 + TOP_BIT - scratch_uint));
						set_CF(scratch2_uint & 1 << (scratch_uint - 1));
						set_OF(SIGN_OF(op_result) ^ SIGN_OF(op_result * 2))
					OPCODE 4: // SHL
						set_OF(SIGN_OF(op_result) ^ set_CF(SIGN_OF(op_dest << (scratch_uint - 1))))
					OPCODE 5: // SHR
						set_OF(SIGN_OF(op_dest))
					OPCODE 7: // SAR
						scratch_uint < TOP_BIT || set_CF(scratch2_uint);
						set_OF(0);
						R_M_OP(mem[rm_addr], +=, scratch2_uint *= ~(((1 << TOP_BIT) - 1) >> scratch_uint));
				}
			OPCODE 13: // LOOPxx|JCZX
				scratch_uint = !!--regs16[REG_CX];

				switch(i_reg4bit)
				{
					OPCODE_CHAIN 0: // LOOPNZ
						scratch_uint &= !regs8[FLAG_ZF]
					OPCODE 1: // LOOPZ
						scratch_uint &= regs8[FLAG_ZF]
					OPCODE 3: // JCXXZ
						scratch_uint = !++regs16[REG_CX];
				}
				reg_ip += scratch_uint*(char)i_data0
			OPCODE 14: // JMP | CALL short/near
				reg_ip += 3 - i_d;
				if (!i_w)
				{
					if (i_d) // JMP far
						reg_ip = 0,
						regs16[REG_CS] = i_data2;
					else // CALL
						R_M_PUSH(reg_ip);
				}
				reg_ip += i_d && i_w ? (char)i_data0 : i_data0
			OPCODE 15: // TEST reg, r/m
				MEM_OP(op_from_addr, &, op_to_addr)
			OPCODE 16: // XCHG AX, regs16
				i_w = 1;
				op_to_addr = REGS_BASE;
				op_from_addr = GET_REG_ADDR(i_reg4bit);
			OPCODE_CHAIN 24: // NOP|XCHG reg, r/m
				if (op_to_addr != op_from_addr)
					OP(^=),
					MEM_OP(op_from_addr, ^=, op_to_addr),
					OP(^=)
			OPCODE 17: // MOVSx (extra=0)|STOSx (extra=1)|LODSx (extra=2)
				scratch2_uint = seg_override_en ? seg_override : REG_DS;

				for (scratch_uint = rep_override_en ? regs16[REG_CX] : 1; scratch_uint; scratch_uint--)
				{
					//MEM_OP(extra < 2 ? SEGREG(REG_ES, REG_DI) : REGS_BASE, =, extra & 1 ? REGS_BASE : SEGREG(scratch2_uint, REG_SI)),
					MEM_MOV(extra < 2 ? SEGREG(REG_ES, REG_DI) : REGS_BASE, extra & 1 ? REGS_BASE : SEGREG(scratch2_uint, REG_SI)),
					extra & 1 || INDEX_INC(REG_SI),
					extra & 2 || INDEX_INC(REG_DI);
				}

				if (rep_override_en)
					regs16[REG_CX] = 0
			OPCODE 18: // CMPSx (extra=0)|SCASx (extra=1)
				scratch2_uint = seg_override_en ? seg_override : REG_DS;

				if ((scratch_uint = rep_override_en ? regs16[REG_CX] : 1))
				{
					for (; scratch_uint; rep_override_en || scratch_uint--)
					{
						MEM_OP(extra ? REGS_BASE : SEGREG(scratch2_uint, REG_SI), -, SEGREG(REG_ES, REG_DI)),
						extra || INDEX_INC(REG_SI),
						INDEX_INC(REG_DI), rep_override_en && !(--regs16[REG_CX] && (!op_result == rep_mode)) && (scratch_uint = 0);
					}

					set_flags_type = FLAGS_UPDATE_SZP | FLAGS_UPDATE_AO_ARITH; // Funge to set SZP/AO flags
					set_CF(op_result > op_dest);
				}
			OPCODE 19: // RET|RETF|IRET
				i_d = i_w;
				R_M_POP(reg_ip);
				if (extra) // IRET|RETF|RETF imm16
					R_M_POP(regs16[REG_CS]);
				if (extra & 2) // IRET
					set_flags(R_M_POP(scratch_uint));
				else if (!i_d) // RET|RETF imm16
				  regs16[REG_SP] += i_data0
			OPCODE 20: // MOV r/m, immed
        //R_M_OP(mem[op_from_addr], =, i_data2)
        regs16[REG_TMP] = i_data2;
        MEM_MOV(op_from_addr, REGS_BASE + REG_TMP * 2)
		  OPCODE 21: // IN AL/AX, DX/imm8
				scratch_uint = extra ? regs16[REG_DX] : (unsigned char)i_data0;
        io_ports[scratch_uint] = Interface.ReadPort(scratch_uint);
				if (i_w)
        {
          io_ports[scratch_uint+1] = Interface.ReadPort(scratch_uint+1);
        }
				R_M_OP(regs8[REG_AL], =, io_ports[scratch_uint]);
			OPCODE 22: // OUT DX/imm8, AL/AX
			  scratch_uint = extra ? regs16[REG_DX] : (unsigned char)i_data0;
				R_M_OP(io_ports[scratch_uint], =, regs8[REG_AL]);
        Interface.WritePort(scratch_uint, io_ports[scratch_uint]);
        if (i_w)
        {
          Interface.WritePort(scratch_uint+1, io_ports[scratch_uint+1]);
        }
			OPCODE 23: // REPxx
				rep_override_en = 2;
				rep_mode = i_w;
				seg_override_en && seg_override_en++
			OPCODE 25: // PUSH reg
				R_M_PUSH(regs16[extra])
			OPCODE 26: // POP reg
				R_M_POP(regs16[extra])
			OPCODE 27: // xS: segment overrides
				seg_override_en = 2;
				seg_override = extra;
				rep_override_en && rep_override_en++
			OPCODE 28: // DAA/DAS
				i_w = 0;
        // extra = 0 for DAA, 1 for DAS
				if (extra) DAA_DAS(-=, >); else DAA_DAS(+=, <)
			OPCODE 29: // AAA/AAS
				op_result = AAA_AAS(extra - 1)
			OPCODE 30: // CBW
				regs8[REG_AH] = -SIGN_OF(regs8[REG_AL])
			OPCODE 31: // CWD
				regs16[REG_DX] = -SIGN_OF(regs16[REG_AX])
			OPCODE 32: // CALL FAR imm16:imm16
				R_M_PUSH(regs16[REG_CS]);
				R_M_PUSH(reg_ip + 5);
				regs16[REG_CS] = i_data2;
				reg_ip = i_data0
			OPCODE 33: // PUSHF
				make_flags();
				R_M_PUSH(scratch_uint)
			OPCODE 34: // POPF
				set_flags(R_M_POP(scratch_uint))
			OPCODE 35: // SAHF
				make_flags();
				set_flags((scratch_uint & 0xFF00) + regs8[REG_AH])
			OPCODE 36: // LAHF
				make_flags(),
				regs8[REG_AH] = scratch_uint
			OPCODE 37: // LES|LDS reg, r/m
				i_w = i_d = 1;
				DECODE_RM_REG;
				OP(=);
				MEM_OP(REGS_BASE + extra, =, rm_addr + 2)
			OPCODE 38: // INT 3
				++reg_ip;
				pc_interrupt(3)
			OPCODE 39: // INT imm8
				reg_ip += 2;
				pc_interrupt(i_data0)
			OPCODE 40: // INTO
				++reg_ip;
				regs8[FLAG_OF] && pc_interrupt(4)
			OPCODE 41: // AAM;
				if (i_data0 &= 0xFF)
					regs8[REG_AH] = regs8[REG_AL] / i_data0,
					op_result = regs8[REG_AL] %= i_data0;
				else // Divide by zero
					pc_interrupt(0);
			OPCODE 42: // AAD
				i_w = 0;
				regs16[REG_AX] = op_result = 0xFF & (regs8[REG_AL] + i_data0 * regs8[REG_AH])
			OPCODE 43: // SALC
				regs8[REG_AL] = -regs8[FLAG_CF]
			OPCODE 44: // XLAT
				regs8[REG_AL] = mem[SEGREG_OP(seg_override_en ? seg_override : REG_DS, REG_BX, regs8[REG_AL] +)]
			OPCODE 45: // CMC
				regs8[FLAG_CF] ^= 1
			OPCODE 46: // CLC|STC|CLI|STI|CLD|STD
				regs8[extra / 2] = extra & 1
			OPCODE 47: // TEST AL/AX, immed
				R_M_OP(regs8[REG_AL], &, i_data0)
      OPCODE 48: // LOCK:
      OPCODE 49: // HLT
			OPCODE 50: // Emulator-specific 0F xx opcodes
				switch ((char)i_data0)
				{
					OPCODE_CHAIN 0: // PUTCHAR_AL
						putchar(regs8[0]); //write(1, regs8, 1)
					OPCODE 1: // GET_RTC
						time(&clock_buf);
						ftime(&ms_clock);
						memcpy(mem + SEGREG(REG_ES, REG_BX), localtime(&clock_buf), sizeof(struct tm));
						CAST(short)mem[SEGREG_OP(REG_ES, REG_BX, 36+)] = ms_clock.millitm;
					OPCODE 2: // DISK_READ
					OPCODE_CHAIN 3: // DISK_WRITE
						regs8[REG_AL] = ~lseek(disk[regs8[REG_DL]], CAST(unsigned)regs16[REG_BP] << 9, 0)
							? ((char)i_data0 == 3 ? (int(*)(int, const void *, int))write : (int(*)(int, const void *, int))read)(disk[regs8[REG_DL]], mem + SEGREG(REG_ES, REG_BX), regs16[REG_AX])
							: 0;
				}
      OPCODE 51: // 80186, NEC V20: ENTER
        // i_data0 = locals
        // LSB(i_data2)  = lex level
        R_M_PUSH(regs16[REG_BP]);
        scratch_uint = regs16[REG_SP];
        scratch2_uint = i_data2 &= 0x00ff;

        if (scratch2_uint > 0)
        {
          while (scratch2_uint != 1)
          {
            scratch2_uint--;
            regs16[REG_BP] -= 2;
            R_M_PUSH(regs16[REG_BP]);
          }
          R_M_PUSH(scratch_uint);
        }
        regs16[REG_BP] = scratch_uint;
        regs16[REG_SP] -= i_data0
      OPCODE 52: // 80186, NEC V20: LEAVE
        regs16[REG_SP] = regs16[REG_BP];
        R_M_POP(regs16[REG_BP])
      OPCODE 53: // 80186, NEC V20: PUSHA
        // PUSH AX, PUSH CX, PUSH DX, PUSH BX, PUSH SP, PUSH BP, PUSH SI, PUSH DI
        R_M_PUSH(regs16[REG_AX]);
        R_M_PUSH(regs16[REG_CX]);
        R_M_PUSH(regs16[REG_DX]);
        R_M_PUSH(regs16[REG_BX]);
        scratch_uint = regs16[REG_SP];
        R_M_PUSH(scratch_uint);
        R_M_PUSH(regs16[REG_BP]);
        R_M_PUSH(regs16[REG_SI]);
        R_M_PUSH(regs16[REG_DI])
      OPCODE 54: // 80186, NEC V20: POPA
        // POP DI, POP SI, POP BP, ADD SP,2, POP BX, POP DX, POP CX, POP AX
        R_M_POP(regs16[REG_DI]);
        R_M_POP(regs16[REG_SI]);
        R_M_POP(regs16[REG_BP]);
        regs16[REG_SP] += 2;
        R_M_POP(regs16[REG_BX]);
        R_M_POP(regs16[REG_DX]);
        R_M_POP(regs16[REG_CX]);
        R_M_POP(regs16[REG_AX])
      OPCODE 55: // 80186: BOUND
        // not implemented. Incompatible with PC/XT hardware
        printf("BOUND\n")
      OPCODE 56: // 80186, NEC V20: PUSH imm16
        R_M_PUSH(i_data0)
      OPCODE 57: // 80186, NEC V20: PUSH imm8
        R_M_PUSH(i_data0 & 0x00ff)
      OPCODE 58: // 80186 IMUL
        // not implemented
        printf("IMUL at %04X:%04X\n", regs16[REG_CS], reg_ip)
      OPCODE 59: // 80186: INSB INSW
        // Loads data from port to the destination ES:DI.
        // DI is adjusted by the size of the operand and increased if the
        // Direction Flag is cleared and decreased if the Direction Flag is set.

				scratch2_uint = regs16[REG_DX];

				for (scratch_uint = rep_override_en ? regs16[REG_CX] : 1 ; scratch_uint ; scratch_uint--)
				{
          io_ports[scratch2_uint] = Interface.ReadPort(scratch2_uint);
				  if (i_w)
          {
            io_ports[scratch2_uint+1] = Interface.ReadPort(scratch2_uint+1);
          }

				  R_M_OP(mem[SEGREG(REG_ES, REG_DI)], =, io_ports[scratch_uint]);
					INDEX_INC(REG_DI);
				}

				if (rep_override_en)
					regs16[REG_CX] = 0

      OPCODE 60: // 80186: OUTSB OUTSW
        // Transfers a byte or word "src" to the hardware port specified in DX.
        // The "src" is located at DS:SI and SI is incremented or decremented
        // by the size dictated by the instruction format.
        // When the Direction Flag is set SI is decremented, when clear, SI is
        // incremented.
				scratch2_uint = regs16[REG_DX];

				for (scratch_uint = rep_override_en ? regs16[REG_CX] : 1 ; scratch_uint ; scratch_uint--)
				{
				  R_M_OP(io_ports[scratch2_uint], =, mem[SEGREG(REG_DS, REG_SI)]);
          Interface.WritePort(scratch2_uint, io_ports[scratch2_uint]);
          if (i_w)
          {
            Interface.WritePort(scratch2_uint+1, io_ports[scratch2_uint+1]);
          }
					INDEX_INC(REG_SI);
				}

				if (rep_override_en)
					regs16[REG_CX] = 0

      OPCODE 69: // 8087 MATH Coprocessor
        printf("8087 coprocessor instruction: 0x%02X\n", raw_opcode_id)
      OPCODE 70: // 80286+
        printf("80286+ only op code: 0x%02X at %04X:%04X\n", raw_opcode_id, regs16[REG_CS], reg_ip)
      OPCODE 71: // 80386+
        printf("80386+ only op code: 0x%02X at %04X:%04X\n", raw_opcode_id, regs16[REG_CS], reg_ip)
      OPCODE 72: // BAD OP CODE
        printf("Bad op code: %02x  at %04X:%04X\n", raw_opcode_id, regs16[REG_CS], reg_ip);
		}

		// Increment instruction pointer by computed instruction length. Tables in the BIOS binary
		// help us here.
		reg_ip += (i_mod*(i_mod != 3) + 2*(!i_mod && i_rm == 6))*i_mod_size + bios_table_lookup[TABLE_BASE_INST_SIZE][raw_opcode_id] + bios_table_lookup[TABLE_I_W_SIZE][raw_opcode_id]*(i_w + 1);

		// If instruction needs to update SF, ZF and PF, set them as appropriate
		if (set_flags_type & FLAGS_UPDATE_SZP)
		{
			regs8[FLAG_SF] = SIGN_OF(op_result);
			regs8[FLAG_ZF] = !op_result;
			regs8[FLAG_PF] = bios_table_lookup[TABLE_PARITY_FLAG][(unsigned char)op_result];

			// If instruction is an arithmetic or logic operation, also set AF/OF/CF as appropriate.
			if (set_flags_type & FLAGS_UPDATE_AO_ARITH)
				set_AF_OF_arith();
			if (set_flags_type & FLAGS_UPDATE_OC_LOGIC)
				set_CF(0), set_OF(0);
		}

		regs16[REG_IP] = reg_ip;

		// Update the interface module
		if (Interface.TimerTick(4))
		{
		  if (Interface.ExitEmulation())
      {
        ExitEmulation = true;
      }
      else
      {
        if (Interface.FDChanged())
        {
          close(disk[1]);
          disk[1] = open(Interface.GetFDImageFilename(), O_BINARY | O_NOINHERIT | O_RDWR);
        }

        if (Interface.Reset())
        {
          Reset();
        }
      }

		}

		// Application has set trap flag, so fire INT 1
		if (trap_flag)
    {
			pc_interrupt(1);
    }

		trap_flag = regs8[FLAG_TF];

    // Check for interrupts triggered by system interfaces
    int IntNo;
    static int InstrSinceInt8 = 0;
    InstrSinceInt8++;
    if (!seg_override_en && !rep_override_en &&
        regs8[FLAG_IF] && !regs8[FLAG_TF] &&
        Interface.IntPending(IntNo))
    {
      if ((IntNo == 8) && (InstrSinceInt8 < 300))
      {
        //printf("*** Int8 after %d instructions\n", InstrSinceInt8);
      }
      else
      {
        if (IntNo == 8)
        {
          InstrSinceInt8 = 0;
        }
        pc_interrupt(IntNo);

        regs16[REG_IP] = reg_ip;
        Interface.CheckBreakPoints();
      }
    }

	} // for each instruction


	Interface.Cleanup();

	return 0;
}

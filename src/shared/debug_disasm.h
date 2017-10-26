// =============================================================================
// File: debug_disasm.h
//
// Description:
// 80x86 disassembler modified to work with 8086 tiny.
//

#ifndef __DEBUG_DISASM_H
#define __DEBUG_DISASM_H

unsigned int DasmI386(char* buffer, unsigned char *memory, unsigned int pc, unsigned int cur_ip, bool bit32);

int DasmLastOperandSize();

#endif // __DEBUG_DISASM_H

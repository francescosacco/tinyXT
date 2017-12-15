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

#include <windows.h>
#include <sys/timeb.h>
#include <fcntl.h>
#include <conio.h>

#include <unistd.h>

#ifdef _MSC_VER
  #include <io.h>
#endif

#include "8086tiny_interface.h"
#include "emulator/XTmemory.h"

T8086TinyInterface_t Interface ;

#define XFALSE                                   ( ( uint8_t ) 0x00 )
#define XTRUE                                    ( ( uint8_t ) 0x01 )

// Emulator system constants

#define REGS_BASE                                0xF0000

// 16-bit register decodes

#define REG_AX                                   0
#define REG_CX                                   1
#define REG_DX                                   2
#define REG_BX                                   3
#define REG_SP                                   4
#define REG_BP                                   5
#define REG_SI                                   6
#define REG_DI                                   7

#define REG_ES                                   8
#define REG_CS                                   9
#define REG_SS                                   10
#define REG_DS                                   11

#define REG_ZERO                                 12
#define REG_SCRATCH                              13

#define REG_IP                                   14
#define REG_TMP                                  15

// 8-bit register decodes
#define REG_AL                                   0
#define REG_AH                                   1
#define REG_CL                                   2
#define REG_CH                                   3
#define REG_DL                                   4
#define REG_DH                                   5
#define REG_BL                                   6
#define REG_BH                                   7

// FLAGS register decodes
#define FLAG_CF                                  40
#define FLAG_PF                                  41
#define FLAG_AF                                  42
#define FLAG_ZF                                  43
#define FLAG_SF                                  44
#define FLAG_TF                                  45
#define FLAG_IF                                  46
#define FLAG_DF                                  47
#define FLAG_OF                                  48

// Lookup tables in the BIOS binary
#define TABLE_XLAT_OPCODE                        8
#define TABLE_XLAT_SUBFUNCTION                   9
#define TABLE_STD_FLAGS                          10
#define TABLE_PARITY_FLAG                        11
#define TABLE_BASE_INST_SIZE                     12
#define TABLE_I_W_SIZE                           13
#define TABLE_I_MOD_SIZE                         14
#define TABLE_COND_JUMP_DECODE_A                 15
#define TABLE_COND_JUMP_DECODE_B                 16
#define TABLE_COND_JUMP_DECODE_C                 17
#define TABLE_COND_JUMP_DECODE_D                 18
#define TABLE_FLAGS_BITFIELDS                    19

// Bitfields for TABLE_STD_FLAGS values
#define FLAGS_UPDATE_SZP                         1
#define FLAGS_UPDATE_AO_ARITH                    2
#define FLAGS_UPDATE_OC_LOGIC                    4

// Helper macros

// [I]MUL/[I]DIV/DAA/DAS/ADC/SBB helpers
#define MUL_MACRO(op_data_type,out_regs) (set_opcode(0x10), \
                                          out_regs[i_w + 1] = (op_result = *(op_data_type*)&mem[rm_addr] * (op_data_type)*out_regs) >> 16, \
                                          regs16[REG_AX] = op_result, \
                                          set_OF(set_CF(op_result - (op_data_type)op_result)))
#define DIV_MACRO(out_data_type,in_data_type,out_regs) (scratch_int = *(out_data_type*)&mem[rm_addr]) && !(scratch2_uint = (in_data_type)(scratch_uint = (out_regs[i_w+1] << 16) + regs16[REG_AX]) / scratch_int, scratch2_uint - (out_data_type)scratch2_uint) ? out_regs[i_w+1] = scratch_uint - scratch_int * (*out_regs = scratch2_uint) : pc_interrupt(0)
#define DAA_DAS(op1,op2) \
                  set_AF((((scratch_uchar = regs8[REG_AL]) & 0x0F) > 9) || regs8[FLAG_AF]) && (op_result = (regs8[REG_AL] op1 6), set_CF(regs8[FLAG_CF] || (regs8[REG_AL] op2 scratch_uchar))), \
                                  set_CF((regs8[REG_AL] > 0x9f) || regs8[FLAG_CF]) && (op_result = (regs8[REG_AL] op1 0x60))
#define ADC_SBB_MACRO(a) MEM_OP(op_to_addr,a##= regs8[FLAG_CF] +,op_from_addr), \
                         set_CF((regs8[FLAG_CF] && (op_result == op_dest)) || (a op_result < a(int)op_dest)), \
                         set_AF_OF_arith()

// Execute arithmetic/logic operations in emulator memory/registers
#define R_M_OP(dest,op,src) (i_w ? op_dest = *(uint16_t*)&dest, op_result = *(uint16_t*)&dest op (op_source = *(uint16_t*)&src) \
                                 : (op_dest = dest, op_result = dest op (op_source = *(uint8_t*)&src)))

// Execute a memory move with no other operation on dest
#define R_M_MOV(dest,src) (i_w ? op_dest = *(uint16_t*)&dest, op_result = *(uint16_t*)&dest = (op_source = *(uint16_t*)&src) \
                                 : (op_dest = dest, op_result = dest = (op_source = *(uint8_t*)&src)))

#define MEM_OP(dest,op,src) R_M_OP(mem[dest],op,mem[src])
#define MEM_MOV(dest, src) R_M_MOV(mem[dest],mem[src])

// Increment or decrement a register #reg_id (usually SI or DI), depending on direction flag and operand size (given by i_w)
#define INDEX_INC(reg_id) (regs16[reg_id] -= (2 * regs8[FLAG_DF] - 1)*(i_w + 1))

// Helpers for stack operations
#define R_M_PUSH(a) (i_w = 1, R_M_OP(mem[SEGREG_OP(REG_SS, REG_SP, --)], =, a))
#define R_M_POP(a) (i_w = 1, regs16[REG_SP] += 2, R_M_OP(a, =, mem[SEGREG_OP(REG_SS, REG_SP, -2+)]))

// Convert segment:offset to linear address in emulator memory space
#define SEGREG(reg_seg,reg_ofs) 16 * regs16[reg_seg] + (uint16_t)(regs16[reg_ofs])
#define SEGREG_OP(reg_seg,reg_ofs,op) 16 * regs16[reg_seg] + (uint16_t)(op regs16[reg_ofs])

// Global variable definitions

typedef struct STOPCODE_T
{
  uint32_t set_flags_type  ;
  uint8_t  raw_opcode_id   ;
  uint8_t  xlat_opcode_id  ;
  uint8_t  extra           ;
  uint8_t  i_mod_size      ;
} stOpcode_t ;

stOpcode_t stOpcode ;

uint32_t op_source      ;
uint32_t op_dest        ;
uint32_t rm_addr        ;
uint32_t op_to_addr     ;
uint32_t op_from_addr   ;
uint32_t scratch_uint   ;
uint32_t scratch2_uint  ;

int i_data1r, op_result, disk[3], scratch_int;

uint16_t * regs16       ;
uint16_t   reg_ip       ;
uint16_t   seg_override ;
uint16_t   i_data0      ;
uint16_t   i_data1      ;
uint16_t   i_data2      ;

uint8_t   bios_table_lookup[ 20 ][ 256 ] ;
uint8_t * opcode_stream   ;
uint8_t * regs8           ;
uint8_t   i_rm            ;
uint8_t   i_w             ;
uint8_t   i_reg           ;
uint8_t   i_mod           ;
uint8_t   i_d             ;
uint8_t   i_reg4bit       ;
uint8_t   rep_mode        ;
uint8_t   seg_override_en ;
uint8_t   rep_override_en ;
uint8_t   trap_flag       ;
uint8_t   scratch_uchar   ;

time_t clock_buf ;
struct timeb ms_clock ;

// Helper functions

// Set carry flag
int8_t set_CF( int new_CF )
{
  uint8_t reg ;

  reg = ( new_CF ) ? XTRUE : XFALSE ;
  regs8[ FLAG_CF ] = reg ;

  return( reg ) ;
}

// Set auxiliary flag
int8_t set_AF(int new_AF)
{
  uint8_t reg ;

  reg = ( new_AF ) ? XTRUE : XFALSE ;
  regs8[ FLAG_AF ] = reg ;

  return( reg ) ;
}

// Set overflow flag
int8_t set_OF(int new_OF)
{
  uint8_t reg ;

  reg = ( new_OF ) ? XTRUE : XFALSE ;
  regs8[ FLAG_OF ] = reg ;

  return( reg ) ;
}

// Set auxiliary and overflow flag after arithmetic operations
int8_t set_AF_OF_arith( void )
{
  uint8_t reg ;

  op_source ^= ( op_dest ^ op_result ) ;
  reg = op_source & 0x10 ;
  set_AF( reg ) ;

  if( op_result == op_dest )
  {
    reg = set_OF( 0 ) ;
  }
  else
  {
    reg = set_OF( 1 & ( regs8[ FLAG_CF ] ^ op_source >> ( 8 * ( i_w + 1 ) - 1 ) ) ) ;
  }

  return( reg ) ;
}

// Assemble and return emulated CPU FLAGS register in scratch_uint
void make_flags( void )
{
  uint8_t i ;

  // 8086 has reserved and unused flags set to 1
  scratch_uint = 0xF002 ;
  for( i = 0 ; i < 9 ; i++ )
  {
    scratch_uint += regs8[ FLAG_CF + i ] << bios_table_lookup[ TABLE_FLAGS_BITFIELDS ][ i ] ;
  }
}

// Set emulated CPU FLAGS register from regs8[FLAG_xx] values
void set_flags( int new_flags )
{
  uint8_t i ;

  for(i = 0 ; i < 9 ; i++ )
  {
    regs8[ FLAG_CF + i ] = ( 1 << bios_table_lookup[ TABLE_FLAGS_BITFIELDS ][ i ] & new_flags ) ? XTRUE : XFALSE ;
  }
}

// Convert raw opcode to translated opcode index. This condenses a large number of different encodings of similar
// instructions into a much smaller number of distinct functions, which we then execute
void set_opcode( uint8_t opcode )
{
  stOpcode.raw_opcode_id  = opcode ;
  stOpcode.xlat_opcode_id = bios_table_lookup[ TABLE_XLAT_OPCODE      ][ opcode ] ;
  stOpcode.extra          = bios_table_lookup[ TABLE_XLAT_SUBFUNCTION ][ opcode ] ;
  stOpcode.i_mod_size     = bios_table_lookup[ TABLE_I_MOD_SIZE       ][ opcode ] ;
  stOpcode.set_flags_type = bios_table_lookup[ TABLE_STD_FLAGS        ][ opcode ];
}

// Execute INT #interrupt_num on the emulated machine
int8_t pc_interrupt( uint8_t interrupt_num )
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
int AAA_AAS(int8_t which_operation)
{
    return (regs16[REG_AX] += 262 * which_operation*set_AF(set_CF(((regs8[REG_AL] & 0x0F) > 9) || regs8[FLAG_AF])), regs8[REG_AL] &= 0x0F);
}

void Reset(void)
{
  uint32_t i ;

  // Fill RAM with 00h.
  // BIOS area is 64K from F0000h.
  memset( ( void * ) mem , 0x00 , ( size_t ) RAM_SIZE ) ;

  for( i = 0 ; i < 3 ; i++ )
  {
    if( disk[ i ] != 0 )
    {
      close( disk[ i ] ) ;
      disk[ i ] = 0 ;
    }
  }

  if( Interface.GetBIOSFilename() != NULL )
  {
    disk[ 2 ] = open( Interface.GetBIOSFilename() , O_BINARY | O_NOINHERIT | O_RDWR ) ;
  }

  if( Interface.GetFDImageFilename() != NULL )
  {
    disk[ 1 ] = open( Interface.GetFDImageFilename() , O_BINARY | O_NOINHERIT | O_RDWR ) ;
  }

  if( Interface.GetHDImageFilename() != NULL )
  {
    disk[ 0 ] = open( Interface.GetHDImageFilename() , O_BINARY | O_NOINHERIT | O_RDWR ) ;
  }

  // Set CX:AX equal to the hard disk image size, if present
  *( uint32_t * )&regs16[ REG_AX ] = ( disk[ 0 ] ) ? ( lseek( disk[ 0 ] , 0 , 2 ) >> 9 ) : ( 0 ) ;

  // CS is initialised to F000
  regs16[ REG_CS ] = ( REGS_BASE >> 4 ) ;

  // Load BIOS image into F000:0100, and set IP to 0100
  reg_ip = 0x100 ;
  read( disk[ 2 ] , ( regs8 + 0x100 ) , 0xFF00 ) ;

  // Initialise CPU state variables
  seg_override_en = 0 ;
  rep_override_en = 0 ;

  // Load instruction decoding helper table vectors
  for( i = 0 ; i < 20 ; i++ )
  {
    for( int j = 0 ; j < 256 ; j++ )
    {
      bios_table_lookup[ i ][ j ] = regs8[ regs16[ 0x81 + i ] + j ] ;
    }
  }
}

// Emulator entry point

#if defined(_WIN32)
int CALLBACK WinMain(
  HINSTANCE hInstance,
  HINSTANCE /* hPrevInstance */,
  LPSTR     /* lpCmdLine */,
  int       /* nCmdShow */)
#else
int main(int argc, char **argv)
#endif
{

#if defined(_WIN32)
  Interface.SetInstance(hInstance);
#endif
  Interface.Initialise(mem);

  // regs16 and reg8 point to F000:0, the start of memory-mapped registers
  regs8  = ( uint8_t  * ) ( mem + REGS_BASE ) ;
  regs16 = ( uint16_t * ) ( mem + REGS_BASE ) ;

  // Clear BIOS and disk filed.
  disk[ 0 ] = 0 ;
  disk[ 1 ] = 0 ;
  disk[ 2 ] = 0 ;

  // Reset, loads initial disk and bios images, clears RAM and sets CS & IP.
  Reset();

  // Instruction execution loop.
  bool ExitEmulation = false;
  while( !ExitEmulation )
  {
    opcode_stream = mem + 16 * regs16[ REG_CS ] + reg_ip ;

    // Set up variables to prepare for decoding an opcode.
    set_opcode( *opcode_stream ) ;

    /**********
     *
     *      7     6     5     4     3     2     1     0
     *   +-----+-----+-----+-----+-----+-----+-----+-----+
     *   |     |     |     |     |     |       REG       |
     *   +-----+-----+-----+-----+-----+-----+-----+-----+
     *                                        \_ _/ \_ _/
     *                                          |     |
     *                                          |     +---> W
     *                                          +---------> D
     *
     **********/
    // Extract i_w and i_d fields from instruction.

    i_reg4bit = stOpcode.raw_opcode_id & 0x07 ;
    i_w       = ( i_reg4bit & 0x01 ) == 0x01 ;
    i_d       = ( i_reg4bit & 0x02 ) == 0x02 ;

    // Extract instruction data fields
    i_data0 = *( int16_t * )&opcode_stream[ 1 ] ;
    i_data1 = *( int16_t * )&opcode_stream[ 2 ] ;
    i_data2 = *( int16_t * )&opcode_stream[ 3 ] ;

    // seg_override_en and rep_override_en contain number of instructions to hold segment override and REP prefix respectively
    if( seg_override_en )
    {
      seg_override_en-- ;
    }

    if( rep_override_en )
    {
      rep_override_en-- ;
    }

    // i_mod_size > 0 indicates that opcode uses i_mod/i_rm/i_reg, so decode them
    if( stOpcode.i_mod_size )
    {

    /**********
     *
     *      7     6     5     4     3     2     1     0
     *   +-----+-----+-----+-----+-----+-----+-----+-----+
     *   |           |                 |                 |
     *   +-----+-----+-----+-----+-----+-----+-----+-----+
     *    \____ ____/ \_______ _______/ \_______ _______/
     *         |              |                 |
     *         |              |                 +---------> i_rm
     *         |              +---------------------------> i_reg
     *         +------------------------------------------> i_mod
     *
     **********/
      i_mod = ( i_data0 >> 6 ) & 0x03 ; // ##......
      i_reg = ( i_data0 >> 3 ) & 0x07 ; // ..###...
      i_rm  =   i_data0        & 0x07 ; // .....###

      if( ( !i_mod && i_rm == 6 ) || ( i_mod == 2 ) )
      {
        i_data2 = *( int16_t * )&opcode_stream[ 4 ] ;
      }
      else if( i_mod != 1 )
      {
        i_data2 = i_data1 ;
      }
      else // If i_mod is 1, operand is (usually) 8 bits rather than 16 bits
      {
        i_data1 = ( int8_t )i_data1 ;
      }

      scratch2_uint = 4 * !i_mod ;
      if( i_mod < 3 )
      {
        uint16_t localIndex ;
        uint16_t localAddr  ;

        if( seg_override_en )
        {
          localIndex = seg_override ;
        }
        else
        {
          localIndex = bios_table_lookup[ scratch2_uint + 3 ][ i_rm ] ;
        }

        localAddr  = ( uint16_t ) regs16[ bios_table_lookup[ scratch2_uint + 1 ][ i_rm ] ] ;
        localAddr += ( uint16_t ) bios_table_lookup[ scratch2_uint + 2 ][ i_rm ] * i_data1 ;
        localAddr += ( uint16_t ) regs16[ bios_table_lookup[ scratch2_uint ][ i_rm ] ] ;
        rm_addr = ( 16 * regs16[ localIndex ] ) + localAddr ;
      }
      else
      {
        rm_addr = ( REGS_BASE + ( ( i_w ) ? ( 2 * i_rm ) : ( 2 * i_rm + i_rm / 4 ) & 7 ) ) ;
      }

      op_to_addr   = rm_addr ;
      op_from_addr = ( REGS_BASE + ( ( i_w ) ? ( 2 * i_reg ) : ( 2 * i_reg + i_reg / 4 ) & 7 ) ) ;
      if( i_d )
      {
        scratch_uint = op_from_addr ;
        op_from_addr = rm_addr      ;
        op_to_addr   = scratch_uint ;
      }
    }

    // Instruction execution unit.
    switch( stOpcode.xlat_opcode_id )
    {
    // Conditional jump (JAE, JNAE, etc.)
    case 0x00 :
      // i_w is the invert flag, e.g. i_w == 1 means JNAE, whereas i_w == 0 means JAE
      scratch_uchar  = stOpcode.raw_opcode_id ;
      scratch_uchar >>= 1 ;
      scratch_uchar  &= 7 ;

      reg_ip += (int8_t)i_data0 * ( i_w ^ ( regs8[ bios_table_lookup[ TABLE_COND_JUMP_DECODE_A ][ scratch_uchar ] ] ||
                                  regs8[ bios_table_lookup[ TABLE_COND_JUMP_DECODE_B ][ scratch_uchar ] ] ||
                                  regs8[ bios_table_lookup[ TABLE_COND_JUMP_DECODE_C ][ scratch_uchar ] ] ^
                                  regs8[ bios_table_lookup[ TABLE_COND_JUMP_DECODE_D ][ scratch_uchar ] ] ) ) ;
      break ;

    // MOV reg, imm
    case 0x01 :
      if( stOpcode.raw_opcode_id & 8 )
      {
        i_w = 1 ;
        *(uint16_t*)&op_dest   = *(uint16_t*)&mem[ REGS_BASE + ( 2 * i_reg4bit ) ] ;
        *(uint16_t*)&op_source = *(uint16_t*)&i_data0 ;
        *(uint16_t*)&op_result = *(uint16_t*)&i_data0 ;
        *(uint16_t*)&mem[ REGS_BASE + ( 2 * i_reg4bit ) ] = *(uint16_t*)&i_data0 ;
      }
      else
      {
        i_w = 0 ;
        *(uint8_t*)&op_dest   = *(uint8_t*)&mem[ REGS_BASE + ( ( 2 * i_reg4bit + i_reg4bit / 4 ) & 0x07 ) ] ;
        *(uint8_t*)&op_source = *(uint8_t*)&i_data0 ;
        *(uint8_t*)&op_result = *(uint8_t*)&i_data0 ;
        *(uint8_t*)&mem[ REGS_BASE + ( ( 2 * i_reg4bit + i_reg4bit / 4 ) & 0x07 ) ] = *(uint8_t*)&i_data0 ;
      }
      break ;

    // PUSH regs16.
    case 0x03 :
      i_w = 1 ;
      op_dest   = *( uint16_t * ) &mem[ 16 * regs16[ REG_SS ] + ( uint16_t ) ( --regs16[ REG_SP ] ) ] ;
      op_source = *( uint16_t * ) &regs16[ i_reg4bit ] ;
      op_result = op_source ;
      *( uint16_t * ) &mem[ 16 * regs16[ REG_SS ] + ( uint16_t ) ( --regs16[ REG_SP ] ) ] = op_source ;
      break ;

    // POP regs16.
    case 0x04 :
      i_w = 1 ;
      regs16[ REG_SP ] += 2 ;
      op_dest   = *( uint16_t * ) &regs16[ i_reg4bit ] ;
      op_source = *( uint16_t * ) &(mem[ 16 * regs16[ REG_SS ] + ( uint16_t ) ( - 2 + regs16[ REG_SP ] ) ]) ;
      op_result = op_source ;
      *( uint16_t * ) &regs16[ i_reg4bit ] = op_source ;
      break ;

    // INC|DEC regs16
    case 0x02 :
      i_w   = 1 ;
      i_d   = 0 ;
      i_reg = i_reg4bit ;

      scratch2_uint = 4 * !i_mod ;
      if( i_mod < 3 )
      {
        uint16_t localIndex ;
        uint16_t localAddr  ;

        if( seg_override_en )
        {
          localIndex = seg_override ;
        }
        else
        {
          localIndex = bios_table_lookup[ scratch2_uint + 3 ][ i_rm ] ;
        }

        localAddr  = ( uint16_t ) regs16[ bios_table_lookup[ scratch2_uint + 1 ][ i_rm ] ] ;
        localAddr += ( uint16_t ) bios_table_lookup[ scratch2_uint + 2 ][ i_rm ] * i_data1 ;
        localAddr += ( uint16_t ) regs16[ bios_table_lookup[ scratch2_uint ][ i_rm ] ] ;
        rm_addr = ( 16 * regs16[ localIndex ] ) + localAddr ;
      }
      else
      {
        rm_addr = ( REGS_BASE + ( i_w ? ( 2 * i_rm ) : ( 2 * i_rm + i_rm / 4 ) & 7 ) ) ;
      }
      op_to_addr = rm_addr ;
      op_from_addr = (REGS_BASE + ( i_w ? ( 2 * i_reg ) : ( 2 * i_reg + i_reg / 4) & 7 ) );
      if( i_d )
      {
        scratch_uint = op_from_addr ;
        op_from_addr = rm_addr      ;
        op_to_addr   = scratch_uint ;
      }

      i_reg = stOpcode.extra ;

    // INC|DEC|JMP|CALL|PUSH
    case 0x05 :
      // INC|DEC
      if( i_reg < 2 )
      {
        MEM_OP( op_from_addr , += 1 - 2 * i_reg + , REGS_BASE + 2 * REG_ZERO ) ;
        op_source = 1 ;
        set_AF_OF_arith() ;
        set_OF( op_dest + 1 - i_reg == 1 << ( 8 * ( i_w + 1 ) - 1 ) ) ;
        if( stOpcode.xlat_opcode_id == 0x05 )
        {
          // Decode like ADC.
          set_opcode( 0x10 ) ;
        }
      }
      else if( i_reg != 6 ) // JMP|CALL
      {
        // CALL (far)
        if( ( i_reg - 3 ) == 0 )
        {
          R_M_PUSH( regs16[ REG_CS ] ) ;
        }

        // CALL (near or far)
        if( i_reg & 0x02 )
        {
          R_M_PUSH( reg_ip + 2 + i_mod * ( i_mod != 3 ) + 2 * ( !i_mod && i_rm == 6 ) ) ;
        }

        // JMP|CALL (far)
        if( i_reg & 0x01 )
        {
          ( regs16[ REG_CS ] = *( int16_t * )&mem[ op_from_addr + 2 ] ) ;
        }

        R_M_OP( reg_ip , = , mem[ op_from_addr ] ) ;

        // Decode like CALL
        set_opcode( 0x9A ) ;
      }
      else // PUSH
      {
        R_M_PUSH( mem[ rm_addr ] ) ;
      }
      break ;

    // TEST r/m, imm16 / NOT|NEG|MUL|IMUL|DIV|IDIV reg
    case 0x06 :
      op_to_addr = op_from_addr ;

      switch( i_reg )
      {
      // TEST
      case 0x00 :
        // Decode like AND
        set_opcode( 0x20 ) ;
        reg_ip += i_w + 1;
        R_M_OP( mem[ op_to_addr ] , & , i_data2 ) ;
        break ;

      // NOT
      case 0x02 :
        MEM_OP( op_to_addr , =~ , op_from_addr ) ;
        break ;

      // NEG
      case 0x03 :
        MEM_OP( op_to_addr , =- , op_from_addr ) ;
        op_dest = 0 ;

        // Decode like SUB
        set_opcode( 0x28 ) ;
        set_CF( op_result > op_dest ) ;
        break ;

      // MUL
      case 0x04 :
        if( i_w )
        {
          MUL_MACRO( uint16_t , regs16 ) ;
        }
        else
        {
          MUL_MACRO( uint8_t , regs8 ) ;
        }
        break ;

      // IMUL
      case 0x05 :
        if( i_w )
        {
          MUL_MACRO( int16_t , regs16 ) ;
        }
        else
        {
          MUL_MACRO( int8_t , regs8 ) ;
        }
        break ;

      // DIV
      case 0x06 :
        if( i_w )
        {
          DIV_MACRO( uint16_t , uint32_t , regs16 ) ;
        }
        else
        {
          DIV_MACRO( uint8_t , uint16_t , regs8 ) ;
        }
        break ;

      // IDIV
      case 0x07 :
        if( i_w )
        {
          DIV_MACRO( int16_t , int32_t , regs16 ) ;
        }
        else
        {
          DIV_MACRO( int8_t, int16_t, regs8);
        }
        break ;
      }
      break ;

    // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP AL/AX, immed
    case 0x07 :
      rm_addr = REGS_BASE ;
      i_data2 = i_data0   ;
      i_mod   = 3         ;
      i_reg   = stOpcode.extra ;
      reg_ip-- ;

    // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP reg, immed
    case 0x08 :
      op_to_addr = rm_addr ;
      i_d |= !i_w ;
      if( i_d )
      {
        regs16[ REG_SCRATCH ] = ( int8_t ) i_data2 ;
      }
      else
      {
        regs16[ REG_SCRATCH ] = i_data2 ;
      }

      op_from_addr = REGS_BASE + 2 * REG_SCRATCH ;
      reg_ip += ( !i_d + 1 ) ;
      stOpcode.extra = i_reg ;
      set_opcode( 0x08 * i_reg ) ;

    // ADD|OR|ADC|SBB|AND|SUB|XOR|CMP|MOV reg, r/m
    case 0x09 :
      switch( stOpcode.extra )
      {
        // ADD
        case 0x00 :
          MEM_OP( op_to_addr ,+= , op_from_addr ) ;
          set_CF( op_result < op_dest ) ;
          break ;

        // OR
        case 0x01 :
          MEM_OP( op_to_addr , |= , op_from_addr ) ;
          break ;

        // ADC
        case 0x02 :
          ADC_SBB_MACRO( + ) ;
          break ;

        // SBB
        case 0x03 :
          ADC_SBB_MACRO( - ) ;
          break ;

        // AND
        case 0x04 :
          MEM_OP( op_to_addr , &= , op_from_addr ) ;
          break ;

        // SUB
        case 0x05 :
          MEM_OP( op_to_addr , -= , op_from_addr ) ;
          set_CF( op_result > op_dest ) ;
          break ;

        // XOR
        case 0x06 :
          MEM_OP( op_to_addr , ^= , op_from_addr ) ;
          break ;

        // CMP
        case 0x07 :
          MEM_OP( op_to_addr , - , op_from_addr ) ;
          set_CF( op_result > op_dest ) ;
          break ;

        // MOV
        case 0x08 :
          MEM_MOV( op_to_addr , op_from_addr ) ;
          break ;
        }
        break ;

      // MOV sreg, r/m | POP r/m | LEA reg, r/m
      case 0x0A :
        // MOV
        if( !i_w )
        {
          i_w = 1,
          i_reg += 8,

          scratch2_uint = 4 * !i_mod ;
          if( i_mod < 3 )
          {
            uint16_t localIndex ;
            uint16_t localAddr  ;

            if( seg_override_en )
            {
              localIndex = seg_override ;
            }
            else
            {
              localIndex = bios_table_lookup[ scratch2_uint + 3 ][ i_rm ] ;
            }

            localAddr  = ( uint16_t ) regs16[ bios_table_lookup[ scratch2_uint + 1 ][ i_rm ] ] ;
            localAddr += ( uint16_t ) bios_table_lookup[ scratch2_uint + 2 ][ i_rm ] * i_data1 ;
            localAddr += ( uint16_t ) regs16[ bios_table_lookup[ scratch2_uint ][ i_rm ] ] ;
            rm_addr = ( 16 * regs16[ localIndex ] ) + localAddr ;
          }
          else
          {
            rm_addr = ( REGS_BASE + ( ( i_w ) ? ( 2 * i_rm ) : ( 2 * i_rm + i_rm / 4 ) & 7 ) ) ;
          }
          op_to_addr = rm_addr ;
          op_from_addr = (REGS_BASE + ( ( i_w ) ? ( 2 * i_reg ) : ( 2 * i_reg + i_reg / 4 ) & 7 ) ) ;
          if( i_d )
          {
            scratch_uint = op_from_addr ;
            op_from_addr = rm_addr      ;
            op_to_addr   = scratch_uint ;
          }

          MEM_OP( op_to_addr , = , op_from_addr ) ;
        }
        else if( !i_d ) // LEA
        {
          seg_override_en = 1 ;
          seg_override = REG_ZERO ;

          scratch2_uint = 4 * !i_mod ;
          if( i_mod < 3 )
          {
            uint16_t localIndex ;
            uint16_t localAddr  ;

            if( seg_override_en )
            {
              localIndex = seg_override ;
            }
            else
            {
              localIndex = bios_table_lookup[ scratch2_uint + 3 ][ i_rm ] ;
            }

            localAddr  = ( uint16_t ) regs16[ bios_table_lookup[ scratch2_uint + 1 ][ i_rm ] ] ;
            localAddr += ( uint16_t ) bios_table_lookup[ scratch2_uint + 2 ][ i_rm ] * i_data1 ;
            localAddr += ( uint16_t ) regs16[ bios_table_lookup[ scratch2_uint ][ i_rm ] ] ;
            rm_addr = ( 16 * regs16[ localIndex ] ) + localAddr ;
          }
          else
          {
            rm_addr = ( REGS_BASE + ( ( i_w ) ? ( 2 * i_rm ) : ( 2 * i_rm + i_rm / 4 ) & 7 ) ) ;
          }
          op_to_addr = rm_addr ;
          op_from_addr = ( REGS_BASE + ( ( i_w ) ? ( 2 * i_reg ) : ( 2 * i_reg + i_reg / 4 ) & 7 ) ) ;
          if( i_d )
          {
            scratch_uint = op_from_addr ;
            op_from_addr = rm_addr      ;
            op_to_addr   = scratch_uint ;
          }

          R_M_MOV( mem[ op_from_addr ] , rm_addr ) ;
        }
        else // POP
        {
          R_M_POP( mem[ rm_addr ] ) ;
        }
        break ;

      // MOV AL/AX, [loc]
      case 0x0B :
        i_mod = 0 ;
        i_reg = 0 ;
        i_rm  = 6 ;
        i_data1 = i_data0 ;

        scratch2_uint = 4 * !i_mod ;
        if( i_mod < 3 )
        {
          uint16_t localIndex ;
          uint16_t localAddr  ;

          if( seg_override_en )
          {
            localIndex = seg_override ;
          }
          else
          {
            localIndex = bios_table_lookup[ scratch2_uint + 3 ][ i_rm ] ;
          }

          localAddr  = ( uint16_t ) regs16[ bios_table_lookup[ scratch2_uint + 1 ][ i_rm ] ] ;
          localAddr += ( uint16_t ) bios_table_lookup[ scratch2_uint + 2 ][ i_rm ] * i_data1 ;
          localAddr += ( uint16_t ) regs16[ bios_table_lookup[ scratch2_uint ][ i_rm ] ] ;
          rm_addr = ( 16 * regs16[ localIndex ] ) + localAddr ;
        }
        else
        {
          rm_addr = ( REGS_BASE + ( ( i_w ) ? ( 2 * i_rm ) : ( 2 * i_rm + i_rm / 4 ) & 7 ) ) ;
        }
        op_to_addr = rm_addr ;
        op_from_addr = ( REGS_BASE + ( ( i_w ) ? ( 2 * i_reg ) : ( 2 * i_reg + i_reg / 4 ) & 7 ) ) ;
        if( i_d )
        {
          scratch_uint = op_from_addr ;
          op_from_addr = rm_addr      ;
          op_to_addr   = scratch_uint ;
        }

        MEM_MOV( op_from_addr , op_to_addr ) ;
        break ;

      // ROL|ROR|RCL|RCR|SHL|SHR|???|SAR reg/mem, 1/CL/imm (80186)
      case 0x0C :

        // Returns sign bit of an 8-bit or 16-bit operand.
        if( i_w )
        {
          scratch2_uint = *( int16_t * )&( mem[ rm_addr ] ) ;
          scratch2_uint >>= 15 ;
        }
        else
        {
          scratch2_uint = ( mem[ rm_addr ] ) ;
          scratch2_uint >>= 7 ;
        }
        scratch2_uint &= 1 ;

        if( stOpcode.extra )
        {
          // xxx reg/mem, imm
          scratch_uint = ( int8_t ) i_data1 ;
        }
        else if( i_d )
        {
          // xxx reg/mem, CL
          scratch_uint = 31 & regs8[REG_CL] ;
        }
        else
        {
          // xxx reg/mem, 1
          scratch_uint = 1 ;
        }

        if( scratch_uint )
        {
          if( i_reg < 4 ) // Rotate operations
          {
            scratch_uint %= i_reg / 2 + 8 * ( i_w + 1 ) ;
            R_M_OP( scratch2_uint , = , mem[ rm_addr ] ) ;
          }

          if( i_reg & 1 ) // Rotate/shift right operations
          {
            R_M_OP( mem[ rm_addr ] , >>= , scratch_uint ) ;
          }
          else // Rotate/shift left operations
          {
            R_M_OP( mem[ rm_addr ] , <<= , scratch_uint ) ;
          }

          // Shift operations
          if( i_reg > 3 )
          {
            // Shift instructions affect SZP
            stOpcode.set_flags_type = FLAGS_UPDATE_SZP ;
          }

          // SHR or SAR
          if( i_reg > 4 )
          {
            set_CF( op_dest >> ( scratch_uint - 1 ) & 1 ) ;
          }
        }

        switch( i_reg )
        {
        // ROL
        case 0x00 :
          R_M_OP( mem[ rm_addr ] , += , scratch2_uint >> ( 8 * ( i_w + 1 ) - scratch_uint ) ) ;

          // Returns sign bit of an 8-bit or 16-bit operand
          set_OF( ( 1 & ( ( i_w ) ? *( int16_t * )&( op_result ) : ( op_result ) ) >> ( 8 * ( i_w + 1 ) - 1 ) ) ^ set_CF( op_result & 1 ) ) ;
          break ;

        // ROR
        case 0x01 :
          scratch2_uint &= ( 1 << scratch_uint ) - 1 ;
          R_M_OP( mem[ rm_addr ] , += , scratch2_uint << ( 8 * ( i_w + 1 ) - scratch_uint ) ) ;

          if( i_w )
          {
            set_OF( ( 1 & ( *( int16_t * )&op_result * 2 ) >> 15 ) ^ set_CF( 1 & ( *( int16_t * )&( op_result ) ) >> 15 ) ) ;
          }
          else
          {
            set_OF( ( 1 & ( op_result * 2 ) >> 7 ) ^ set_CF( 1 & ( op_result ) >> 7 ) ) ;
          }
          break ;

        // RCL
        case 0x02 :
          R_M_OP( mem[ rm_addr ] , += ( regs8[ FLAG_CF ] << ( scratch_uint - 1 ) ) + , scratch2_uint >> ( 1 + 8 * ( i_w + 1 ) - scratch_uint ) ) ;
          if( i_w )
          {
            set_OF( ( 1 & *( int16_t * )&( op_result ) >> 15 ) ^ set_CF( scratch2_uint & 1 << ( 16 - scratch_uint ) ) ) ;
          }
          else
          {
            set_OF( ( ( 1 & op_result ) >> 7 ) ^ set_CF( scratch2_uint & 1 << ( 8 - scratch_uint ) ) ) ;
          }
          break ;

        // RCR
        case 0x03 :
          if( i_w )
          {
            R_M_OP( mem[ rm_addr ] , += ( regs8[ FLAG_CF ] << ( 16 - scratch_uint ) ) + , scratch2_uint << ( 17 - scratch_uint ) ) ;
            set_CF( scratch2_uint & 1 << ( scratch_uint - 1 ) ) ;
            set_OF( ( 1 & *( int16_t * )&( op_result ) >> 15 ) ^ ( 1 & *( int16_t * )&op_result * 2 >> 15 ) ) ;
          }
          else
          {
            R_M_OP( mem[ rm_addr ] , += ( regs8[ FLAG_CF ] << ( 8 - scratch_uint ) ) + , scratch2_uint << ( 9 - scratch_uint ) ) ;
            set_CF( scratch2_uint & 1 << ( scratch_uint - 1 ) ) ;
            set_OF( ( 1 & op_result >> 7 ) ^ ( 1 & ( op_result * 2 ) >> 7 ) ) ;
          }
          break ;

        // SHL
        case 0x04 :
          if( i_w )
          {
            set_OF( ( 1 & *( int16_t * )&( op_result ) >> 15 ) ^ set_CF( ( 1 & *( int16_t * )&op_dest << ( scratch_uint - 1 ) ) >> 15 ) ) ;
          }
          else
          {
            set_OF( ( 1 & op_result >> 7 ) ^ set_CF( ( 1 & ( op_dest << ( scratch_uint - 1 ) ) >> 7 ) ) ) ;
          }
          break ;

        // SHR
        case 0x05 :
          if( i_w )
          {
            set_OF( 1 & *( int16_t * )&( op_dest ) >> 15 ) ;
          }
          else
          {
            set_OF( ( 1 & ( op_dest ) >> 7 ) ) ;
          }
          break ;

        // SAR
        case 0x07 :
          if( !( scratch_uint < 8 * ( i_w + 1 ) ) )
          {
            set_CF( scratch2_uint ) ;
          }
          set_OF( 0 ) ;
          R_M_OP( mem[ rm_addr ] , += , scratch2_uint *= ~( ( ( 1 << 8*( i_w + 1 ) ) - 1 ) >> scratch_uint ) ) ;
          break ;
        }
        break ;

      case 13: // LOOPxx|JCZX
                scratch_uint = !!--regs16[REG_CX];

                switch(i_reg4bit)
                {
                    ; case 0: // LOOPNZ
                        scratch_uint &= !regs8[FLAG_ZF]
                    ;break; case 1: // LOOPZ
                        scratch_uint &= regs8[FLAG_ZF]
                    ;break; case 3: // JCXXZ
                        scratch_uint = !++regs16[REG_CX];
                }
                reg_ip += scratch_uint*(int8_t)i_data0
            ;break; case 14: // JMP | CALL short/near
                reg_ip += 3 - i_d;
                if (!i_w)
                {
                    if (i_d) // JMP far
                        reg_ip = 0,
                        regs16[REG_CS] = i_data2;
                    else // CALL
                        R_M_PUSH(reg_ip);
                }
                reg_ip += i_d && i_w ? (int8_t)i_data0 : i_data0
            ;break; case 15: // TEST reg, r/m
                MEM_OP(op_from_addr, &, op_to_addr)
            ;break; case 16: // XCHG AX, regs16
                i_w = 1;
                op_to_addr   = REGS_BASE ;
                op_from_addr = ( REGS_BASE + ( 2 * i_reg4bit ) ) ;

            // NOP|XCHG reg, r/m
            case 24 :
                if( op_to_addr != op_from_addr )
                {
                    MEM_OP( op_to_addr   , ^= , op_from_addr ) ;
                    MEM_OP( op_from_addr , ^= , op_to_addr   ) ;
                    MEM_OP( op_to_addr   , ^= , op_from_addr ) ;
                }
                break ;

            case 17: // MOVSx (extra=0)|STOSx (extra=1)|LODSx (extra=2)
                scratch2_uint = seg_override_en ? seg_override : REG_DS;

                for (scratch_uint = rep_override_en ? regs16[REG_CX] : 1; scratch_uint; scratch_uint--)
                {
                    MEM_MOV(stOpcode.extra < 2 ? SEGREG(REG_ES, REG_DI) : REGS_BASE, stOpcode.extra & 1 ? REGS_BASE : SEGREG(scratch2_uint, REG_SI)),
                    stOpcode.extra & 1 || INDEX_INC(REG_SI),
                    stOpcode.extra & 2 || INDEX_INC(REG_DI);
                }

                if (rep_override_en)
                    regs16[REG_CX] = 0
            ;break; case 18: // CMPSx (extra=0)|SCASx (extra=1)
                scratch2_uint = seg_override_en ? seg_override : REG_DS;

                if ((scratch_uint = rep_override_en ? regs16[REG_CX] : 1))
                {
                    for (; scratch_uint; rep_override_en || scratch_uint--)
                    {
                        MEM_OP(stOpcode.extra ? REGS_BASE : SEGREG(scratch2_uint, REG_SI), -, SEGREG(REG_ES, REG_DI)),
                        stOpcode.extra || INDEX_INC(REG_SI),
                        INDEX_INC(REG_DI), rep_override_en && !(--regs16[REG_CX] && (!op_result == rep_mode)) && (scratch_uint = 0);
                    }

                    stOpcode.set_flags_type = FLAGS_UPDATE_SZP | FLAGS_UPDATE_AO_ARITH; // Funge to set SZP/AO flags
                    set_CF(op_result > op_dest);
                }
            ;break; case 19: // RET|RETF|IRET
                i_d = i_w;
                R_M_POP(reg_ip);
                if (stOpcode.extra) // IRET|RETF|RETF imm16
                    R_M_POP(regs16[REG_CS]);
                if (stOpcode.extra & 2) // IRET
                    set_flags(R_M_POP(scratch_uint));
                else if (!i_d) // RET|RETF imm16
                  regs16[REG_SP] += i_data0
            ;break; case 20: // MOV r/m, immed
        //R_M_OP(mem[op_from_addr], =, i_data2)
        regs16[REG_TMP] = i_data2;
        MEM_MOV(op_from_addr, REGS_BASE + REG_TMP * 2)
          ;break; case 21: // IN AL/AX, DX/imm8
                scratch_uint = stOpcode.extra ? regs16[REG_DX] : (uint8_t)i_data0;
        io_ports[scratch_uint] = Interface.ReadPort(scratch_uint);
                if (i_w)
        {
          io_ports[scratch_uint+1] = Interface.ReadPort(scratch_uint+1);
        }
                R_M_OP(regs8[REG_AL], =, io_ports[scratch_uint]);
            ;break; case 22: // OUT DX/imm8, AL/AX
              scratch_uint = stOpcode.extra ? regs16[REG_DX] : (uint8_t)i_data0;
                R_M_OP(io_ports[scratch_uint], =, regs8[REG_AL]);
        Interface.WritePort(scratch_uint, io_ports[scratch_uint]);
        if (i_w)
        {
          Interface.WritePort(scratch_uint+1, io_ports[scratch_uint+1]);
        }
            ;break; case 23: // REPxx
                rep_override_en = 2;
                rep_mode = i_w;
                seg_override_en && seg_override_en++
            ;break; case 25: // PUSH reg
                R_M_PUSH(regs16[stOpcode.extra])
            ;break; case 26: // POP reg
                R_M_POP(regs16[stOpcode.extra])
            ;break; case 27: // xS: segment overrides
                seg_override_en = 2;
                seg_override = stOpcode.extra;
                rep_override_en && rep_override_en++
            ;break; case 28: // DAA/DAS
                i_w = 0;
        // extra = 0 for DAA, 1 for DAS
                if (stOpcode.extra) DAA_DAS(-=, >); else DAA_DAS(+=, <)
            ;break; case 29: // AAA/AAS
                op_result = AAA_AAS(stOpcode.extra - 1)
            ;break; case 30: // CBW
                regs8[REG_AH] = -( (1 & (i_w ? *(int16_t*)&(regs8[REG_AL]) : (regs8[REG_AL])) >> (8*(i_w + 1) - 1)) )
            ;break; case 31: // CWD
                regs16[REG_DX] = -( (1 & (i_w ? *(int16_t*)&(regs16[REG_AX]) : (regs16[REG_AX])) >> (8*(i_w + 1) - 1)) )
            ;break; case 32: // CALL FAR imm16:imm16
                R_M_PUSH(regs16[REG_CS]);
                R_M_PUSH(reg_ip + 5);
                regs16[REG_CS] = i_data2;
                reg_ip = i_data0
            ;break; case 33: // PUSHF
                make_flags();
                R_M_PUSH(scratch_uint)
            ;break; case 34: // POPF
                set_flags(R_M_POP(scratch_uint))
            ;break; case 35: // SAHF
                make_flags();
                set_flags((scratch_uint & 0xFF00) + regs8[REG_AH])
            ;break; case 36: // LAHF
                make_flags(),
                regs8[REG_AH] = scratch_uint
            ;break; case 37: // LES|LDS reg, r/m
                i_w = i_d = 1;

        scratch2_uint = 4 * !i_mod ;
        if( i_mod < 3 )
        {
          uint16_t localIndex ;
          uint16_t localAddr  ;

          if( seg_override_en )
          {
            localIndex = seg_override ;
          }
          else
          {
            localIndex = bios_table_lookup[ scratch2_uint + 3 ][ i_rm ] ;
          }

          localAddr  = ( uint16_t ) regs16[ bios_table_lookup[ scratch2_uint + 1 ][ i_rm ] ] ;
          localAddr += ( uint16_t ) bios_table_lookup[ scratch2_uint + 2 ][ i_rm ] * i_data1 ;
          localAddr += ( uint16_t ) regs16[ bios_table_lookup[ scratch2_uint ][ i_rm ] ] ;
          rm_addr = ( 16 * regs16[ localIndex ] ) + localAddr ;
        }
        else
        {
          rm_addr = (REGS_BASE + (i_w ? 2 * i_rm : (2 * i_rm + i_rm / 4) & 7)) ;
        }
        op_to_addr = rm_addr ;
        op_from_addr = (REGS_BASE + (i_w ? 2 * i_reg : (2 * i_reg + i_reg / 4) & 7));
        if( i_d )
        {
          scratch_uint = op_from_addr ;
          op_from_addr = rm_addr      ;
          op_to_addr   = scratch_uint ;
        }

                MEM_OP( op_to_addr , = ,op_from_addr ) ;
                MEM_OP(REGS_BASE + stOpcode.extra, =, rm_addr + 2)
            ;break; case 38: // INT 3
                ++reg_ip;
                pc_interrupt(3)
            ;break; case 39: // INT imm8
                reg_ip += 2;
                pc_interrupt(i_data0)
            ;break; case 40: // INTO
                ++reg_ip;
                regs8[FLAG_OF] && pc_interrupt(4)
            ;break; case 41: // AAM;
                if (i_data0 &= 0xFF)
                    regs8[REG_AH] = regs8[REG_AL] / i_data0,
                    op_result = regs8[REG_AL] %= i_data0;
                else // Divide by zero
                    pc_interrupt(0);
            ;break; case 42: // AAD
                i_w = 0;
                regs16[REG_AX] = op_result = 0xFF & (regs8[REG_AL] + i_data0 * regs8[REG_AH])
            ;break; case 43: // SALC
                regs8[REG_AL] = -regs8[FLAG_CF]
            ;break; case 44: // XLAT
                regs8[REG_AL] = mem[SEGREG_OP(seg_override_en ? seg_override : REG_DS, REG_BX, regs8[REG_AL] +)]
            ;break; case 45: // CMC
                regs8[FLAG_CF] ^= 1
            ;break; case 46: // CLC|STC|CLI|STI|CLD|STD
                regs8[stOpcode.extra / 2] = stOpcode.extra & 1
            ;break; case 47: // TEST AL/AX, immed
                R_M_OP(regs8[REG_AL], &, i_data0)
      ;break; case 48: // LOCK:
      ;break; case 49: // HLT
            ;break;

            // Emulator-specific 0F xx opcodes
            case 50 :
                switch( ( int8_t ) i_data0 )
                {
                  // PUTCHAR_AL.
                    case 0 :
                      //write(1, regs8, 1)
                        putchar( regs8[ 0 ] ) ;
                      break ;

          // GET_RTC
          case 1:
                        time( &clock_buf ) ;
                        ftime( &ms_clock ) ;
                        memcpy( mem + SEGREG(REG_ES, REG_BX), localtime(&clock_buf), sizeof(struct tm));
                        *(int16_t*)&mem[SEGREG_OP(REG_ES, REG_BX, 36+)] = ms_clock.millitm;
                      break ;

          // DISK_READ
          case 2 :
          // DISK_WRITE
                    case 3 :
                        regs8[ REG_AL ] = ~lseek( disk[ regs8[ REG_DL ] ] , *(unsigned*)&regs16[REG_BP] << 9 , 0 ) ?
              ((int8_t)i_data0 == 3 ? (int(*)(int, const void *, int))write :(int(*)(int, const void *, int))read)(disk[regs8[REG_DL]], mem + SEGREG(REG_ES, REG_BX), regs16[REG_AX])
                            : 0;
                }
      ;break; case 51: // 80186, NEC V20: ENTER
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
      ;break; case 52: // 80186, NEC V20: LEAVE
        regs16[REG_SP] = regs16[REG_BP];
        R_M_POP(regs16[REG_BP])
      ;break; case 53: // 80186, NEC V20: PUSHA
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
      ;break; case 54: // 80186, NEC V20: POPA
        // POP DI, POP SI, POP BP, ADD SP,2, POP BX, POP DX, POP CX, POP AX
        R_M_POP(regs16[REG_DI]);
        R_M_POP(regs16[REG_SI]);
        R_M_POP(regs16[REG_BP]);
        regs16[REG_SP] += 2;
        R_M_POP(regs16[REG_BX]);
        R_M_POP(regs16[REG_DX]);
        R_M_POP(regs16[REG_CX]);
        R_M_POP(regs16[REG_AX])
      ;break; case 55: // 80186: BOUND
        // not implemented. Incompatible with PC/XT hardware
        printf("BOUND\n")
      ;break; case 56: // 80186, NEC V20: PUSH imm16
        R_M_PUSH(i_data0)
      ;break; case 57: // 80186, NEC V20: PUSH imm8
        R_M_PUSH(i_data0 & 0x00ff)
      ;break; case 58: // 80186 IMUL
        // not implemented
        printf("IMUL at %04X:%04X\n", regs16[REG_CS], reg_ip)
      ;break; case 59: // 80186: INSB INSW
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

      ;break; case 60: // 80186: OUTSB OUTSW
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

      ;break; case 69: // 8087 MATH Coprocessor
        printf("8087 coprocessor instruction: 0x%02X\n", stOpcode.raw_opcode_id)
      ;break; case 70: // 80286+
        printf("80286+ only op code: 0x%02X at %04X:%04X\n", stOpcode.raw_opcode_id, regs16[REG_CS], reg_ip)
      ;break; case 71: // 80386+
        printf("80386+ only op code: 0x%02X at %04X:%04X\n", stOpcode.raw_opcode_id, regs16[REG_CS], reg_ip)
      ;break; case 72: // BAD OP CODE
        printf("Bad op code: %02x  at %04X:%04X\n", stOpcode.raw_opcode_id, regs16[REG_CS], reg_ip);
        }

        // Increment instruction pointer by computed instruction length. Tables in the BIOS binary
        // help us here.
        reg_ip += (i_mod*(i_mod != 3) + 2*(!i_mod && i_rm == 6))* stOpcode.i_mod_size ;
        reg_ip += bios_table_lookup[TABLE_BASE_INST_SIZE][ stOpcode.raw_opcode_id] ;
        reg_ip += bios_table_lookup[TABLE_I_W_SIZE][stOpcode.raw_opcode_id]*(i_w + 1);

        // If instruction needs to update SF, ZF and PF, set them as appropriate
        if (stOpcode.set_flags_type & FLAGS_UPDATE_SZP)
        {
            // Returns sign bit of an 8-bit or 16-bit operand
            regs8[FLAG_SF] = (1 & (i_w ? *(int16_t*)&(op_result) : (op_result)) >> (8*(i_w + 1) - 1));

            regs8[FLAG_ZF] = !op_result;
            regs8[FLAG_PF] = bios_table_lookup[TABLE_PARITY_FLAG][(uint8_t)op_result];

            // If instruction is an arithmetic or logic operation, also set AF/OF/CF as appropriate.
            if (stOpcode.set_flags_type & FLAGS_UPDATE_AO_ARITH)
                set_AF_OF_arith();
            if (stOpcode.set_flags_type & FLAGS_UPDATE_OC_LOGIC)
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
      }
    }

    } // for each instruction


    Interface.Cleanup();

    return 0;
}

// =============================================================================
// File: win32_debug.cpp
//
// Description:
// Win32 debug dialog.
// Shows register state, disassembly and memory view.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#include <stdio.h>

#include "win32_debug.h"
#include "file_dialog.h"
#include "resource.h"

#include "debug_disasm.h"

// These must match 8086tiny.

#define RAM_SIZE 0x10FFF0

#define REGS_BASE 0xF0000

// index into regs16
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

#define REG_IP 14

// index into regs8
#define FLAG_CF 40
#define FLAG_PF 41
#define FLAG_AF 42
#define FLAG_ZF 43
#define FLAG_SF 44
#define FLAG_TF 45
#define FLAG_IF 46
#define FLAG_DF 47
#define FLAG_OF 48

// =============================================================================
// Local variables
//
static HWND DbgHwnd = 0;
static DebugState_t DbgState = DEBUG_NONE;

static int DisassTabStops[] =
  { 12 };

static int MemoryTabStops[] =
  { 48, 60, 72, 84, 96, 108, 120, 132, 144, 156, 168, 180, 192, 204, 216, 228 };

static unsigned char *mem;
static unsigned char *regs8;
static unsigned short *regs16;

static int disass_seg = 0;
static int disass_off = 0;

static int showmem_seg = 0;
static int showmem_off = 0;

static HWND ConfigParent = 0;
static bool LastPosSet = false;
static int LastDialogX = 0;
static int LastDialogY = 0;

// Breakpoints
#define DISASS_NUM_LINES 32

static bool BreakPointSet[RAM_SIZE];
static int DisassLineAddr[DISASS_NUM_LINES];

// Instruction trace
#define TRACE_BUFFER_LENGTH 65536

static bool TraceEnabled = false;
static int TraceLength = 0;
static int TracePos = 0;
static int TraceCS[TRACE_BUFFER_LENGTH];
static int TraceIP[TRACE_BUFFER_LENGTH];

// =============================================================================
// Local Functions
//

static void Breakpoint_ClearAll(void)
{
  for (int i = 0 ; i < RAM_SIZE ; i++)
  {
    BreakPointSet[i] = false;
  }

  if (TraceEnabled)
  {
    BreakpointCount = 1;
  }
  else
  {
    BreakpointCount = 0;
  }
}

static void DEBUG_SaveTrace(const char *filename)
{
  FILE *fp;
  int TraceIdx;
  char DASMBuffer[64];
  int dasm_addr;

  fp = fopen(filename, "wt");

  if (fp == NULL) return;

  if (TraceLength < TRACE_BUFFER_LENGTH)
  {
    TraceIdx = 0;
  }
  else
  {
    TraceIdx = TracePos;
  }

  for (int i = 0 ; i < TraceLength ; i++)
  {
    dasm_addr = 16 * TraceCS[TraceIdx] + TraceIP[TraceIdx];

    DasmI386(DASMBuffer, mem, dasm_addr, TraceIP[TraceIdx], false);

    fprintf(fp, "%c\t%04X:%04X   %s\n",
            BreakPointSet[dasm_addr] ? 'B' : ' ',
            TraceCS[TraceIdx], TraceIP[TraceIdx], DASMBuffer);

    TraceIdx = (TraceIdx + 1) % TRACE_BUFFER_LENGTH;
  }

  fclose(fp);
}

static void DEBUG_UpdateDisassembly(HWND hwnd)
{
  char RegText[16];
  char DASMBuffer[64];
  char ASMText[128];
  unsigned short reg_ip;
  unsigned int instr_size;
  int dasm_addr;

  sprintf(RegText, "%04X:%04X", disass_seg, disass_off);
  SendDlgItemMessage(hwnd, IDC_DISASS_ADDRESS, WM_SETTEXT, 0, (LPARAM) RegText);

  reg_ip = disass_off;

  for (int i = 0 ; i < DISASS_NUM_LINES ; i++)
  {
    SendDlgItemMessage(hwnd, IDC_LIST_ASM, LB_DELETESTRING, 0, 0);
  }
  SendDlgItemMessage(hwnd, IDC_LIST_ASM, LB_SETTABSTOPS , 1, (LPARAM) DisassTabStops);

  for (int i = 0 ; i < DISASS_NUM_LINES ; i++)
  {
    dasm_addr = 16 * disass_seg + reg_ip;
    DisassLineAddr[i] = dasm_addr;

    instr_size = DasmI386(DASMBuffer, mem, dasm_addr, reg_ip, false);

    sprintf(ASMText, "%c\t%04X:%04X   %s",
            BreakPointSet[dasm_addr] ? 'B' : ' ',
            disass_seg, reg_ip, DASMBuffer);
    SendDlgItemMessage(hwnd, IDC_LIST_ASM, LB_ADDSTRING, 0, (LPARAM) ASMText);
    reg_ip += instr_size;
  }

}

static void DEBUG_UpdateMemDump(HWND hwnd)
{
  char RegText[16];
  char Buffer[128];

  sprintf(RegText, "%04X:%04X", showmem_seg, showmem_off);
  SendDlgItemMessage(hwnd, IDC_MEMORY_ADDRESS, WM_SETTEXT, 0, (LPARAM) RegText);
  unsigned int memaddr = showmem_seg * 16 + showmem_off;

  for (int i = 0 ; i < 64 ; i++)
  {
    SendDlgItemMessage(hwnd, IDC_LIST_MEMORY, LB_DELETESTRING, 0, 0);
  }
  SendDlgItemMessage(hwnd, IDC_LIST_MEMORY, LB_SETTABSTOPS , 16, (LPARAM) MemoryTabStops);
  for (int i = 0 ; i < 1024 ; i+= 16)
  {
    sprintf(
      Buffer,
      "%04X:%04x :\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x\t%02x",
      showmem_seg, (showmem_off + i) % 0xFFFF,
      mem[memaddr+i], mem[memaddr+i+1], mem[memaddr+i+2], mem[memaddr+i+3],
      mem[memaddr+i+4], mem[memaddr+i+5], mem[memaddr+i+6], mem[memaddr+i+7],
      mem[memaddr+i+8], mem[memaddr+i+9], mem[memaddr+i+10], mem[memaddr+i+11],
      mem[memaddr+i+12], mem[memaddr+i+13], mem[memaddr+i+14], mem[memaddr+i+15]);
    SendDlgItemMessage(hwnd, IDC_LIST_MEMORY, LB_ADDSTRING, 0, (LPARAM) Buffer);
  }
}

static void DEBUG_UpdateControls(HWND hwnd)
{
  char RegText[16];

  // Update register states
  sprintf(RegText, "%04X", regs16[REG_CS]);
  SendDlgItemMessage(hwnd, IDC_EDIT_CS, WM_SETTEXT, 0, (LPARAM) RegText);
  sprintf(RegText, "%04X", regs16[REG_SS]);
  SendDlgItemMessage(hwnd, IDC_EDIT_SS, WM_SETTEXT, 0, (LPARAM) RegText);
  sprintf(RegText, "%04X", regs16[REG_DS]);
  SendDlgItemMessage(hwnd, IDC_EDIT_DS, WM_SETTEXT, 0, (LPARAM) RegText);
  sprintf(RegText, "%04X", regs16[REG_ES]);
  SendDlgItemMessage(hwnd, IDC_EDIT_ES, WM_SETTEXT, 0, (LPARAM) RegText);

  sprintf(RegText, "%04X", regs16[REG_AX]);
  SendDlgItemMessage(hwnd, IDC_EDIT_AX, WM_SETTEXT, 0, (LPARAM) RegText);
  sprintf(RegText, "%04X", regs16[REG_BX]);
  SendDlgItemMessage(hwnd, IDC_EDIT_BX, WM_SETTEXT, 0, (LPARAM) RegText);
  sprintf(RegText, "%04X", regs16[REG_CX]);
  SendDlgItemMessage(hwnd, IDC_EDIT_CX, WM_SETTEXT, 0, (LPARAM) RegText);
  sprintf(RegText, "%04X", regs16[REG_DX]);
  SendDlgItemMessage(hwnd, IDC_EDIT_DX, WM_SETTEXT, 0, (LPARAM) RegText);

  sprintf(RegText, "%04X", regs16[REG_SP]);
  SendDlgItemMessage(hwnd, IDC_EDIT_SP, WM_SETTEXT, 0, (LPARAM) RegText);
  sprintf(RegText, "%04X", regs16[REG_BP]);
  SendDlgItemMessage(hwnd, IDC_EDIT_BP, WM_SETTEXT, 0, (LPARAM) RegText);
  sprintf(RegText, "%04X", regs16[REG_SI]);
  SendDlgItemMessage(hwnd, IDC_EDIT_SI, WM_SETTEXT, 0, (LPARAM) RegText);
  sprintf(RegText, "%04X", regs16[REG_DI]);
  SendDlgItemMessage(hwnd, IDC_EDIT_DI, WM_SETTEXT, 0, (LPARAM) RegText);

  RegText[0] = 0;
  strcat(RegText, (regs8[FLAG_CF]) ? " C" : " -");
  strcat(RegText, (regs8[FLAG_ZF]) ? " Z" : " -");
  strcat(RegText, (regs8[FLAG_SF]) ? " S" : " -");
  strcat(RegText, (regs8[FLAG_OF]) ? " O" : " -");
  strcat(RegText, (regs8[FLAG_PF]) ? " P" : " -");
  strcat(RegText, (regs8[FLAG_AF]) ? " A" : " -");
  SendDlgItemMessage(hwnd, IDC_STATIC_FLAGS, WM_SETTEXT, 0, (LPARAM) RegText);

  SendDlgItemMessage(hwnd,
                     IDC_ENABLE_TRACE,
                     BM_SETCHECK,
                     (TraceEnabled) ? BST_CHECKED : BST_UNCHECKED,
                     (LPARAM) 0);


  // Update disassembly
  disass_seg = regs16[REG_CS];
  disass_off = regs16[REG_IP];

  DEBUG_UpdateDisassembly(hwnd);

  // update RAM display
  DEBUG_UpdateMemDump(hwnd);
}

// =============================================================================
// FUNCTION: DebugDialogProc
//
// DESCRIPTION:
// Windows callback for the debug dialog.
//
// PARAMETERS:
//
//   hwnd   : The window handle for the message
//
//   msg    : The message received
//
//   wparam : The wparam for the message
//
//   lparam : The lparam for the message.
//
// RETURN VALUE:
//
//   0 if the message was handled by this procedure.
//
static BOOL CALLBACK DebugDialogProc(
  HWND hwnd,
  UINT msg,
  WPARAM wparam,
  LPARAM /* lparam */)
{
  RECT WRect;
  RECT DRect;
  int WindowWidth;
  int WindowHeight;
  int DialogWidth;
  int DialogHeight;
  int DialogX;
  int DialogY;
  char Buffer[256];
  int DasmLine;

  //
  // What is the message
  //
  switch (msg)
  {
    case WM_INITDIALOG:

      // hwndFocus = (HWND) wparam; // handle of control to receive focus
      // lInitParam = lparam;
      //
      // Do initialization stuff here
      //
      if (!LastPosSet)
      {
        GetWindowRect(ConfigParent, &WRect);
        WindowWidth = (WRect.right - WRect.left) + 1;
        WindowHeight = (WRect.bottom - WRect.top) + 1;

        GetWindowRect(hwnd, &DRect);
        DialogWidth = (DRect.right - DRect.left) + 1;
        DialogHeight = (DRect.bottom - DRect.top) + 1;

        DialogX = WRect.left + (WindowWidth - DialogWidth) / 2;
        DialogY = WRect.top + (WindowHeight - DialogHeight) / 2;

        if (DialogX < 0) DialogX = 0;
        if (DialogY < 0) DialogY = 0;
      }
      else
      {
        DialogX = LastDialogX;
        DialogY = LastDialogY;
      }

      SetWindowPos(hwnd, NULL, DialogX, DialogY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

      // Update dialog controls.
      DEBUG_UpdateControls(hwnd);

      return(FALSE);

    case WM_COMMAND:
    {
      int wNotifyCode = HIWORD(wparam); // notification code
      int wID = LOWORD(wparam);         // item, control, or accelerator identifier
      //HWND hwndCtl = (HWND) lparam;     // handle of control

      switch (wNotifyCode)
      {
        case BN_CLICKED:
        {
          switch (wID)
          {
            case IDC_ENABLE_TRACE:
            {
              bool NewTraceEnabled =
                (SendDlgItemMessage(hwnd, IDC_ENABLE_TRACE, BM_GETCHECK, 0, 0) == BST_CHECKED);

              if (NewTraceEnabled != TraceEnabled)
              {
                TraceEnabled = NewTraceEnabled;
                if (TraceEnabled)
                {
                  BreakpointCount++;
                }
                else
                {
                  BreakpointCount--;
                }
              }
              break;
            }

            case IDC_DUMP_TRACE:
            {
              char ofnBuffer[1024];
              if (SaveFileDialog("Save trace file...", ofnBuffer, 1024, "trace file\0*.txt\0\0"))
              {
                DEBUG_SaveTrace(ofnBuffer);
              }
              break;
            }

            case IDC_BRK_SET:
              DasmLine = SendDlgItemMessage(hwnd, IDC_LIST_ASM, LB_GETCURSEL, 0, (LPARAM) 0);
              if (DasmLine != LB_ERR)
              {
                if (!BreakPointSet[DisassLineAddr[DasmLine]])
                {
                  BreakpointCount++;
                  BreakPointSet[DisassLineAddr[DasmLine]] = true;
                  DEBUG_UpdateDisassembly(hwnd);
                }
              }
              break;

            case IDC_BRK_CLR:
              DasmLine = SendDlgItemMessage(hwnd, IDC_LIST_ASM, LB_GETCURSEL, 0, (LPARAM) 0);
              if (DasmLine != LB_ERR)
              {
                if (BreakPointSet[DisassLineAddr[DasmLine]])
                {
                  BreakpointCount--;
                  BreakPointSet[DisassLineAddr[DasmLine]] = false;
                  DEBUG_UpdateDisassembly(hwnd);
                }
              }
              break;

            case IDC_BRK_CLR_ALL:
              Breakpoint_ClearAll();
              DEBUG_UpdateDisassembly(hwnd);
              break;

            case IDC_DEBUG_CONTINUE:
            {
              RECT wr;
              GetWindowRect(hwnd, &wr);

              LastPosSet = true;
              LastDialogX = wr.left;
              LastDialogY = wr.top;

              DbgState = DEBUG_NONE;
              EndDialog(hwnd, IDOK);
              DbgHwnd = 0;
              break;
            }

            case IDC_DEBUG_STEP:
              DbgState = DEBUG_STEP;
              break;

            default:
              break;
          }
          break;
        }

        case EN_KILLFOCUS:
        {
          switch (wID)
          {
            case IDC_DISASS_ADDRESS:
            {
              // user has edited disassembly memory address
              SendDlgItemMessage(hwnd, IDC_DISASS_ADDRESS, WM_GETTEXT, 256, (LPARAM) Buffer);
              printf("Disassembly Addr changed to %s\n", Buffer);
              sscanf(Buffer, "%X:%X", (unsigned int *) &disass_seg, (unsigned int *) &disass_off);

              // update disassembly display
              DEBUG_UpdateDisassembly(hwnd);

              break;
            }

            case IDC_MEMORY_ADDRESS:
              // user has edited memory address
              SendDlgItemMessage(hwnd, IDC_MEMORY_ADDRESS, WM_GETTEXT, 256, (LPARAM) Buffer);
              printf("MemAddr changed to %s\n", Buffer);
              sscanf(Buffer, "%X:%X", (unsigned int *) &showmem_seg, (unsigned int *) &showmem_off);

              // update RAM display
              DEBUG_UpdateMemDump(hwnd);
              break;
          }
        }

        default:
          break;
      }
      break;
    }


    default:
      break;

  }

  return (FALSE);

}


// =============================================================================
// Exported variables
//

int BreakpointCount = 0;

// =============================================================================
// Exported Functions
//

void DEBUG_Initialise(unsigned char *mem_in)
{
  mem = mem_in;
  regs8 = mem + REGS_BASE;
  regs16 = (unsigned short *)(mem + REGS_BASE);

  Breakpoint_ClearAll();
}

void DEBUG_CreateDialog(HINSTANCE hInstance, HWND hwndParent)
{
  ConfigParent = hwndParent;

  if (DbgHwnd == 0)
  {
    DbgHwnd = CreateDialog(
        hInstance,
        MAKEINTRESOURCE(IDD_DIALOG_DEBUG),
        hwndParent,
        DebugDialogProc);
    DbgState = DEBUG_STOPPED;
  }
}

void DEBUG_Update(void)
{
  DEBUG_UpdateControls(DbgHwnd);
}

DebugState_t DEBUG_GetState(void)
{
  if (DbgState == DEBUG_STEP)
  {
    DbgState = DEBUG_STOPPED;
    return DEBUG_STEP;
  }
  return DbgState;
}

void DEBUG_CheckBreak(void)
{
  if (BreakpointCount == 0)
  {
    return;
  }

  unsigned short reg_cs = regs16[REG_CS];
  unsigned short reg_ip = regs16[REG_IP];

  if (TraceEnabled)
  {
    TraceCS[TracePos] = reg_cs;
    TraceIP[TracePos] = reg_ip;

    if (TraceLength < TRACE_BUFFER_LENGTH) TraceLength++;
    TracePos = (TracePos + 1) % TRACE_BUFFER_LENGTH;
  }

  int Address = reg_cs * 16 + reg_ip;

  if (BreakPointSet[Address])
  {
    DbgState = DEBUG_STOPPED;
  }

}



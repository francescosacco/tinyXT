// =============================================================================
// File: win32_8086tiny_interface.cpp
//
// Description:
// Win32 implementation of the 8086tiny interface class.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#include "8086tiny_interface.h"
#include "resource.h"

#include <Windows.h>
#include <Windowsx.h>
#include <stdio.h>
#include <ctype.h>

#include <math.h>

#include "serial_emulation.h"
#include "file_dialog.h"

#include "win32_cga.h"
#include "win32_serial_cfg.h"
#include "win32_sound_cfg.h"
#include "win32_snd_drv.h"

/*  Make the class name into a global variable  */
char szClassName[ ] = "8086TinyWindowsApp";

/* The app instance */
static HINSTANCE MyInstance;

/* This is the handle for our window */
static HWND hwndMain;
#define WIN_FLAGS (WS_OVERLAPPED | WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU)
static int CurrentDispW = 0;
static int CurrentDispH = 0;
static TextDisplay_t WindowTextDisplay = TD_VGA_8x16;

// emulation state control flags
static bool EmulationExitFlag = false;
static bool ResetPending = false;
static bool FDImageChanged = false;

// disk and bios image file names
static char BiosFilename[1024];
static char HDFilename[1024];
static char FDFilename[1024];

int CPU_Clock_Hz = 4770000;
const int PIT_Clock_Hz = 1193181;

int CPU_Counter = 0;
int CPU_Frame = 0;
int PIT_Counter = 0;

// timing control variables
DWORD INT8_PERIOD_MS = 55;
static DWORD NextInt8Time = 0;
static int Int8Pending = 0;

static DWORD NextSlowdownTime = 0;

// Mouse state variables

static bool HaveCapture = false;
static bool MouseLButtonDown = false;
static bool MouseRButtonDown = false;

static bool lastPosSet = false;
static int lx = 0;
static int ly = 0;

// Sound emulation data
int CurrentSampleRate;
CWaveOut *WaveOut;

static bool SpkrData = false;
static bool SpkrT2Gate = false;
static bool SpkrT2Out = false;
static bool SpkrT2US = false;  // Is T2 rate ultrasonic? Some games use this
                               // instead of silence!

static short SndBuffer[2048];
static int SndBufferLen = 0;
static int SND_Counter = 0;

// =============================================================================
// PIC 8259 stuff
//

int PIC_OCW_Idx = 0;
unsigned char PIC_OCW[3] = { 0, 0, 0 };
int PIC_ICW_Idx = 0;
unsigned char PIC_ICW[4] = { 0, 0, 0, 0 };

// =============================================================================
// PIT 8253 stuff
//

struct TimerData_t
{
  bool BCD;             // BCD mode
  int Mode;             // Timer mode
  int RLMode;           // Read/Load mode
  int ResetHolding;     // Holding area for timer reset count
  int ResetCount;       // Reload value when count = 0
  int Count;            // Current timer counter
  int Latch;            // Latched timer count: -1 = not latched
  bool LSBToggle;       // Read load LSB (true) /MSB(false) next?
};

const TimerData_t PIT_Channel0Default = { false, 2, 3, 0, 0, 0, -1 , true};
const TimerData_t PIT_Channel1Default = { false, 2, 3, 1024, 1024, 1024, -1, true };
const TimerData_t PIT_Channel2Default = { false, 3, 3, 1024, 1024, 1024, -1, true };


TimerData_t PIT_Channel0 = PIT_Channel0Default;
TimerData_t PIT_Channel1 = PIT_Channel1Default;
TimerData_t PIT_Channel2 = PIT_Channel2Default;

void ResetPIT(void)
{
  PIT_Channel0 = PIT_Channel0Default;
  PIT_Channel1 = PIT_Channel1Default;
  PIT_Channel2 = PIT_Channel2Default;

  INT8_PERIOD_MS = 55;
  NextInt8Time = 0;
}

void PIT_UpdateTimers(int Ticks)
{
  PIT_Channel0.Count -= Ticks;
  while (PIT_Channel0.Count <= 0)
  {
    if (PIT_Channel0.ResetCount == 0)
    {
      PIT_Channel0.Count += 65536;
    }
    else
    {
      PIT_Channel0.Count += PIT_Channel0.ResetCount;
    }

    Int8Pending++;
  }

  // PIT Channel 1 is only used for DRAM refresh.
  // Don't bother update this timer.
  //PIT_Channel1.Count -= Ticks;
  //while (PIT_Channel1.Count <= 0)
  //{
  //  if (PIT_Channel1.ResetCount == 0)
  //  {
  //    PIT_Channel1.Count += 65536;
  //  }
  //  else
  //  {
  //    PIT_Channel1.Count += PIT_Channel1.ResetCount;
  //  }
  //}

  PIT_Channel2.Count -= Ticks;
  if (PIT_Channel2.Mode == 2)
  {
    SpkrT2Out = false;

    if (PIT_Channel2.Count <= 0)
    {
      if (PIT_Channel2.ResetCount == 0)
      {
        PIT_Channel2.Count += 65536;
      }
      else
      {
        PIT_Channel2.Count += PIT_Channel2.ResetCount;
      }
      SpkrT2Out = true;
    }
  }
  else if (PIT_Channel2.Mode == 3)
  {
    if (PIT_Channel2.Count <= 0)
    {
      if (PIT_Channel2.ResetCount == 0)
      {
        PIT_Channel2.Count += 65536;
      }
      else
      {
        PIT_Channel2.Count += PIT_Channel2.ResetCount;
      }
    }
    SpkrT2Out = (PIT_Channel2.Count >= (PIT_Channel2.ResetCount / 2));
  }
}

void PIT_WriteTimer(int T, unsigned char Val)
{
  TimerData_t *Timer;
  if (T == 0)
    Timer = &PIT_Channel0;
  else if (T == 1)
    Timer = &PIT_Channel1;
  else
    Timer = &PIT_Channel2;

  bool WriteLSB = false;

  if (Timer->RLMode == 1)
  {
    WriteLSB = true;
  }
  else if (Timer->RLMode == 3)
  {
    WriteLSB = Timer->LSBToggle;
    Timer->LSBToggle = !Timer->LSBToggle;
  }

  if (WriteLSB)
  {
    Timer->ResetHolding = (Timer->ResetHolding & 0xFF00) | Val;
  }
  else
  {
    Timer->ResetHolding = (Timer->ResetHolding & 0x00FF) | (((int) Val) << 8);
    Timer->ResetCount = Timer->ResetHolding;

    if (Timer->Mode == 0)
    {
      Timer->Count = Timer->ResetCount;
    }

    if (T == 0)
    {
      INT8_PERIOD_MS = (Timer->ResetCount * 1000) / PIT_Clock_Hz;
      if (INT8_PERIOD_MS == 0) INT8_PERIOD_MS = 1;
    }
    else if (T == 2)
    {
      // Is T2 frequency ultrasonic (> 15 kHz)
      SpkrT2US = (Timer->ResetCount < 80);
    }
  }
}

unsigned char PIT_ReadTimer(int T)
{
  TimerData_t *Timer;
  int ReadValue;
  bool ReadLSB = false;
  unsigned char Val;

  if (T == 0)
    Timer = &PIT_Channel0;
  else if (T == 1)
    Timer = &PIT_Channel1;
  else
    Timer = &PIT_Channel2;


  if (Timer->Latch != -1)
  {
    ReadValue = Timer->Latch;
  }
  else
  {
    ReadValue = Timer->Count;
  }

  if (Timer->RLMode == 1)
  {
    ReadLSB = true;
  }
  else if (Timer->RLMode == 3)
  {
    ReadLSB = Timer->LSBToggle;
    Timer->LSBToggle = !Timer->LSBToggle;
  }

  if (ReadLSB)
  {
    Val = (unsigned char)(ReadValue & 0xFF);
  }
  else
  {
    Val = (unsigned char)((ReadValue >> 8) & 0xFF);
    Timer->Latch = -1;
  }

  return Val;
}

void PIT_WriteControl(unsigned char Val)
{
  int T = (Val >> 6) & 0x03;
  TimerData_t *Timer;

  if (T == 0)
    Timer = &PIT_Channel0;
  else if (T == 1)
    Timer = &PIT_Channel1;
  else
    Timer = &PIT_Channel2;

  int RLMode = (Val >> 4) & 0x03;
  if (RLMode == 0)
  {
    Timer->Latch = Timer->Count;
    Timer->LSBToggle = true;
  }
  else
  {
    Timer->RLMode = RLMode;
    if (RLMode == 3) Timer->LSBToggle = true;
  }

  int Mode = (Val >> 1) & 0x07;
  Timer->Mode = Mode;

  Timer->BCD = (Val & 1) == 1;
}

// =============================================================================
// Keyboard stuff
//

#define KEYBUFFER_LEN 64
static int KeyBufferHead = 0;
static int KeyBufferTail = 0;
static int KeyBufferCount = 0;
static unsigned char KeyBuffer[KEYBUFFER_LEN];

static unsigned char KeyInputBuffer = 0;
static bool KeyInputFull = false;

// Convert windows virtual key to set 1 scan code for alpha keys
static unsigned char VKAlphaToSet1[26] =
{
  0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, // 'A' .. 'J'
  0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, // 'K' .. 'T'
  0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C                          // 'U' .. 'Z'
};

// Convert windows virtual key to set 1 scan code for digit keys
static unsigned char VKDigitToSet1[10] =
{
  0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A // '0' .. '9'
};

static unsigned int VKtoSet1Code(int vk, bool IsExtended)
{
  if ((vk >= 'A') && (vk <= 'Z'))
  {
    return VKAlphaToSet1[vk-'A'];
  }
  else if ((vk >= '0') && (vk <= '9'))
  {
    return VKDigitToSet1[vk - '0'];
  }
  else if ((vk >= VK_F1) && (vk <= VK_F10))
  {
    return 0x3B + (vk - VK_F1);
  }

  switch (vk)
  {
    case VK_RETURN:
      return 0x1C;

    case VK_ESCAPE:
      return 0x01;

    case VK_SPACE:
      return 0x39;

    case VK_SHIFT:
      if (IsExtended)
        return 0x36;
      else
        return 0x2A;
    case VK_RSHIFT:
      return 0x36;
    case VK_CONTROL:
      return 0x1D;
    case VK_RCONTROL:
      return 0x1D;

    case VK_MENU: // Alt
      return 0x38;

    case VK_LEFT:
      return 0x4B;
    case VK_UP:
      return 0x48;
    case VK_RIGHT:
      return 0x4D;
    case VK_DOWN:
      return 0x50;

    case VK_BACK:
      return 0x0E;

    case VK_TAB:
      return 0x0F;

    case VK_CAPITAL:
      return 0x3A;
    case VK_NUMLOCK:
      return 0x45;
    case VK_SCROLL:
      return 0x46;

    case VK_NUMPAD0:
    case VK_INSERT:
      return 0x52;
    case VK_NUMPAD1:
    case VK_END:
      return 0x4F;
    case VK_NUMPAD2:
      return 0x50;
    case VK_NUMPAD3:
    case VK_NEXT:
      return 0x51;
    case VK_NUMPAD4:
      return 0x4B;
    case VK_NUMPAD5:
    case VK_CLEAR:
      return 0x4C;
    case VK_NUMPAD6:
      return 0x4D;
    case VK_NUMPAD7:
    case VK_HOME:
      return 0x47;
    case VK_NUMPAD8:
      return 0x48;
    case VK_NUMPAD9:
    case VK_PRIOR:
      return 0x49;
    case VK_MULTIPLY:
      return 0x37;
    case VK_SUBTRACT:
      return 0x4A;
    case VK_ADD:
      return 0x4E;
    case VK_DIVIDE:
      return 0x35;
    case VK_DECIMAL:
    case VK_DELETE:
      return 0x53;

    case VK_OEM_1: // ';'
      return 0x27;

    case VK_OEM_PLUS:
      return 0x0D;

    case VK_OEM_COMMA:
      return 0x33;

    case VK_OEM_MINUS:
      return 0x0C;

    case VK_OEM_PERIOD:
      return 0x34;

    case VK_OEM_2: // '/'
      return 0x35;

    case VK_OEM_3: // '~'
      return 0x29;

    case VK_OEM_4: // '['
      return 0x1A;

    case VK_OEM_5: // '\'
      return 0x2B;

    case VK_OEM_6: // ']'
      return 0x1B;

    case VK_OEM_7: // '''
      return 0x28;

    case VK_F11:

      // F11 is not available on the XT keyboard so use this to test
      // instruction execution (trace on and off)
      return 0x7e;

    case VK_F12:
      // F12 is not available on the XT keyboard
      if (HaveCapture)
      {
        HaveCapture = false;
        ReleaseCapture();
        ShowCursor(TRUE);
      }
      return 0xfF;

    default:
      printf("Unhandled key code 0x%02x\n", vk);
      break;

  }

  return 0xFF;
}

static inline void AddKeyEvent(unsigned char code)
{
  if (KeyBufferCount < KEYBUFFER_LEN)
  {
    KeyBuffer[KeyBufferTail] = code;
    KeyBufferTail = (KeyBufferTail + 1) % KEYBUFFER_LEN;
    KeyBufferCount++;
  }
}

static inline  bool IsKeyEventAvailable(void)
{
  return (KeyBufferCount > 0);
}

static inline unsigned char GetKeyEvent(void)
{
  unsigned char code = 0xff;

  if (KeyBufferCount > 0)
  {
    code = KeyBuffer[KeyBufferHead];
  }

  return code;
}

static inline unsigned char NextKeyEvent(void)
{
  unsigned char code = 0xff;

  if (KeyBufferCount > 0)
  {
    code = KeyBuffer[KeyBufferHead];
    KeyBufferHead = (KeyBufferHead + 1) % KEYBUFFER_LEN;
    KeyBufferCount--;
  }

  return code;
}

// ============================================================================
// Windows stuff
//

int ReadConfig(const char *Filename)
{
  FILE *fp;
  char Line[1024];
  int len;

  fp = fopen(Filename, "r");
  if (fp == NULL)
  {
    /*
     * .cfg file doesn't exist yet
     */
    return 0;
  }

  // Read BIOS image file
  fgets(Line, 256, fp);
  if (strncmp(Line, "[BIOS]", 6) != 0) return 0;

  fgets(Line, 256, fp);
  len = strlen(Line)-1;
  while ((len > 0) && (!isprint(Line[len]))) Line[len--] = 0;
  if (strncmp(Line, "NIL", 3) == 0)
  {
    BiosFilename[0] = 0;
  }
  else
  {
    strncpy(BiosFilename, Line, 1024);
  }

  // Read floppy image name
  fgets(Line, 256, fp);
  if (strncmp(Line, "[FD]", 4) != 0) return 0;

  fgets(Line, 256, fp);
  len = strlen(Line)-1;
  while ((len > 0) && (!isprint(Line[len]))) Line[len--] = 0;
  if (strncmp(Line, "NIL", 3) == 0)
  {
    FDFilename[0] = 0;
  }
  else
  {
    strncpy(FDFilename, Line, 1024);
  }

  // Read HD image name
  fgets(Line, 256, fp);
  if (strncmp(Line, "[HD]", 4) != 0) return 0;

  fgets(Line, 256, fp);
  len = strlen(Line)-1;
  while ((len > 0) && (!isprint(Line[len]))) Line[len--] = 0;
  if (strncmp(Line, "NIL", 3) == 0)
  {
    HDFilename[0] = 0;
  }
  else
  {
    strncpy(HDFilename, Line, 1024);
  }

  // Read CPU speed
  fgets(Line, 256, fp);
  if (strncmp(Line, "[CPU_SPEED]", 11) != 0) return 0;

  fgets(Line, 256, fp);
  sscanf(Line, "%d\n", &CPU_Clock_Hz);

  // Read serial port configuration.
  SERIAL_ReadConfig(fp);

  // Read sound configuration
  SNDCFG_Read(fp);

  fclose(fp);

  return 1;
}

LRESULT CALLBACK WindowProcedure (HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  unsigned int KeyCode;

  switch (message)                  /* handle the messages */
  {
    case WM_DESTROY:
      EmulationExitFlag = true;
      PostQuitMessage (0);       /* send a WM_QUIT to the message queue */
      break;

    case WM_INITMENU:
      if (WindowTextDisplay == TD_CGA)
      {
        CheckMenuItem((HMENU) wParam, IDM_TEXT_CGA, MF_BYCOMMAND | MF_CHECKED);
        CheckMenuItem((HMENU) wParam, IDM_TEXT_VGA_8x16, MF_BYCOMMAND | MF_UNCHECKED);
      }
      else
      {
        CheckMenuItem((HMENU) wParam, IDM_TEXT_CGA, MF_BYCOMMAND | MF_UNCHECKED);
        CheckMenuItem((HMENU) wParam, IDM_TEXT_VGA_8x16, MF_BYCOMMAND | MF_CHECKED);
      }
      break;

    case WM_KEYDOWN:
      KeyCode = VKtoSet1Code(wParam, ((lParam & 0x01000000) != 0));
      if ((KeyCode & 0xFF00) != 0) AddKeyEvent((KeyCode >> 8) & 0xFF);
      AddKeyEvent(KeyCode & 0xFF);
      break;

    case WM_KEYUP:
      KeyCode = VKtoSet1Code(wParam, ((lParam & 0x01000000) != 0));
      if ((KeyCode & 0xFF00) != 0) AddKeyEvent((KeyCode >> 8) & 0xFF);
      AddKeyEvent((KeyCode & 0xFF) | 0x80);
      break;

    case WM_SYSKEYDOWN:
      KeyCode = VKtoSet1Code(wParam, ((lParam & 0x01000000) != 0));
      if ((KeyCode & 0xFF00) != 0) AddKeyEvent((KeyCode >> 8) & 0xFF);
      AddKeyEvent(KeyCode & 0xFF);
      break;

    case WM_SYSKEYUP:
      KeyCode = VKtoSet1Code(wParam, ((lParam & 0x01000000) != 0));
      if ((KeyCode & 0xFF00) != 0) AddKeyEvent((KeyCode >> 8) & 0xFF);
      AddKeyEvent((KeyCode & 0xFF) | 0x80);
      break;

    case WM_CAPTURECHANGED:
      if (HaveCapture)
      {
        ShowCursor(TRUE);
        HaveCapture = false;
      }
      break;

    case WM_LBUTTONDOWN:
      if (!HaveCapture)
      {
        SetCapture(hwndMain);
        ShowCursor(FALSE);
        HaveCapture = true;
      }
      MouseLButtonDown = true;
      SERIAL_MouseMove(0, 0, MouseLButtonDown, MouseRButtonDown);
      break;

    case WM_LBUTTONUP:
      MouseLButtonDown = false;
      SERIAL_MouseMove(0, 0, MouseLButtonDown, MouseRButtonDown);
      break;

    case WM_RBUTTONDOWN:
      MouseRButtonDown = true;
      SERIAL_MouseMove(0, 0, MouseLButtonDown, MouseRButtonDown);
      break;

    case WM_RBUTTONUP:
      MouseRButtonDown = false;
      SERIAL_MouseMove(0, 0, MouseLButtonDown, MouseRButtonDown);
      break;

    case WM_COMMAND:
    {
      //  int wNotifyCode = HIWORD(wParam); // notification code
      int wID = LOWORD(wParam);         // item, control, or accelerator identifier
      //HWND hwndCtl = (HWND) lparam;     // handle of control

      switch (wID)
      {
        //
        // File menu
        //
        case IDM_RESET:
          ResetPending = true;
          break;

        case IDM_QUIT:
          DestroyWindow(hwnd);
          break;

        case IDM_TEXT_CGA:
        {
          WindowTextDisplay = TD_CGA;
          CGA_SetTextDisplay(TD_CGA);
          break;
        }

        case IDM_TEXT_VGA_8x16:
        {
          WindowTextDisplay = TD_VGA_8x16;
          CGA_SetTextDisplay(TD_VGA_8x16);
          break;
        }

        case IDM_SET_SERIAL_PORTS:
          SERIAL_ConfigDialog(MyInstance, hwnd);
          break;

        case IDM_CONFIGURE_SOUND:
          if (SNDCFG_Dialog(MyInstance, hwnd))
          {
            if (CurrentSampleRate != AudioSampleRate)
            {
              delete WaveOut;

              WAVEFORMATEX wfx;
              wfx.cbSize = 0;
              wfx.wFormatTag = WAVE_FORMAT_PCM;
              wfx.nAvgBytesPerSec = AudioSampleRate;
              wfx.nChannels = 1;
              wfx.nSamplesPerSec = AudioSampleRate;
              wfx.wBitsPerSample = 16;
              wfx.nBlockAlign = 2;

              WaveOut = new CWaveOut(&wfx, 64, 1024);

              CurrentSampleRate = AudioSampleRate;
            }
          }
          break;
      }
    }

    default:                      /* for messages that we don't deal with */
      return DefWindowProc (hwnd, message, wParam, lParam);
  }

  return 0;
}

// =============================================================================
// Interface class.
//

T8086TinyInterface_t::T8086TinyInterface_t()
{
}

T8086TinyInterface_t::~T8086TinyInterface_t()
{
}

void T8086TinyInterface_t::SetInstance(HINSTANCE hInst)
{
  hInstance = hInst;
  MyInstance = hInst;
}

bool T8086TinyInterface_t::Initialise(unsigned char *mem_in)
{
  AllocConsole();
  freopen("CONOUT$", "wb", stdout);

  printf("TinyXT starting\n");

  // Store a pointer to system memory
  mem = mem_in;

  // Initialise ports
  for (int i = 0 ; i < 65536 ; i++)
  {
    Port[i] = 0xff;
  }

  WNDCLASSEX wincl;        /* Data structure for the windowclass */

  /* The Window structure */
  wincl.hInstance = hInstance;
  wincl.lpszClassName = szClassName;
  wincl.lpfnWndProc = WindowProcedure;      /* This function is called by windows */
  wincl.style = CS_DBLCLKS;                 /* Catch double-clicks */
  wincl.cbSize = sizeof (WNDCLASSEX);

  /* Use default icon and mouse-pointer */
  wincl.hIcon = LoadIcon (NULL, IDI_APPLICATION);
  wincl.hIconSm = LoadIcon (NULL, IDI_APPLICATION);
  wincl.hCursor = LoadCursor (NULL, IDC_ARROW);
  wincl.lpszMenuName = NULL;                 /* No menu */
  wincl.cbClsExtra = 0;                      /* No extra bytes after the window class */
  wincl.cbWndExtra = 0;                      /* structure or the window instance */
  /* Use Windows's default colour as the background of the window */
  wincl.hbrBackground = (HBRUSH) COLOR_BACKGROUND;

  /* Register the window class, and if it fails quit the program */
  if (!RegisterClassEx (&wincl)) return false;

  CurrentDispW = 640;
  CurrentDispH = 400;
  RECT wrect = { 0, 0, CurrentDispW, CurrentDispH };
  AdjustWindowRect(&wrect, WIN_FLAGS, TRUE);
  int width = wrect.right - wrect.left;
  int height = wrect.bottom - wrect.top;

  /* The class is registered, let's create the program*/
  hwndMain = CreateWindowEx (
          0,                   /* Extended possibilities for variation */
          szClassName,         /* Classname */
          "TinyXT",         /* Title Text */
          WIN_FLAGS,           /* window flags */
          CW_USEDEFAULT,       /* Windows decides the position */
          CW_USEDEFAULT,       /* where the window ends up on the screen */
          width,               /* The programs width */
          height,              /* and height in pixels */
          HWND_DESKTOP,        /* The window is a child-window to desktop */
          (HMENU) LoadMenu(hInstance, (LPCSTR) IDR_MENU1),           /* Main menu */
          hInstance,           /* Program Instance handler */
          NULL                 /* No Window Creation data */
          );

  /* Make the window visible on the screen */
  ShowWindow (hwndMain, SW_SHOW);

  timeBeginPeriod(1);

  CGA_Initialise();
  SERIAL_Initialise();

  ReadConfig("default.cfg");

  WAVEFORMATEX wfx;
  wfx.cbSize = 0;
  wfx.wFormatTag = WAVE_FORMAT_PCM;
  wfx.nAvgBytesPerSec = AudioSampleRate;
  wfx.nChannels = 1;
  wfx.nSamplesPerSec = AudioSampleRate;
  wfx.wBitsPerSample = 16;
  wfx.nBlockAlign = 2;

  WaveOut = new CWaveOut(&wfx, 64, 1024);

  CurrentSampleRate = AudioSampleRate;

  return true;

}

void T8086TinyInterface_t::Cleanup(void)
{
  delete WaveOut;

  timeEndPeriod(1);

  CGA_Cleanup();
  SERIAL_Cleanup();
}

bool T8086TinyInterface_t::ExitEmulation(void)
{
  return EmulationExitFlag;
}

bool T8086TinyInterface_t::Reset(void)
{
  if (ResetPending)
  {
    CPU_Counter = 0;
    CPU_Frame = 0;
    PIT_Counter = 0;

    // Reset keyboard
    KeyBufferHead = 0;
    KeyBufferTail = 0;
    KeyBufferCount = 0;
    KeyInputBuffer = 0;
    KeyInputFull = false;

    // Reset sound emulation
    SpkrData = false;
    SpkrT2Gate = false;
    SpkrT2Out = false;
    SpkrT2US = false;
    SndBufferLen = 0;
    SND_Counter = 0;

    CGA_Reset();
    SERIAL_Reset();
    ResetPIT();
    Int8Pending = false;
    ResetPending = false;

    for (int i = 0 ; i < 4 ; i++) PIC_ICW[i] = 0;
    for (int i = 0 ; i < 3 ; i++) PIC_OCW[i] = 0;
    PIC_ICW_Idx = 0;
    PIC_OCW_Idx = 0;

    return true;
  }
  return false;
}

char *T8086TinyInterface_t::GetBIOSFilename(void)
{
  if (BiosFilename[0] == 0)
  {
    return NULL;
  }

  return BiosFilename;
}

char *T8086TinyInterface_t::GetFDImageFilename(void)
{
  FDImageChanged = false;
  if (FDFilename[0] == 0)
  {
    return NULL;
  }

  return FDFilename;
}

char *T8086TinyInterface_t::GetHDImageFilename(void)
{
  if (HDFilename[0] == 0)
  {
    return NULL;
  }

  return HDFilename;
}

bool T8086TinyInterface_t::FDChanged(void)
{
  return FDImageChanged;
}

bool T8086TinyInterface_t::TimerTick(int nTicks)
{
  int PIT_Ticks;
  MSG messages;
  bool NextVideoFrame = false;

  // TODO:
  // This works OK for small values of nTicks, however, the following
  // processing will break down horribly if nTicks is ever larger ( > about 10)
  // If I ever get nTicks per instruction then this will need to change to
  // a loop. Probably safe to process 4 ticks per loop.

  // Update PIT

  PIT_Counter = PIT_Counter + PIT_Clock_Hz * nTicks;
  PIT_Ticks = PIT_Counter / CPU_Clock_Hz;
  PIT_Counter = PIT_Counter % CPU_Clock_Hz;

  PIT_UpdateTimers(PIT_Ticks);

  // Update sound output
  if (SoundEnabled)
  {
    int SoundTicks;
    SND_Counter = SND_Counter + AudioSampleRate * nTicks;
    SoundTicks = SND_Counter / CPU_Clock_Hz;
    SND_Counter = SND_Counter % CPU_Clock_Hz;
    for (int i = 0 ; i < SoundTicks ; i++)
    {
      if (SpkrT2Gate)
      {
        if (SpkrT2US)
        {
          SndBuffer[SndBufferLen] = 0;
        }
        else
        {
          SndBuffer[SndBufferLen] = (SpkrT2Out) ? VolumeSample : -VolumeSample;
        }
      }
      else
      {
        SndBuffer[SndBufferLen] = (SpkrData) ? VolumeSample : 0;
      }
      SndBufferLen+=1;
    }
  }

  // main update processing is every 4 ms of CPU time.

  CPU_Counter += nTicks;
  if (CPU_Counter > (CPU_Clock_Hz / 250))
  {

    CPU_Counter = 0;
    CPU_Frame++;

    if (CPU_Frame == 4)
    {
      if (SoundEnabled)
      {
        WaveOut->Write((PBYTE) SndBuffer, SndBufferLen*2);
        SndBufferLen = 0;
      }

      int w, h;
      CGA_GetDisplaySize(w, h);
      if ((w != CurrentDispW) || (h != CurrentDispH))
      {
        CurrentDispW = w;
        CurrentDispH = h;

        RECT wrect = { 0, 0, CurrentDispW, CurrentDispH };
        AdjustWindowRect(&wrect, WIN_FLAGS, TRUE);
        w = wrect.right - wrect.left;
        h = wrect.bottom - wrect.top;
        SetWindowPos(hwndMain, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
      }

      CGA_DrawScreen(hwndMain, mem);
      NextVideoFrame = true;
      CPU_Frame = 0;

      // Get the mouse position using GetCursorPos.

      POINT cp;
      GetCursorPos(&cp);
      int xPos = cp.x;
      int yPos = cp.y;

      if (lastPosSet)
      {
        int dx = xPos - lx;
        int dy = yPos - ly;
        if ((dx != 0) || (dy != 0))
        {
          SERIAL_MouseMove(dx, dy, MouseLButtonDown, MouseRButtonDown);
        }
      }

      if (HaveCapture)
      {
        static double scale = 1.0;
        RECT wrect;
        GetWindowRect(hwndMain, &wrect);
        lx = (wrect.left + wrect.right) / 2;
        ly = (wrect.top + wrect.bottom) / 2;
        SetCursorPos(lx * scale + 0.5, ly * scale + 0.5);
        GetCursorPos(&cp);
        if ((cp.x != lx) || (cp.y != ly))
        {
          // If the cursor position we get is not what we set then
          // display scaling has occurred.
          // We need to calculate the scaling factor so we can account
          // for it.
          scale = 1.0;
          SetCursorPos(lx * scale + 0.5, ly * scale + 0.5);
          GetCursorPos(&cp);

          scale = ((double) lx) / ((double) cp.x);
          SetCursorPos(lx * scale + 0.5, ly * scale + 0.5);
        }
      }
      else
      {
        lx = xPos;
        ly = yPos;
        lastPosSet = true;
      }

      /* Run the message loop. It will run until PeekMessage() returns 0 */
      while (PeekMessage (&messages, NULL, 0, 0, PM_REMOVE))
      {
        /* Translate virtual-key messages into character messages */
        TranslateMessage(&messages);
        /* Send message to WindowProcedure */
        DispatchMessage(&messages);
      }
    }

    SERIAL_HandleSerial();

    DWORD CurrentTime = timeGetTime();
    if (CurrentTime >= NextSlowdownTime)
    {
      // No slowdown required
      NextSlowdownTime = CurrentTime + 4;
    }
    else
    {
      Sleep(NextSlowdownTime - CurrentTime);
      NextSlowdownTime += 4;
    }

    if (NextVideoFrame)
    {
      CGA_VBlankStart();
    }
  }

  return NextVideoFrame;
}

void T8086TinyInterface_t::WritePort(int Address, unsigned char Value)
{
  Port[Address] = Value;

  if (CGA_WritePort(Address, Value))
  {
    return;
  }

  if (SERIAL_WritePort(Address, Value))
  {
    return;
  }

  switch (Address)
  {
    // PIC Registers
    case 0x20:
      if (PIC_OCW_Idx == 0)
      {
        if ((Value & 0x10) != 0)
        {
          PIC_ICW[0] = Value;
          PIC_ICW_Idx = 1;
        }
      }
      else
      {
        PIC_OCW[PIC_OCW_Idx] = Value;
        PIC_OCW_Idx++;
        if (PIC_OCW_Idx > 2) PIC_OCW_Idx = 0;
      }
      break;
    case 0x21:
      if (PIC_ICW_Idx == 0)
      {
        PIC_OCW[0] = Value;
        PIC_OCW_Idx = 1;
      }
      else
      {
        PIC_ICW[PIC_ICW_Idx] = Value;
        PIC_ICW_Idx++;
        if ((PIC_ICW[0] & 0x02) != 0)
        {
          // No ICW3 needed
          if (PIC_ICW_Idx > 1) PIC_ICW_Idx = 0;
        }
        if ((PIC_ICW[0] & 0x01) == 0)
        {
          // No ICW 4 needed
          if (PIC_ICW_Idx > 2) PIC_ICW_Idx = 0;
        }
        if (PIC_ICW_Idx > 3) PIC_ICW_Idx = 0;
      }
      break;

    // PIT Registers
    case 0x40:
      PIT_WriteTimer(0, Value);
      break;
    case 0x41:
      PIT_WriteTimer(1, Value);
      break;
    case 0x42:
      PIT_WriteTimer(2, Value);
      break;
    case 0x43:
      PIT_WriteControl(Value);
      break;

    case 0x61:
      SpkrData = ((Value & 0x02) == 0x02);
      SpkrT2Gate = ((Value & 0x01) == 0x01);
      break;

    default:
      //printf("OUT %04x=%02x\n", Address, Value);
      break;
  }
}

unsigned char T8086TinyInterface_t::ReadPort(int Address)
{
  // By default return the last value written to the port.
  unsigned char retval = Port[Address];

  if (CGA_ReadPort(Address, retval))
  {
    return retval;
  }

  if (SERIAL_ReadPort(Address, retval))
  {
    return retval;
  }

  // Handle specific processing for ports that do something different.
  switch (Address)
  {
    case 0x0020:
      retval = 0;
      break;
    case 0x0021:
      retval = PIC_OCW[0];
      break;
    case 0x0040:
      retval = PIT_ReadTimer(0);
      break;
    case 0x0041:
      retval = PIT_ReadTimer(1);
      break;
    case 0x0042:
      retval = PIT_ReadTimer(2);
      break;
    case 0x0043:
      break;

    case 0x0060:
      retval = KeyInputBuffer;
      KeyInputFull = false;
      break;

    case 0x0064:
      retval = 0x14;
      if (KeyInputFull) retval |= 0x01;
      break;

    case 0x0201:
      // joystick is unsupported at the moment
      retval = 0xff;
      break;

    default:
      //printf("IN %04X\n", Address);
      break;
  }

  return retval;
}

unsigned int T8086TinyInterface_t::VMemRead(int i_w, int addr)
{
  return CGA_VMemRead(mem, i_w, addr);
}

unsigned int T8086TinyInterface_t::VMemWrite(int i_w, int addr, unsigned int val)
{
  return CGA_VMemWrite(mem, i_w, addr, val);
}

bool T8086TinyInterface_t::IntPending(int &IntNumber)
{
  if (Int8Pending > 0)
  {
    IntNumber = 8;
    Int8Pending--;
    return true;
  }

  if (IsKeyEventAvailable() && !KeyInputFull)
  {
    KeyInputBuffer = NextKeyEvent();
    KeyInputFull = true;
    IntNumber = 9;
    return true;
  }

  return SERIAL_IntPending(IntNumber);
}


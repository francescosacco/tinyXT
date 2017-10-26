// =============================================================================
// File: win32_serial_hw.h
//
// Description:
// Win32 Implementation for the serial HW interface to real serial ports
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#include <windows.h>
#include <stdio.h>
#include "serial_hw.h"

// =============================================================================
// Local variables
//

struct Win32HWComData_t
{
  HANDLE hComPort;  // The handle for the HW Com port
  int BaudRate;     // Windows Baud rate
  int DataBits;     // Windows data bit specifier
  int StopBits;     // Windows stop bits specifier
  int Parity;       // Windows parity specifier
  bool RTS_High;    // Current RTS state
  bool DTR_High;    // Current DTR state
};

Win32HWComData_t HWComData[4];

// =============================================================================
// Local Functions
//

static void SerialDbg_PrintDCBSettings(const DCB &dcb)
{
  char ParityChar;
  switch (dcb.Parity)
  {
    case NOPARITY:
      ParityChar = 'N';
      break;
    case ODDPARITY:
      ParityChar = 'O';
      break;
    case EVENPARITY:
      ParityChar = 'E';
      break;
    case MARKPARITY:
      ParityChar = 'M';
      break;
    case SPACEPARITY:
      ParityChar = 'S';
      break;

    default:
      ParityChar = '?';
      break;
  }

  const char *StopStr;
  switch (dcb.StopBits)
  {
    case ONESTOPBIT:
      StopStr = "1";
      break;
    case ONE5STOPBITS:
      StopStr = "1.5";
      break;
    case TWOSTOPBITS:
      StopStr = "2";
      break;

    default:
      StopStr = "?";
      break;
  }

  printf("%d, %d, %c, %s\n", (int) dcb.BaudRate, (int) dcb.ByteSize, ParityChar, StopStr);
}

static void ConfigureHWComPort(int ComPort)
{
  DCB dcb;

  if (HWComData[ComPort].hComPort == 0) return;

  GetCommState(HWComData[ComPort].hComPort, &dcb);

  dcb.DCBlength = sizeof(DCB);
  dcb.BaudRate = HWComData[ComPort].BaudRate;
  dcb.fBinary = TRUE;
  dcb.fParity = FALSE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDtrControl = DTR_CONTROL_DISABLE;
  dcb.fDsrSensitivity = FALSE;
  dcb.fTXContinueOnXoff = TRUE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  dcb.fErrorChar = FALSE;
  dcb.fNull = FALSE;
  dcb.fRtsControl = RTS_CONTROL_DISABLE;
  dcb.fAbortOnError = FALSE;
  dcb.ByteSize = HWComData[ComPort].DataBits;
  dcb.Parity = HWComData[ComPort].Parity;
  dcb.StopBits = HWComData[ComPort].StopBits;


  if (!SetCommState(HWComData[ComPort].hComPort, &dcb))
  {
    printf("Failed to set comm state: ");
    SerialDbg_PrintDCBSettings(dcb);
  }

  if (!EscapeCommFunction(HWComData[ComPort].hComPort,
                          HWComData[ComPort].DTR_High ? SETDTR  : CLRDTR))
  {
    printf("Set DTR failed\n");
  }
  if (!EscapeCommFunction(HWComData[ComPort].hComPort,
                          HWComData[ComPort].RTS_High ? SETRTS  : CLRRTS))
  {
    printf("SetRTS failed\n");
  }

  char Buffer[256];
  DWORD nBytesRread;

  // Flush the input buffer
  while (ReadFile(HWComData[ComPort].hComPort, Buffer, 256, &nBytesRread, NULL))
  {
    if (nBytesRread == 0) break;
  }
}

// =============================================================================
// Exported functions
//

void SERIAL_HW_Initialise(void)
{
  for (int i = 0 ; i < 4 ; i++)
  {
    HWComData[i].hComPort = 0;
    HWComData[i].BaudRate = CBR_9600;
    HWComData[i].DataBits = 8;
    HWComData[i].StopBits = ONESTOPBIT;
    HWComData[i].Parity   = NOPARITY;
    HWComData[i].RTS_High = false;
    HWComData[i].DTR_High = false;
  }
}

void SERIAL_HW_Open(int ComPort, const char *HWComName)
{
  if ((ComPort < 0) || (ComPort >= 4)) return;

  if (HWComData[ComPort].hComPort != 0)
  {
    CloseHandle(HWComData[ComPort].hComPort);
    HWComData[ComPort].hComPort = 0;
  }

  HWComData[ComPort].hComPort =
    ::CreateFile(HWComName,
      GENERIC_READ|GENERIC_WRITE, // access (read and write
      0,                          // (share) 0:cannot share the COM port
      0,                          // security  (None)
      OPEN_EXISTING,              // creation : open_existing
      0,                          // FILE_FLAG_OVERLAPPED, we want polled mode
      0                           // no templates file for COM port...
      );

  if (HWComData[ComPort].hComPort == 0)
  {
    printf("Failed to connect COM%d to device '%s'\n", ComPort+1, HWComName);
    return;
  }

  printf("Connected COM%d to device '%s'\n", ComPort+1, HWComName);

  // Set a zero timeout for polled mode
  COMMTIMEOUTS ct;
  ct.ReadIntervalTimeout = MAXDWORD;
  ct.ReadTotalTimeoutConstant = 0;
  ct.ReadTotalTimeoutMultiplier = 0;
  ct.WriteTotalTimeoutConstant = 0;
  ct.WriteTotalTimeoutMultiplier = 0;
  SetCommTimeouts(HWComData[ComPort].hComPort, &ct);

  ConfigureHWComPort(ComPort);
}

void SERIAL_HW_Close(int ComPort)
{
  if ((ComPort < 0) || (ComPort >= 4)) return;

  if (HWComData[ComPort].hComPort != 0)
  {
    CloseHandle(HWComData[ComPort].hComPort);
    HWComData[ComPort].hComPort = 0;
  }
}

void SERIAL_HW_Configure(
  int              ComPort,
  int              Baud,
  int              DataBits,
  SerialParity_t   Parity,
  SerialStopBits_t StopBits)
{
  if ((ComPort < 0) || (ComPort >= 4)) return;

  HWComData[ComPort].BaudRate = Baud;
  HWComData[ComPort].DataBits = DataBits;

  switch (Parity)
  {
    case SERIAL_PARITY_NONE:
      HWComData[ComPort].Parity = NOPARITY;
      break;

    case SERIAL_PARITY_EVEN:
      HWComData[ComPort].Parity = EVENPARITY;
      break;

    case SERIAL_PARITY_ODD:
      HWComData[ComPort].Parity = ODDPARITY;
      break;

    case SERIAL_PARITY_MARK:
      HWComData[ComPort].Parity = MARKPARITY;
      break;

    case SERIAL_PARITY_SPACE:
      HWComData[ComPort].Parity = SPACEPARITY;
      break;

    default:
      printf("ERROR: SERIAL_HW_Configure: Invalid parity (%d)\n", Parity);
      break;
  }

  switch (StopBits)
  {
    case SERIAL_STOPBITS_1:
      HWComData[ComPort].StopBits = ONESTOPBIT;
      break;

    case SERIAL_STOPBITS_1_5:
      HWComData[ComPort].StopBits = ONE5STOPBITS;
      break;

    case SERIAL_STOPBITS_2:
      HWComData[ComPort].StopBits = TWOSTOPBITS;
      break;

    default:
      printf("ERROR: SERIAL_HW_Configure: Invalid Stop Bits (%d)\n", StopBits);
      break;
  }

  ConfigureHWComPort(ComPort);
}

int SERIAL_HW_Read(int ComPort, unsigned char *Buffer, int Count)
{
  DWORD nBytesRead = 0;

  if ((ComPort < 0) || (ComPort >= 4)) return 0;
  if (HWComData[ComPort].hComPort == 0) return 0;

  if (!ReadFile(HWComData[ComPort].hComPort, Buffer, Count, &nBytesRead, NULL))
  {
    printf("ReadFile failed\n");
    nBytesRead = 0;
  }

  return (int) nBytesRead;
}

int SERIAL_HW_Write(int ComPort, unsigned char *Buffer, int Count)
{
  DWORD nBytesWritten = 0;

  if ((ComPort < 0) || (ComPort >= 4)) return 0;
  if (HWComData[ComPort].hComPort == 0) return 0;

  if (!WriteFile(HWComData[ComPort].hComPort, Buffer, Count, &nBytesWritten, NULL))
  {
    printf("WriteFile failed\n");
    nBytesWritten = 0;
  }

  return (int) nBytesWritten;
}

void SERIAL_HW_SetDTR(int ComPort, bool Active)
{
  if ((ComPort < 0) || (ComPort >= 4)) return;
  if (HWComData[ComPort].hComPort == 0) return;
  HWComData[ComPort].DTR_High = Active;

  if (!EscapeCommFunction(HWComData[ComPort].hComPort,
                          HWComData[ComPort].DTR_High ? SETDTR  : CLRDTR))
  {
    printf("Set DTR failed\n");
  }
}

void SERIAL_HW_SetRTS(int ComPort, bool Active)
{
  if ((ComPort < 0) || (ComPort >= 4)) return;
  if (HWComData[ComPort].hComPort == 0) return;
  HWComData[ComPort].RTS_High = Active;

  if (!EscapeCommFunction(HWComData[ComPort].hComPort,
                          HWComData[ComPort].RTS_High ? SETRTS  : CLRRTS))
  {
    printf("SetRTS failed\n");
  }
}

void SERIAL_HW_GetModemStatusBits(int ComPort, unsigned char &MSRBits)
{
  DWORD ModemStat;

  MSRBits = 0;

  if ((ComPort < 0) || (ComPort >= 4)) return;
  if (HWComData[ComPort].hComPort == 0) return;

  // Get modem status
  GetCommModemStatus(HWComData[ComPort].hComPort, &ModemStat);

  // Set the new MSR state bits
  MSRBits |= (ModemStat & MS_CTS_ON) ? 0x10 : 0x00;
  MSRBits |= (ModemStat & MS_DSR_ON) ? 0X20 : 0X00;
  MSRBits |= (ModemStat & MS_RING_ON) ? 0x40 : 0x00;
  MSRBits |= (ModemStat & MS_RLSD_ON) ? 0x80 : 0x00;
}

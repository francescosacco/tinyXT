// =============================================================================
// File: dummy_serial_hw.h
//
// Description:
// Dummy Implementation for the serial HW interface to real serial ports.
// For use on systems with not serial ports.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#include "serial_hw.h"

// =============================================================================
// Exported functions
//

void SERIAL_HW_Initialise(void)
{

}

void SERIAL_HW_Open(int ComPort, const char *HWComName)
{

}

void SERIAL_HW_Close(int ComPort)
{

}

void SERIAL_HW_Configure(
  int              ComPort,
  int              Baud,
  int              DataBits,
  SerialParity_t   Parity,
  SerialStopBits_t StopBits)
{

}

int SERIAL_HW_Read(int ComPort, unsigned char *Buffer, int Count)
{
  return 0;
}

int SERIAL_HW_Write(int ComPort, unsigned char *Buffer, int Count)
{
  return 0;
}

void SERIAL_HW_SetDTR(int ComPort, bool Active)
{
}

void SERIAL_HW_SetRTS(int ComPort, bool Active)
{
}

void SERIAL_HW_GetModemStatusBits(int ComPort, unsigned char &MSRBits)
{
  MSRBits = 0;
}

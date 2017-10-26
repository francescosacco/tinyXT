// =============================================================================
// File: serial_hw.h
//
// Description:
// Interface specification for the serial HW interface to real serial ports
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#ifndef __SERIAL_HW_H
#define __SERIAL_HW_H

enum SerialStopBits_t
{
  SERIAL_STOPBITS_1,
  SERIAL_STOPBITS_1_5,
  SERIAL_STOPBITS_2
};

enum SerialParity_t
{
  SERIAL_PARITY_NONE,
  SERIAL_PARITY_EVEN,
  SERIAL_PARITY_ODD,
  SERIAL_PARITY_MARK,
  SERIAL_PARITY_SPACE
};

// Function: SERIAL_HW_Initialise
//
// Description:
// Initialise the Serial Hardware module.
//
// Parameters:
//
//   None.
//
// Returns:
//
//   None.
//
void SERIAL_HW_Initialise(void);

// Function: SERIAL_HW_Open
//
// Description:
// Open HWComName for emulated COM port.
//
// Parameters:
//
//   ComPort : The emulated COM port number.
//
//   HWComName : The name of the HW COM port device
//
// Returns:
//
//   None.
//
void SERIAL_HW_Open(int ComPort, const char *HWComName);

// Function: SERIAL_HW_Close
//
// Description:
// Close the HW COM Port opened for an emulated COM port.
//
// Parameters:
//
//   ComPort : The emulated COM port number.
//
// Returns:
//
//   None.
//
void SERIAL_HW_Close(int ComPort);

// Function: SERIAL_HW_Configure
//
// Description:
// Set the serial port configuration for a HW COM Port.
//
// Parameters:
//
//   ComPort : The emulated COM port number.
//
//   Baud    : The baud rate
//
//   DataBits : the number of data bits (5 .. 8)
//
//   Parity   : The parity type
//
//   StopBits : The number of stop bits
//
// Returns:
//
//   None.
//
void SERIAL_HW_Configure(
  int              ComPort,
  int              Baud,
  int              DataBits,
  SerialParity_t   Parity,
  SerialStopBits_t StopBits);

// Function: SERIAL_HW_Read
//
// Description:
// Read data from the HW COM Port opened for an emulated COM port.
//
// Parameters:
//
//   ComPort : The emulated COM port number.
//
//   Buffer  : The buffer to receive bytes, must be at least Count bytes long.
//
//   Count   : The maximum number of bytes to read.
//
// Returns:
//
//   int : The number of bytes actually read.
//
int SERIAL_HW_Read(int ComPort, unsigned char *Buffer, int Count);

// Function: SERIAL_HW_Write
//
// Description:
// Write data to the HW COM Port opened for an emulated COM port.
//
// Parameters:
//
//   ComPort : The emulated COM port number.
//
//   Buffer  : The buffer of bytes to write, must be at least Count bytes long.
//
//   Count   : The number of bytes to write.
//
// Returns:
//
//   int : The number of bytes actually written.
//
int SERIAL_HW_Write(int ComPort, unsigned char *Buffer, int Count);

// Function: SERIAL_HW_SetDTR
//
// Description:
// Set the HW COM Port DTR state for an emulated COM port.
//
// Parameters:
//
//   ComPort : The emulated COM port number.
//
//   Active  : The new DTR state.
//
// Returns:
//
//   None.
//
void SERIAL_HW_SetDTR(int ComPort, bool Active);

// Function: SERIAL_HW_SetRTS
//
// Description:
// Set the HW COM Port RTS state for an emulated COM port.
//
// Parameters:
//
//   ComPort : The emulated COM port number.
//
//   Active  : The new RTS state.
//
// Returns:
//
//   None.
//
void SERIAL_HW_SetRTS(int ComPort, bool Active);

// Function: SERIAL_HW_GetModemStatusBits
//
// Description:
// Get the current Modem Status for a HW COM Port opened for an emulated
//  COM port.
//
// Parameters:
//
//   ComPort : The emulated COM port number.
//
//   MSRBits : This is set to the 16550 MSR bits set for the current modem
//             status.
//
// Returns:
//
//   None.
//
void SERIAL_HW_GetModemStatusBits(int ComPort, unsigned char &MSRBits);

#endif // __SERIAL_HW_H

// =============================================================================
// File: serial_emulation.h
//
// Description:
// Common implementation of serial emulation.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#ifndef __SERIAL_EMULATION_H
#define __SERIAL_EMULATION_H

#include <windows.h>
#include <stdio.h>

enum SerialMapping_t
{
  SERIAL_UNUSED,     // Emulated serial port is unused
  SERIAL_MOUSE,      // Emulated serial port is a mouse
  SERIAL_TCP_SERVER, // TCP/IP stream socket as server
  SERIAL_TCP_CLIENT, // TCP/IP stream socket as client
  SERIAL_COM         // Emulated serial port -> Hardware COM port
};

// =============================================================================
// Function: SERIAL_Initialise
//
// Description:
// Initialise the serial port emulation.
//
// Parameters:
//
//   None.
//
// Returns:
//
//   None.
//
void SERIAL_Initialise(void);

// =============================================================================
// Function: SERIAL_Reset
//
// Description:
// Reset the serial port emulation.
//
// Parameters:
//
//   None.
//
// Returns:
//
//   None.
//
void SERIAL_Reset(void);

// =============================================================================
// Function: SERIAL_Cleanup
//
// Description:
// Clean up the serial port emulation.
//
// Parameters:
//
//   None.
//
// Returns:
//
//   None.
//
void SERIAL_Cleanup(void);

// =============================================================================
// Function: SERIAL_ReadConfig
//
// Description:
// Read the serial port configuration for a file.
//
// Parameters:
//
//   fp : a pointer to the configuration file to being read.
//
// Returns:
//
//   None.
//
void SERIAL_ReadConfig(FILE *fp);

// =============================================================================
// Function: SERIAL_WriteConfig
//
// Description:
// Write the current serial port configuration to a file.
//
// Parameters:
//
//   fp : a pointer to the configuration file to be written.
//
// Returns:
//
//   None.
//
void SERIAL_WriteConfig(FILE *fp);

// =============================================================================
// Function: SERIAL_GetConfig
//
// Description:
// Get the configuration data for a serial port.
//
// Parameters:
//
//   ComPort : The serial port
//
//   Mapping : This is set to the current serial port mapping
//
//   ComName : This is set to a pointer to the COM name string (128 bytes)
//
//   TCPAddress : This is set to a pointer to the TCP Address string (128 bytes)
//
//   TCPPort    : This is set to a pointer to the TCP Port string (64 bytes)
//
// Returns:
//
//   None.
//
void SERIAL_GetConfig(
  int ComPort,
  SerialMapping_t &Mapping,
  char * &ComName,
  char * &TCPAddress,
  char * &TCPPort);

// =============================================================================
// Function: SERIAL_Configure
//
// Description:
// Configure a serial port as specified.
//
// Parameters:
//
//   ComPort : The emulated serial port number (0 to 3)
//
//   Mapping : The type of serial port emulation to use
//
//   ComName : If linking to a H/W com port this is the com port name
//
// Returns:
//
//   bool : true if the emulated com port was configured successfully.
//
bool SERIAL_Configure(int ComPort, SerialMapping_t Mapping, const char *ComName);

// =============================================================================
// Function: SERIAL_MouseMove
//
// Description:
// Pass mouse movements for emulated serial mouse in the serial port emulation.
//
// Parameters:
//
//   dx : delta x mouse movement
//
//   dy : delta y mouse movement
//
//  LButtonDown : set to true if the left button is currently pressed
//
//  RButtonDown : set to true if the right button is currently pressed
//
// Returns:
//
//   None.
//
void SERIAL_MouseMove(int dx, int dy, bool LButtonDown, bool RButtonDown);

// =============================================================================
// Function: SERIAL_HandleSerial
//
// Description:
// Handle serial port processing.
// This should be called periodically.
//
// Parameters:
//
//   None.
//
// Returns:
//
//   None.
//
void SERIAL_HandleSerial(void);

// =============================================================================
// Function: SERIAL_WritePort
//
// Description:
// Write serial I/O port.
//
// Parameters:
//
//   Address : The port address
//
//   Val : The value to write to the port.
//
// Returns:
//
//   bool : true if this port is associated with the emulated serial ports
//          otherwise false.
//
bool SERIAL_WritePort(int Address, unsigned char Val);

// =============================================================================
// Function: SERIAL_ReadPort
//
// Description:
// Read serial I/O port.
//
// Parameters:
//
//   Address : The port address
//
//   Val : If the port is associated with the serial port emulation then
//         this is set to the port value read.
//
// Returns:
//
//   bool : true if this I/O port associated with an emulated serial port.
//          otherwise false.
//
bool SERIAL_ReadPort(int Address, unsigned char &Val);

// =============================================================================
// Function: SERIAL_IntPending
//
// Description:
// Determine if a serial port interrupt is pending.
//
// Parameters:
//
//   IntNo : If an interrupt is pending then this is set to the interrupt
//           number.
//
// Returns:
//
//   bool : true if an interrupt is pending, otherwise false.
//
bool SERIAL_IntPending(int &IntNo);

#endif // __WIN32_SERIAL_H

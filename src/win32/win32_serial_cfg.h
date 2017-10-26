// =============================================================================
// File: win32_serial.h
//
// Description:
// Win32 implementation of serial emulation.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#ifndef __WIN32_SERIAL_CFG_H
#define __WIN32_SERIAL_CFG_H

#include <windows.h>

// =============================================================================
// Function: SERIAL_ConfigDialog
//
// Description:
// Run the serial port configuration dialog..
//
// Parameters:
//
//   hInstance : the application instance
//
//   hwndParent : the paren-t window handle
//
// Returns:
//
//   None.
//
void SERIAL_ConfigDialog(HINSTANCE hInstance, HWND hwndParent);

#endif // __WIN32_SERIAL_CFG_H

// =============================================================================
// File: win32_cpu_speed_dialog.h
//
// Description:
// Win32 CPU speed dialog.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#ifndef __WIN32_CPU_SPEED_DIALOG_H
#define __WIN32_CPU_SPEED_DIALOG_H

#include <windows.h>

// =============================================================================
// Function: CPU_SPEED_Dialog
//
// Description:
// Run the CPU speed dialog.
//
// Parameters:
//
//   hInstance : the application instance.
//
//   hwndParent : the parent window
//
//   CPUSpeed : The CPU speed.
//              This must be set by the caller to the current CPU speed.
//              On exit this is set to the new CPU speed.
//
// Returns:
//
//   bool : true if a new CPU speed was selected.
//
bool CPU_SPEED_Dialog(HINSTANCE hInstance, HWND hwndParent, int &CPUSpeed);

#endif // __WIN32_CPU_SPEED_DIALOG_H

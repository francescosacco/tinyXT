// =============================================================================
// File: win32_cga.h
//
// Description:
// Win32 implementation of MCGA emulation.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#ifndef __WIN32_CGA_H
#define __WIN32_CGA_H

//
// Text display modes
//
enum TextDisplay_t
{
  TD_CGA,       // CGA 8x8 font
  TD_VGA_8x16   // VGA 8x16 font
};

// =============================================================================
// Function: CGA_Initialise
//
// Description:
// Initialise the CGA emulation module.
//
// Parameters:
//
//   None.
//
// Returns:
//
//   None.
//
void CGA_Initialise(void);

// =============================================================================
// Function: CGA_Reset
//
// Description:
// Reset the CGA emulation module.
//
// Parameters:
//
//   None.
//
// Returns:
//
//   None.
//
void CGA_Reset(void);

// =============================================================================
// Function: CGA_Cleanup
//
// Description:
// Clean up the CGA emulation module.
//
// Parameters:
//
//   None.
//
// Returns:
//
//   None.
//
void CGA_Cleanup(void);

// =============================================================================
// Function: CGA_VMemRead
//
// Description:
// Read from video memory.
//
// Parameters:
//
//   mem : pointer to emulation RAM
//
//   i_w : indicates word access.
//
//   addr : RAM address to read
//
// Returns:
//
//   Value read.
//
unsigned int CGA_VMemRead(unsigned char *mem, int i_w, int addr);

// =============================================================================
// Function: CGA_VMemWrite
//
// Description:
// Write to video memory.
//
// Parameters:
//
//   mem : pointer to emulation RAM
//
//   i_w : indicates word access.
//
//   addr : RAM address to write
//
//   val  : Value to write
//
// Returns:
//
//   Nothing useful, but return value cannot be void due to usage.
//
unsigned int CGA_VMemWrite(unsigned char *mem, int i_w, int addr, unsigned int val);

// =============================================================================
// Function: CGA_WritePort
//
// Description:
// Write to a CGA I/O port.
//
// Parameters:
//
//   Address : The port address
//
//   Val : The value to write to the port.
//
// Returns:
//
//   bool : true if this port is associated with the emulated CGA card
//          otherwise false.
//
bool CGA_WritePort(int Address, unsigned char Val);

// =============================================================================
// Function: CGA_ReadPort
//
// Description:
// Read from a CGa I/O port.
//
//   Address : The port address
//
//   Val : If the port is associated with the emulated CGA card then
//         this is set to the port value read.
//
// Returns:
//
//   bool : true if this I/O port associated with an emulated CGA card
//          otherwise false.
//
bool CGA_ReadPort(int Address, unsigned char &Val);

// =============================================================================
// Function: CGA_VBlankStart
//
// Description:
// Notify the CGA emulation of the start of vertical blanking.
//
// Parameters:
//
//   None.
//
// Returns:
//
//   None.
//
void CGA_VBlankStart(void);

// =============================================================================
// Function: CGA_SetTextDisplay
//
// Description:
// Set the text display type for text modes.
// This allows nicer looking VGA fonts to be used for text modes.
//
// Parameters:
//
//   Mode : The text display mode.
//
// Returns:
//
//   None.
//
void CGA_SetTextDisplay(TextDisplay_t Mode);

// =============================================================================
// Function: CGA_SetScale
//
// Description:
// Set the display scaling.
//
// Parameters:
//
//   Scale : Display scaling.
//
// Returns:
//
//   None.
//
void CGA_SetScale(int Scale);

// =============================================================================
// Function: CGA_GetDisplaySize
//
// Description:
// Get the width and height currently required for the CGA display
//
// Parameters:
//
//   w : this is set to the required width
//
//   h : this is set to the required height
//
// Returns:
//
//   None.
//
void CGA_GetDisplaySize(int &w, int &h);

// =============================================================================
// Function: CGA_DrawScreen
//
// Description:
// Draw the current CGA screen to the specified window.
//
// Parameters:
//
//   hwnd : The display window.
//
//   mem : The current system memory
//
// Returns:
//
//   None.
//
void CGA_DrawScreen(HWND hwnd, unsigned char *mem);

#endif // __WIN32_CGA_H

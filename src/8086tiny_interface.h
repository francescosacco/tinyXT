// =============================================================================
// File: win32_8086tiny_interface.h
//
// Description:
// 8086tiny interface class.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//
#ifndef __8086TINY_INTERFACE_H
#define __8086TINY_INTERFACE_H

#if defined(_WIN32)
#include <Windows.h>
#endif

// Define INTERCEPT_VMEM if your video card emulation requires
// intercepting all reads and writes to the video card memory.
// Intercepting VMEM access will slow things down a little.
#define INTERCEPT_VMEM
// Macro to check if an address is in the video memory pages.
#define IS_VMEM(X) (((X) >= 0xA0000) && ((X) < 0xC0000))

class T8086TinyInterface_t
{
public:
  T8086TinyInterface_t();
  ~T8086TinyInterface_t();

#if defined(_WIN32)
  // Function: SetInstance
  //
  // Description:
  // Windows functions need to know the instance.
  //
  // Parameters :
  //
  //   hInst : The instance handle for this application.
  //
  // Returns:
  //
  //   None.
  //
  void SetInstance(HINSTANCE hInst);
#endif

  // Function: Initialise
  //
  // Description:
  // Call at start.
  // Performs and once-off initialisation required.
  //
  // Returns:
  //
  //   bool : true if the initialisation was successful.
  //
  bool Initialise(unsigned char *mem_in);

  // Function: Cleanup
  //
  // Description:
  // Call at end.
  // Release any resources.
  //
  // Parameters:
  //
  //   None.
  //
  // Returns:
  //
  //   None.
  //
  void Cleanup(void);

  // Function: ExitEmulation
  //
  // Description:
  // Check if emulation should exit.
  //
  // Parameters:
  //
  //   None.
  //
  // Returns:
  //
  //   bool : Returns true when the emulation should exit. (eg window closed)
  //
  bool ExitEmulation(void);

  // Function: Reset
  //
  // Description:
  // Checks if the emulation should reset.
  //
  // Parameters:
  //
  //   None.
  //
  // Returns:
  //
  //   bool : Returns true when the reset option has been selected.
  //
  bool Reset(void);

  // Function: GetBIOSFilename
  //
  // Description:
  // Gets the currently selected BIOS filename.
  //
  // Parameters:
  //
  //   None.
  //
  // Returns:
  //
  //   char * : The current BIOS filename.
  //
  char *GetBIOSFilename(void);

  // Function: GetFDFilename
  //
  // Description:
  // Gets the currently selected FD filename.
  //
  // Parameters:
  //
  //   None.
  //
  // Returns:
  //
  //   char * : The current FD filename.
  //
  char *GetFDImageFilename(void);

  // Function: GetHDilename
  //
  // Description:
  // Gets the currently selected HD filename.
  //
  // Parameters:
  //
  //   None.
  //
  // Returns:
  //
  //   char * : The current HD filename.
  //
  char *GetHDImageFilename(void);

  // Function: FDChanged
  //
  // Description:
  // Tell when FD images is changed.
  //
  // Parameters:
  //
  //   None.
  //
  // Returns:
  //
  //   bool : true if the FD image selected has been changed.
  //
  bool FDChanged(void);

  // Function: TimerTick
  //
  // Description:
  // Call this every instruction to update the HW emulation.
  //
  // Parameters:
  //
  //   nTicks : The number of CPU ticks elapsed in the instruction
  //
  // Returns:
  //
  //   bool : true if the update caused a state change.
  //          If true then the called needs to check:
  //            ExitEmulation()
  //            Reset()
  //            FDChanged()
  //          To find out what has changed.
  //
  bool TimerTick(int nTicks);

  // Function: CheckBreakPoints
  //
  // Description:
  // Check if a break point has been triggered.
  // Normally break points are checked in TimerTick(), however the way H/W
  // interrupts are handled means that the TimerTick will miss the address
  // for the first instructions of H/W interrupt handlers.
  // This needs to be called after a H/W interrupt is triggered to
  // check if the new address is a break point.
  //
  // Parameters:
  //
  //   None.
  //
  // Returns:
  //
  //   None.
  //
  void CheckBreakPoints(void);

  // Function: WritePort
  //
  // Description:
  // Write to an I/O Port
  //
  // Parameters:
  //
  //   Address : the I/O Port address
  //
  //   Value : the value to write to the I/O port
  //
  // Returns:
  //
  //   None.
  //
  void WritePort(int Address, unsigned char Value);

  // Function: ReadPort
  //
  // Description:
  // Read from an I/O Port
  //
  // Parameters:
  //
  //   Address : the I/O Port address
  //
  // Returns:
  //
  //   unsigned char : The value read form the I/O port
  //
  unsigned char ReadPort(int Address);

  unsigned int VMemRead(int i_w, int addr);

  unsigned int VMemWrite(int i_w, int addr, unsigned int val);

  // Function: IntPending
  //
  // Description:
  // Checks if a hardware interrupt is pending.
  //
  // Parameters:
  //
  //   IntNumber : If an interrupt is pending then this is set to the
  //               interrupt number of the highest priority interrupt
  //               pending.
  //
  // Returns:
  //
  //   bool : true when an interrupt is pending.
  //
  bool IntPending(int &IntNumber);

private:

  unsigned char Port[65536];
  unsigned char *mem;

#if defined(_WIN32)
  HINSTANCE hInstance;
#endif
};

#endif

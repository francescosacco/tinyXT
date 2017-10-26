// =============================================================================
// File: win32_cpu_speed_dialog.cpp
//
// Description:
// Win32 CPU speed dialog.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#include <stdio.h>

#include "win32_cpu_speed_dialog.h"
#include "resource.h"

// =============================================================================
// Local Functions
//

static HWND ConfigParent;
static int Speed = 477000000;

// =============================================================================
// FUNCTION: CPUSpeedDialogProc
//
// DESCRIPTION:
// Windows callback for the CPU speed dialog.
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
static BOOL CALLBACK CPUSpeedDialogProc(
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

  char Buffer[32];

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
      GetWindowRect(ConfigParent, &WRect);
      WindowWidth = (WRect.right - WRect.left) + 1;
      WindowHeight = (WRect.bottom - WRect.top) + 1;

      GetWindowRect(hwnd, &DRect);
      DialogWidth = (DRect.right - DRect.left) + 1;
      DialogHeight = (DRect.bottom - DRect.top) + 1;

      DialogX = WRect.left + (WindowWidth - DialogWidth) / 2;
      DialogY = WRect.top + (WindowHeight - DialogHeight) / 2;

      SetWindowPos(hwnd, NULL, DialogX, DialogY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

      sprintf(Buffer, "%d", Speed);
      SendDlgItemMessage(hwnd, IDC_CPU_SPEED_COMBO, WM_SETTEXT, 0, (LPARAM) Buffer);

      SendDlgItemMessage(hwnd, IDC_CPU_SPEED_COMBO, CB_ADDSTRING, 0, (LPARAM) "4770000");
      SendDlgItemMessage(hwnd, IDC_CPU_SPEED_COMBO, CB_ADDSTRING, 0, (LPARAM) "8000000");
      SendDlgItemMessage(hwnd, IDC_CPU_SPEED_COMBO, CB_ADDSTRING, 0, (LPARAM) "12000000");
      SendDlgItemMessage(hwnd, IDC_CPU_SPEED_COMBO, CB_ADDSTRING, 0, (LPARAM) "16000000");
      SendDlgItemMessage(hwnd, IDC_CPU_SPEED_COMBO, CB_ADDSTRING, 0, (LPARAM) "20000000");

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
            case IDOK:
              SendDlgItemMessage(hwnd, IDC_CPU_SPEED_COMBO, WM_GETTEXT, 32, (LPARAM) Buffer);
              Speed = atoi(Buffer);
              EndDialog(hwnd, IDOK);
              break;

            case IDCANCEL:
              EndDialog(hwnd, IDCANCEL);
              break;

            default:
              break;
          }
          break;
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
// Exported Functions
//

bool CPU_SPEED_Dialog(HINSTANCE hInstance, HWND hwndParent, int &CPUSpeed)
{
  DWORD res;

  ConfigParent = hwndParent;

  Speed = CPUSpeed;

  res = DialogBox(
      hInstance,
      MAKEINTRESOURCE(IDD_DIALOG_CPU_SPEED),
      hwndParent,
      CPUSpeedDialogProc);

  if (res == IDOK)
  {
    CPUSpeed = Speed;
    return true;
  }

  return false;
}

// =============================================================================
// File: win32_serial.cpp
//
// Description:
// Win32 implementation of serial emulation.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#include "win32_serial_cfg.h"
#include "serial_emulation.h"
#include "resource.h"

//
// Serial port configuration dialog functions
//

static HWND ConfigParent = 0;
static int nComPorts = 0;
static char strSerialList[20][128];

static int EnumCommNames(void)
{
  LONG Status;

  HKEY  hKey;
  DWORD dwIndex = 0;
  CHAR  Name[48];
  DWORD szName;
  UCHAR PortName[48];
  DWORD szPortName;
  DWORD Type;

  nComPorts = 0;

  if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                   TEXT("HARDWARE\\DEVICEMAP\\SERIALCOMM"),
                   0,
                   KEY_READ,
                   &hKey) != ERROR_SUCCESS)
  {
    return -1;
  }

  do
  {
    szName = sizeof(Name);
    szPortName = sizeof(PortName);

    Status = RegEnumValue(hKey,
                         dwIndex++,
                         Name,
                         &szName,
                         NULL,
                         &Type,
                         PortName,
                         &szPortName);

    if (Status == ERROR_SUCCESS)
    {
      strcpy(strSerialList[nComPorts], (char *) PortName);
      nComPorts++;
    }
  } while((Status == ERROR_SUCCESS) );

  RegCloseKey(hKey);

  return nComPorts;
}

// =============================================================================
// FUNCTION: SerialConfigDialogProc
//
// DESCRIPTION:
// Windows callback for the serial configuration dialog.
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
static BOOL CALLBACK SerialConfigDialogProc(
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

      EnumCommNames();

      for (int ComPort = 0 ; ComPort < 4 ; ComPort++)
      {
        SerialMapping_t Mapping;
        char *ComName;
        char *TCPAddress;
        char *TCPPort;

        SERIAL_GetConfig(ComPort, Mapping, ComName, TCPAddress, TCPPort);

        SendDlgItemMessage(hwnd, IDC_COMBO_COM1 + ComPort, CB_ADDSTRING, 0, (LPARAM) "Unused");
        SendDlgItemMessage(hwnd, IDC_COMBO_COM1 + ComPort, CB_ADDSTRING, 0, (LPARAM) "Mouse");
        SendDlgItemMessage(hwnd, IDC_COMBO_COM1 + ComPort, CB_ADDSTRING, 0, (LPARAM) "TCP Server");
        SendDlgItemMessage(hwnd, IDC_COMBO_COM1 + ComPort, CB_ADDSTRING, 0, (LPARAM) "TCP Client");
        for (int i = 0 ; i < nComPorts ; i++)
        {
          SendDlgItemMessage(hwnd, IDC_COMBO_COM1 + ComPort, CB_ADDSTRING, 0, (LPARAM) strSerialList[i]);
        }

        SendDlgItemMessage(hwnd, IDC_COMBO_COM1 + ComPort, CB_SETCURSEL, Mapping, (LPARAM) 0);

        SendDlgItemMessage(hwnd, IDC_EDIT_COM1_ADDR+ComPort, WM_SETTEXT, 0, (LPARAM) TCPAddress);
        SendDlgItemMessage(hwnd, IDC_EDIT_COM1_PORT+ComPort, WM_SETTEXT, 0, (LPARAM)TCPPort);

        if (Mapping == SERIAL_TCP_SERVER)
        {
          SendDlgItemMessage(hwnd, IDC_EDIT_COM1_ADDR+ComPort, WM_ENABLE, FALSE, (LPARAM) 0);
          SendDlgItemMessage(hwnd, IDC_EDIT_COM1_PORT+ComPort, WM_ENABLE, TRUE, (LPARAM) 0);
        }
        else if (Mapping == SERIAL_TCP_CLIENT)
        {
          SendDlgItemMessage(hwnd, IDC_EDIT_COM1_ADDR+ComPort, WM_ENABLE, TRUE, (LPARAM) 0);
          SendDlgItemMessage(hwnd, IDC_EDIT_COM1_PORT+ComPort, WM_ENABLE, TRUE, (LPARAM) 0);
        }
        else
        {
          SendDlgItemMessage(hwnd, IDC_EDIT_COM1_ADDR+ComPort, WM_ENABLE, FALSE, (LPARAM) 0);
          SendDlgItemMessage(hwnd, IDC_EDIT_COM1_PORT+ComPort, WM_ENABLE, FALSE, (LPARAM) 0);
        }
      }
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
              for (int ComPort = 0 ; ComPort < 4 ; ComPort++)
              {
                SERIAL_Configure(ComPort, SERIAL_UNUSED, NULL);
              }

              for (int ComPort = 0 ; ComPort < 4 ; ComPort++)
              {
                SerialMapping_t Mapping;
                char *ComName;
                char *TCPAddress;
                char *TCPPort;

                SERIAL_GetConfig(ComPort, Mapping, ComName, TCPAddress, TCPPort);

                int Sel = SendDlgItemMessage(hwnd, IDC_COMBO_COM1 + ComPort, CB_GETCURSEL, 0, 0);

                SendDlgItemMessage(hwnd, IDC_EDIT_COM1_ADDR+ComPort, WM_GETTEXT, 128, (LPARAM) TCPAddress);
                SendDlgItemMessage(hwnd, IDC_EDIT_COM1_PORT+ComPort, WM_GETTEXT, 64, (LPARAM) TCPPort);

                if (Sel < SERIAL_COM)
                {
                  SERIAL_Configure(ComPort, (SerialMapping_t) Sel, NULL);
                }
                else
                {
                  SERIAL_Configure(ComPort, SERIAL_COM, strSerialList[Sel - SERIAL_COM]);
                }
              }
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

        case CBN_SELCHANGE:
        {
          switch (wID)
          {
            case IDC_COMBO_COM1:
            case IDC_COMBO_COM2:
            case IDC_COMBO_COM3:
            case IDC_COMBO_COM4:
            {
              int ComPort = wID - IDC_COMBO_COM1;
              int Sel = SendDlgItemMessage(hwnd, IDC_COMBO_COM1 + ComPort, CB_GETCURSEL, 0, 0);

              if (Sel == SERIAL_TCP_SERVER)
              {
                SendDlgItemMessage(hwnd, IDC_EDIT_COM1_ADDR+ComPort, WM_ENABLE, FALSE, (LPARAM) 0);
                SendDlgItemMessage(hwnd, IDC_EDIT_COM1_PORT+ComPort, WM_ENABLE, TRUE, (LPARAM) 0);
              }
              else if (Sel == SERIAL_TCP_CLIENT)
              {
                SendDlgItemMessage(hwnd, IDC_EDIT_COM1_ADDR+ComPort, WM_ENABLE, TRUE, (LPARAM) 0);
                SendDlgItemMessage(hwnd, IDC_EDIT_COM1_PORT+ComPort, WM_ENABLE, TRUE, (LPARAM) 0);
              }
              else
              {
                SendDlgItemMessage(hwnd, IDC_EDIT_COM1_ADDR+ComPort, WM_ENABLE, FALSE, (LPARAM) 0);
                SendDlgItemMessage(hwnd, IDC_EDIT_COM1_PORT+ComPort, WM_ENABLE, FALSE, (LPARAM) 0);
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

void SERIAL_ConfigDialog(HINSTANCE hInstance, HWND hwndParent)
{
  ConfigParent = hwndParent;

  DialogBox(
      hInstance,
      MAKEINTRESOURCE(IDD_DIALOG_SERIAL_CFG),
      hwndParent,
      SerialConfigDialogProc);
}


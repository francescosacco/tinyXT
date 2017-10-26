// =============================================================================
// File: win32_sound_cfg.cpp
//
// Description:
// Win32 sound configuration settings and dialog.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#include <windows.h>
#include <commctrl.h>

#include "win32_sound_cfg.h"
#include "resource.h"

// =============================================================================
// Exported Variables
//

bool SoundEnabled = true;

int AudioSampleRate = 48000;

int VolumePercent = 100;
int VolumeSample = 16384;

// =============================================================================
// Local Functions
//

static HWND ConfigParent = 0;

// =============================================================================
// FUNCTION: SNDCFG_DialogProc
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
static BOOL CALLBACK SNDCFG_DialogProc(
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
  int SelIdx;

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

      SendDlgItemMessage(hwnd,
                         IDC_ENABLE_PC_SPEAKER,
                         BM_SETCHECK,
                         (SoundEnabled) ? BST_CHECKED : BST_UNCHECKED,
                         (LPARAM) 0);

      SendDlgItemMessage(hwnd, IDC_SAMPLE_RATE_COMBO, CB_ADDSTRING, 0, (LPARAM) "22050");
      SendDlgItemMessage(hwnd, IDC_SAMPLE_RATE_COMBO, CB_ADDSTRING, 0, (LPARAM) "24000");
      SendDlgItemMessage(hwnd, IDC_SAMPLE_RATE_COMBO, CB_ADDSTRING, 0, (LPARAM) "44100");
      SendDlgItemMessage(hwnd, IDC_SAMPLE_RATE_COMBO, CB_ADDSTRING, 0, (LPARAM) "48000");

      sprintf(Buffer, "%d", AudioSampleRate);
      SelIdx = SendDlgItemMessage(hwnd, IDC_SAMPLE_RATE_COMBO, CB_FINDSTRING, -1, (LPARAM) Buffer);
      if (SelIdx == CB_ERR) SelIdx = 0;
      SendDlgItemMessage(hwnd, IDC_SAMPLE_RATE_COMBO, CB_SETCURSEL, SelIdx, (LPARAM) 0);

      SendDlgItemMessage(hwnd, IDC_VOLUME_SLIDER, TBM_SETRANGE, FALSE, MAKELPARAM(0, 100));
      SendDlgItemMessage(hwnd, IDC_VOLUME_SLIDER, TBM_SETPOS,   TRUE,  VolumePercent);

      sprintf(Buffer, "%d", VolumePercent);
      SendDlgItemMessage(hwnd, IDC_VOLUME_STATIC, WM_SETTEXT, 0, (LPARAM) Buffer);

      return(FALSE);

     case WM_HSCROLL:
      {
        int nc = LOWORD(wparam);
        if ((nc == TB_THUMBPOSITION) || (nc == TB_THUMBTRACK))
        {
          SelIdx = HIWORD(wparam);
        }
        else
        {
          SelIdx = SendDlgItemMessage(hwnd, IDC_VOLUME_SLIDER, TBM_GETPOS, 0, (LPARAM) 0);
        }
        sprintf(Buffer, "%d", SelIdx);
        SendDlgItemMessage(hwnd, IDC_VOLUME_STATIC, WM_SETTEXT, 0, (LPARAM) Buffer);
        break;
      }

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
              SoundEnabled =
                (SendDlgItemMessage(hwnd, IDC_ENABLE_PC_SPEAKER, BM_GETCHECK, 0, 0) == BST_CHECKED);

              SendDlgItemMessage(hwnd, IDC_SAMPLE_RATE_COMBO, WM_GETTEXT, 32, (LPARAM) Buffer);
              AudioSampleRate = atoi(Buffer);

              VolumePercent = SendDlgItemMessage(hwnd, IDC_VOLUME_SLIDER, TBM_GETPOS, 0, (LPARAM) 0);
              VolumeSample = (16384 * VolumePercent) / 100;

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

bool SNDCFG_Read(FILE *fp)
{
  char Line[256];
  int tmp;

  fgets(Line, 256, fp);
  if (strncmp(Line, "[SOUND_ENABLE]", 14) != 0) return 0;

  fgets(Line, 256, fp);
  sscanf(Line, "%d\n", &tmp);
  SoundEnabled = (tmp != 0);

  fgets(Line, 256, fp);
  if (strncmp(Line, "[SOUND_SAMPLE_RATE]", 19) != 0) return 0;
  fgets(Line, 256, fp);
  sscanf(Line, "%d\n", &AudioSampleRate);

  fgets(Line, 256, fp);
  if (strncmp(Line, "[SOUND_VOLUME]", 14) != 0) return 0;
  fgets(Line, 256, fp);
  sscanf(Line, "%d\n", &VolumePercent);
  VolumeSample = (VolumePercent * 16384) / 100;

  return true;
}

bool SNDCFG_Write(FILE *fp)
{
  fprintf(fp, "[SOUND_ENABLE]\n");
  fprintf(fp, "%d\n", SoundEnabled ? 1 : 0);
  fprintf(fp, "[SOUND_SAMPLE_RATE]\n");
  fprintf(fp, "%d\n", AudioSampleRate);
  fprintf(fp, "[SOUND_VOLUME]\n");
  fprintf(fp, "%d\n", VolumePercent);

  return true;
}

bool SNDCFG_Dialog(HINSTANCE hInstance, HWND hwnd)
{
  int res;
  ConfigParent = hwnd;

  res = DialogBox(
      hInstance,
      MAKEINTRESOURCE(IDD_DIALOG_SOUND_CFG),
      hwnd,
      SNDCFG_DialogProc);

  return (res == IDOK);
}


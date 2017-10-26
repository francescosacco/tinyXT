// =============================================================================
// File: file_dialog.cpp
//
// Description:
// Win32 implementation of file request dialog interface.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#include "file_dialog.h"
#include <windows.h>

bool OpenFileDialog(const char *Title, char *fname, int filelen, const char *filter)
{
  char ofnBuffer[1024];
  OPENFILENAME ofn;

  ofnBuffer[0] = 0;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = NULL; //hWnd;
  ofn.lpstrFilter = filter;
  ofn.nMaxFile = 1024;
  ofn.lpstrFile = ofnBuffer;
  ofn.lpstrTitle = Title;
  ofn.Flags = OFN_NOCHANGEDIR;

  if (GetOpenFileName(&ofn))
  {
    strncpy(fname, ofnBuffer, filelen);
    return true;
  }

  return false;
}

bool SaveFileDialog(const char *Title, char *fname, int filelen, const char *filter)
{
  char ofnBuffer[1024];
  OPENFILENAME ofn;

  ofnBuffer[0] = 0;
  memset(&ofn, 0, sizeof(ofn));
  ofn.lStructSize = sizeof(ofn);
  ofn.hwndOwner = NULL; //hWnd;
  ofn.lpstrFilter = filter;
  ofn.nMaxFile = 1024;
  ofn.lpstrFile = ofnBuffer;
  ofn.lpstrTitle = Title;
  ofn.Flags = OFN_NOCHANGEDIR;

  if (GetSaveFileName(&ofn))
  {
    strncpy(fname, ofnBuffer, filelen);
    return true;
  }

  return false;
}


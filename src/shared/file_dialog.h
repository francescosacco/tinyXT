// =============================================================================
// File: file_dialog.h
//
// Description:
// File request dialog interface.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#ifndef __FILE_DIALOG_H
#define __FILE_DIALOG_H

bool OpenFileDialog(const char *Title, char *fname, int filelen, const char *filter);

bool SaveFileDialog(const char *Title, char *fname, int filelen, const char *filter);

#endif // __FILE_DIALOG_H

// =============================================================================
// File: win32_snd_drv.h
//
// Description:
// Win32 sound driver.
// Based on example code from MSDN.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#ifndef __WIN32_SND_DRV_H
#define __WIN32_SND_DRV_H

#include <windows.h>

class CWaveBuffer
{
public:
  ~CWaveBuffer();
  BOOL Init(HWAVEOUT hWave, int Size);
  BOOL Write(PBYTE pData, int nBytes, int& BytesWritten);
  void Flush();
private:
  WAVEHDR      m_Hdr;
  HWAVEOUT     m_hWave;
  int          m_nBytes;
};

class CWaveOut
{
public:
  CWaveOut(LPCWAVEFORMATEX Format, int nBuffers, int BufferSize);
  ~CWaveOut();
  void Write(PBYTE Data, int nBytes);
  void Flush();
  void Wait();
  void Reset();
private:
  const HANDLE   m_hSem;
  const int      m_nBuffers;
  int            m_CurrentBuffer;
  BOOL           m_NoBuffer;
  CWaveBuffer   *m_Hdrs;
  HWAVEOUT       m_hWave;
};


#endif // __WIN32_SND_DRV_H

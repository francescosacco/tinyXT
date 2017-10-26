// =============================================================================
// File: win32_snd_drv.cpp
//
// Description:
// Win32 sound driver.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

#include "win32_snd_drv.h"

#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

BOOL CWaveBuffer::Init(HWAVEOUT hWave, int Size)
{
  m_hWave  = hWave;
  m_nBytes = 0;

  /*  Allocate a buffer and initialize the header. */
  m_Hdr.lpData = (LPSTR)LocalAlloc(LMEM_FIXED, Size);
  for (int i = 0 ; i < Size ; i++) m_Hdr.lpData[i] = 0;

  if (m_Hdr.lpData == NULL)
  {
      return FALSE;
  }
  m_Hdr.dwBufferLength  = Size;
  m_Hdr.dwBytesRecorded = 0;
  m_Hdr.dwUser = 0;
  m_Hdr.dwFlags = 0;
  m_Hdr.dwLoops = 0;
  m_Hdr.lpNext = 0;
  m_Hdr.reserved = 0;

  /*  Prepare it. */
  waveOutPrepareHeader(hWave, &m_Hdr, sizeof(WAVEHDR));
  return TRUE;
}

CWaveBuffer::~CWaveBuffer()
{
  if (m_Hdr.lpData)
  {
    waveOutUnprepareHeader(m_hWave, &m_Hdr, sizeof(WAVEHDR));
    LocalFree(m_Hdr.lpData);
  }
}

void CWaveBuffer::Flush()
{
  // ASSERT(m_nBytes != 0);
  m_nBytes = 0;
  waveOutWrite(m_hWave, &m_Hdr, sizeof(WAVEHDR));
}

BOOL CWaveBuffer::Write(PBYTE pData, int nBytes, int& BytesWritten)
{
  // ASSERT((DWORD)m_nBytes != m_Hdr.dwBufferLength);
  BytesWritten = min((int)m_Hdr.dwBufferLength - m_nBytes, nBytes);
  CopyMemory((PVOID)(m_Hdr.lpData + m_nBytes), (PVOID)pData, BytesWritten);
  m_nBytes += BytesWritten;
  if (m_nBytes == (int)m_Hdr.dwBufferLength)
  {
    /*  Write it! */
    m_nBytes = 0;
    waveOutWrite(m_hWave, &m_Hdr, sizeof(WAVEHDR));
    return TRUE;
  }
  return FALSE;
}

void CALLBACK WaveCallback(HWAVEOUT /* hWave */,
                           UINT uMsg,
                           DWORD dwUser,
                           DWORD /* dw1 */,
                           DWORD /* dw2 */)
{
  if (uMsg == WOM_DONE)
  {
    ReleaseSemaphore((HANDLE)dwUser, 1, NULL);
  }
}

CWaveOut::CWaveOut(LPCWAVEFORMATEX Format, int nBuffers, int BufferSize) :
    m_hSem(CreateSemaphore(NULL, nBuffers, nBuffers, NULL)),
    m_nBuffers(nBuffers),
    m_CurrentBuffer(0),
    m_NoBuffer(TRUE),
    m_Hdrs(new CWaveBuffer[nBuffers]),
    m_hWave(NULL)
{
  /*  Create wave device. */
  waveOutOpen(&m_hWave,
              WAVE_MAPPER,
              Format,
              (DWORD)WaveCallback,
              (DWORD)m_hSem,
              CALLBACK_FUNCTION);

  /*  Initialize the wave buffers. */
  for (int i = 0; i < nBuffers; i++)
  {
    m_Hdrs[i].Init(m_hWave, BufferSize);
  }
}

CWaveOut::~CWaveOut()
{
  /*  First, get the buffers back. */
  waveOutReset(m_hWave);
  /*  Free the buffers. */
  delete [] m_Hdrs;
  /*  Close the wave device. */
  waveOutClose(m_hWave);
  /*  Free the semaphore. */
  CloseHandle(m_hSem);
}

void CWaveOut::Flush()
{
  if (!m_NoBuffer)
  {
    m_Hdrs[m_CurrentBuffer].Flush();
    m_NoBuffer = TRUE;
    m_CurrentBuffer = (m_CurrentBuffer + 1) % m_nBuffers;
  }
}

void CWaveOut::Reset()
{
  waveOutReset(m_hWave);
}

void CWaveOut::Write(PBYTE pData, int nBytes)
{
  while (nBytes != 0)
  {
    /*  Get a buffer if necessary. */
    if (m_NoBuffer)
    {
      WaitForSingleObject(m_hSem, INFINITE);
      m_NoBuffer = FALSE;
    }

    /*  Write into a buffer. */
    int nWritten;
    if (m_Hdrs[m_CurrentBuffer].Write(pData, nBytes, nWritten))
    {
      m_NoBuffer = TRUE;
      m_CurrentBuffer = (m_CurrentBuffer + 1) % m_nBuffers;
      nBytes -= nWritten;
      pData += nWritten;
    }
    else
    {
      // ASSERT(nWritten == nBytes);
      break;
    }
  }
}

void CWaveOut::Wait()
{
  /*  Send any remaining buffers. */
  Flush();
  /*  Wait for the buffers back. */
  for (int i = 0; i < m_nBuffers; i++)
  {
    WaitForSingleObject(m_hSem, INFINITE);
  }
  LONG lPrevCount;
  ReleaseSemaphore(m_hSem, m_nBuffers, &lPrevCount);
}

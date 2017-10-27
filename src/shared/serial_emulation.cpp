// =============================================================================
// File: serial_emulation.cpp
//
// Description:
// Common implementation of serial emulation.
//
// This work is licensed under the MIT License. See included LICENSE.TXT.
//

// If under windows, need winsock for TCP/IP
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

#include <stdio.h>
#include <ctype.h>
#include "serial_emulation.h"
#include "serial_hw.h"

#define GET_TICKS timeGetTime

// =============================================================================
// Local Functions
//

#define FIFO_SIZE 16

struct ComPortInfo_t
{
  SerialMapping_t Mapping;
  char ComName[128];

  // TCP address and port
  char TCPAddress[128];
  char TCPPort[64];

  // Current register states
  unsigned char Reg[8];

  unsigned int  ConnectRetryTime;  // Time for the next connection retry
  SOCKET        listenSocket;      // For TCP Server, the listen socket
  SOCKET        commSocket;        // The comm data socket
  bool          IsSocketConnected; // Indicates if the socket is connected.

  bool DivisorLatch;

  int DivisorL;
  int DivisorH;
  int Divisor;

  int BaudRate; // Baud rate
  int DataBits; // Data bit specifier
  SerialStopBits_t StopBits; // Stop bits specifier
  SerialParity_t   Parity;   // Parity specifier

  int RxTriggerLevel;

  char RxBuffer[FIFO_SIZE];
  int  RxBufferLen;
  int  RxHead;
  int  RxTail;

  bool RTS_High;
  bool DTR_High;

  char TxBuffer[FIFO_SIZE];
  int  TxBufferLen;
  int  TxBufferLenI; // TxBufferLen when interrupt status register was last read
  int  TxHead;
  int  TxTail;

  unsigned char IIR;  // Interrupt Identification Register
  int IRQ;            // IRQ for this COM port
};

// Serial port mouse data
int SerialMousePort = -1;
bool MouseEventPending = false;
int Mouse_dx = 0;
int Mouse_dy = 0;
bool Mouse_LBPressed = false;
bool Mouse_RBPressed = false;
unsigned int MouseEventTime = 0;
bool MousePowerOn = false;     // Mouse is powered by the RTS line
bool MouseSendOK = false;      // Is the serial port configured for mouse?

// Com port data for each com port
ComPortInfo_t ComData[4];

// Winsock initialise status - windows only
bool WinsockInitialised = false;

// =============================================================================
// Local Functions
//

static int IOAddressToComPort(int Address, int &RegAddr)
{
  int ComPort = -1;

  // COM1 IRQ 4 Port 3F8
  // COM2 IRQ 3 Port 2F8
  // COM3 IRQ 4 Port 3E8
  // COM4 IRQ 3 Port 2E8

  if ((Address >= 0x03f8) && (Address <= 0x03ff))
  {
    ComPort = 0;
    RegAddr = Address - 0x03f8;
  }
  else if ((Address >= 0x02f8) && (Address <= 0x02ff))
  {
    ComPort = 1;
    RegAddr = Address - 0x02f8;
  }
  else if ((Address >= 0x03e8) && (Address <= 0x03ef))
  {
    ComPort = 2;
    RegAddr = Address - 0x03e8;
  }
  else if ((Address >= 0x02e8) && (Address <= 0x02ef))
  {
    ComPort = 3;
    RegAddr = Address - 0x02e8;
  }

  if (ComPort >= 0)
  {
    if (ComData[ComPort].Mapping == SERIAL_UNUSED)
    {
      ComPort = -1;
    }
  }

  return ComPort;
}

static void ConfigureComPort(int ComPort)
{
  if (ComData[ComPort].Mapping == SERIAL_MOUSE)
  {
    if ((ComData[ComPort].BaudRate == 1200) &&
        (ComData[ComPort].DataBits == 7) &&
        (ComData[ComPort].StopBits == ONESTOPBIT) &&
        (ComData[ComPort].Parity == NOPARITY))
    {
      MouseSendOK = true;
    }
    else
    {
      MouseSendOK = false;
    }
  }
  else if (ComData[ComPort].Mapping == SERIAL_COM)
  {
    // Configure HW serial port
    SERIAL_HW_Configure(ComPort,
                        ComData[ComPort].BaudRate,
                        ComData[ComPort].DataBits,
                        ComData[ComPort].Parity,
                        ComData[ComPort].StopBits);

    // Flush the receive buffer
    ComData[ComPort].RxBufferLen = 0;
    ComData[ComPort].RxHead = 0;
    ComData[ComPort].RxTail = 0;
  }
}

static void SetBaud(int ComPort)
{
  ComData[ComPort].Divisor =
    (ComData[ComPort].DivisorH << 8) | ComData[ComPort].DivisorL;

  if (ComData[ComPort].Divisor == 0) return;

  ComData[ComPort].BaudRate = 1843200 / (ComData[ComPort].Divisor * 16);

  ConfigureComPort(ComPort);
}

static void ReevaluateInterrupts(int ComPort)
{
  unsigned char IIR = 0x01; // No interrupt pending

  if ((ComData[ComPort].Reg[1] & 0x01) != 0)
  {
    // Received Data Available Interrupt enabled
    if (ComData[ComPort].RxBufferLen >= ComData[ComPort].RxTriggerLevel)
    {
      IIR = 0x04;
    }
  }

  if ((IIR == 0x01) && ((ComData[ComPort].Reg[1] & 0x02) != 0))
  {
    // Transmitter Holding Register Empty Interrupt enabled
    // Interrupt is raised when the buffer becomes empty.
    // This condition is cleared by reading the IIR
    if ((ComData[ComPort].TxBufferLen == 0) && (ComData[ComPort].TxBufferLenI > 0))
    {
      IIR = 0x02;
    }
  }

  if ((IIR == 0x01) && ((ComData[ComPort].Reg[1] & 0x04) != 0))
  {
    // Receiver Line Status Interrupt enabled.
    // We never generate any of these.
  }

  if ((IIR == 0x01) &&((ComData[ComPort].Reg[1] & 0x08) != 0))
  {
    // Modem Status Interrupt enabled
    if (ComData[ComPort].Reg[6] & 0x0f)
    {
      IIR = 0x00;
    }
  }

  ComData[ComPort].IIR = IIR;
}

static void AddRxByte(int ComPort, unsigned char Byte)
{
  if (ComData[ComPort].RxBufferLen == FIFO_SIZE)
  {
    return;
  }

  ComData[ComPort].RxBuffer[ComData[ComPort].RxTail] = Byte;
  ComData[ComPort].RxTail = (ComData[ComPort].RxTail + 1) % FIFO_SIZE;
  ComData[ComPort].RxBufferLen++;

  ComData[ComPort].Reg[5] |= 0x01; // Set Data Ready

  ReevaluateInterrupts(ComPort);
}

static unsigned char GetRxByte(int ComPort)
{
  unsigned char Byte;

  if (ComData[ComPort].RxBufferLen == 0)
  {
    return 0xff;
  }

  Byte = ComData[ComPort].RxBuffer[ComData[ComPort].RxHead];
  ComData[ComPort].RxHead = (ComData[ComPort].RxHead + 1) % FIFO_SIZE;
  ComData[ComPort].RxBufferLen--;

  if (ComData[ComPort].RxBufferLen == 0)
  {
    ComData[ComPort].Reg[5] &= 0xFE; // Clear Data Ready
  }

  ReevaluateInterrupts(ComPort);

  return Byte;
}

static void AddTxByte(int ComPort, unsigned char Byte)
{
  if (ComData[ComPort].TxBufferLen == FIFO_SIZE)
  {
    return;
  }

  ComData[ComPort].TxBuffer[ComData[ComPort].TxTail] = Byte;
  ComData[ComPort].TxTail = (ComData[ComPort].TxTail + 1) % FIFO_SIZE;
  ComData[ComPort].TxBufferLen++;
  ComData[ComPort].TxBufferLenI = ComData[ComPort].TxBufferLen;

  ComData[ComPort].Reg[5] &= 0xBF;  // Clear Transmitter Empty
  if (ComData[ComPort].TxBufferLen < FIFO_SIZE)
  {
    ComData[ComPort].Reg[5] |= 0x20; // Set Transmitter Holding Register Empty
  }
  else
  {
    ComData[ComPort].Reg[5] &= 0xDF; // Clear Transmitter Holding Register Empty
  }

  ReevaluateInterrupts(ComPort);
}

static unsigned char GetTxByte(int ComPort)
{
  unsigned char Byte;

  if (ComData[ComPort].TxBufferLen == 0)
  {
    return 0xff;
  }

  Byte = ComData[ComPort].TxBuffer[ComData[ComPort].TxHead];
  ComData[ComPort].TxHead = (ComData[ComPort].TxHead + 1) % FIFO_SIZE;
  ComData[ComPort].TxBufferLen--;

  if (ComData[ComPort].TxBufferLen == 0)
  {
    ComData[ComPort].Reg[5] |= 0x40; // Set Transmitter Empty
  }
  else
  {
    ComData[ComPort].Reg[5] &= 0xBF; // Clear Transmitter Empty
  }

  if (ComData[ComPort].TxBufferLen < FIFO_SIZE)
  {
    ComData[ComPort].Reg[5] |= 0x20; // Set Transmitter Holding Register Empty
  }
  else
  {
    ComData[ComPort].Reg[5] &= 0xDF; // Clear Transmitter Holding Register Empty
  }

  ReevaluateInterrupts(ComPort);

  return Byte;
}

static void UpdateMSR(int ComPort, unsigned char NewMSRStateBits)
{
  unsigned char OldMSR = ComData[ComPort].Reg[6];
  unsigned char NewMSR = (OldMSR & 0x0f) | (NewMSRStateBits & 0xf0);

  NewMSR |= ((NewMSR ^ OldMSR) >> 4) & 0x0b;
  if (((OldMSR & 0x40) == 0) && ((NewMSR & 0x40) != 0))
  {
    NewMSR |= 0x04;
  }
  else
  {
    NewMSR &= 0xfb;
  }

  ComData[ComPort].Reg[6] = NewMSR;
}

//
// Hardware serial port functions
//

void HandleHWComPort(int ComPort)
{
  // Read from com port
  unsigned char Buffer[FIFO_SIZE];
  int nBytesToRead;
  int nBytesRead;
  int nBytesWritten;

  nBytesToRead = FIFO_SIZE - ComData[ComPort].RxBufferLen;
  while (nBytesToRead > 0)
  {
    nBytesRead = SERIAL_HW_Read(ComPort, Buffer, 1);
    if (nBytesRead > 0)
    {
      AddRxByte(ComPort, Buffer[0]);
      nBytesToRead--;
    }
    else
    {
      break;
    }
  }

  // Write to com port
  bool TxDone = (ComData[ComPort].TxBufferLen == 0);
  while (!TxDone)
  {
    Buffer[0] = ComData[ComPort].TxBuffer[ComData[ComPort].TxHead];
    nBytesWritten = SERIAL_HW_Write(ComPort, Buffer, 1);
    if (nBytesWritten > 0)
    {
      GetTxByte(ComPort);
      TxDone = (ComData[ComPort].TxBufferLen == 0);
    }
    else
    {
      TxDone = true;
    }
  }

  unsigned char NewMSRStateBits = 0;

  // Get modem status
  SERIAL_HW_GetModemStatusBits(ComPort, NewMSRStateBits);

  // Update the MSR, including delta bits
  UpdateMSR(ComPort, NewMSRStateBits);

  ReevaluateInterrupts(ComPort);
}


//
// TCP/IP serial port emulation functions
//

static bool CreateServerListenSocket(int ComPort)
{
  // Create the listen socket and bind it to the listen port.
  int iResult;
  struct addrinfo *result = NULL;
  struct addrinfo hints;
  unsigned long param;

  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  // Resolve the local address and port to be used by the server.
  iResult = getaddrinfo(NULL, ComData[ComPort].TCPPort, &hints, &result);
  if (iResult != 0)
  {
    printf("getaddrinfo failed: %d\n", iResult);
    return false;
  }

  // Create a socket for the server to listen for client connections
  ComData[ComPort].listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (ComData[ComPort].listenSocket == INVALID_SOCKET)
  {
    printf("Error at socket(): %d\n", WSAGetLastError());
    freeaddrinfo(result);
    return false;
  }

  // set to nonblocking
  param = 1;
  ioctlsocket(ComData[ComPort].listenSocket, FIONBIO, &param);

  // Setup the TCP listening socket
  iResult = bind(ComData[ComPort].listenSocket, result->ai_addr, (int) result->ai_addrlen);
  if (iResult == SOCKET_ERROR)
  {
    printf("bind failed: %d\n", WSAGetLastError());
    freeaddrinfo(result);
    closesocket(ComData[ComPort].listenSocket);
    ComData[ComPort].listenSocket = INVALID_SOCKET;
    return false;
  }

  if (listen(ComData[ComPort].listenSocket, SOMAXCONN) == SOCKET_ERROR)
  {
    printf("listen failed: %d\n", WSAGetLastError());
    freeaddrinfo(result);
    closesocket(ComData[ComPort].listenSocket);
    ComData[ComPort].listenSocket = INVALID_SOCKET;
    return false;
  }

  freeaddrinfo(result);

  ComData[ComPort].ConnectRetryTime = GET_TICKS();

  return true;
}

static bool CreateClientSocket(int ComPort)
{
  unsigned long param;

  // Create the client socket
  ComData[ComPort].commSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (ComData[ComPort].commSocket == INVALID_SOCKET)
  {
    printf("Failed to create TCP socket.\n");
    return false;
  }

  // set to nonblocking
  param = 1;
  ioctlsocket(ComData[ComPort].commSocket, FIONBIO, &param);

  ComData[ComPort].ConnectRetryTime = GET_TICKS();

  return true;
}

static void HandleTCPTxRx(int ComPort)
{
  unsigned char Buffer[FIFO_SIZE];
  int nbytes;
  int RxFIFOSpace;
  int nBytesToRead;
  int idx;
  int errCode;

  // Receive data.
  RxFIFOSpace = FIFO_SIZE - ComData[ComPort].RxBufferLen;
  if (RxFIFOSpace == 0)
  {
    // FIFO is full, so discard incoming bytes
    nBytesToRead = FIFO_SIZE;
  }
  else
  {
    // Read up to enough to fill the FIFO
    nBytesToRead = RxFIFOSpace;
  }

  nbytes = recv(ComData[ComPort].commSocket, (char *) Buffer, nBytesToRead, 0);

  if (nbytes == 0)
  {
    // No bytes read, so check for a socket error
    errCode = WSAGetLastError();
    if (errCode != WSAEWOULDBLOCK)
    {
      closesocket(ComData[ComPort].commSocket);
      ComData[ComPort].commSocket = INVALID_SOCKET;
      ComData[ComPort].IsSocketConnected = false;
      printf("Disconnect on rx\n");
    }
  }
  else if (RxFIFOSpace > 0)
  {
    idx = 0;
    while (nbytes > 0)
    {
      AddRxByte(ComPort, Buffer[idx]);
      idx++;
      nbytes--;
    }
  }

  // Send data

  bool TxDone = (ComData[ComPort].TxBufferLen == 0);
  while (!TxDone)
  {
    Buffer[0] = ComData[ComPort].TxBuffer[ComData[ComPort].TxHead];
    nbytes = send(ComData[ComPort].commSocket, (const char *) Buffer, 1, 0);

    if (nbytes == 1)
    {
      GetTxByte(ComPort);
      TxDone = (ComData[ComPort].TxBufferLen == 0);
    }
    else
    {
      // byte did not send, so check for a socket error.
      errCode = WSAGetLastError();
      if (errCode != WSAEWOULDBLOCK)
      {
        closesocket(ComData[ComPort].commSocket);
        ComData[ComPort].commSocket = INVALID_SOCKET;
        ComData[ComPort].IsSocketConnected = false;
        printf("Disconnect on tx\n");
      }

      TxDone = true;
    }
  }
}

static void HandleTCPServer(int ComPort)
{
  if (ComData[ComPort].IsSocketConnected)
  {
    HandleTCPTxRx(ComPort);
  }
  else
  {
    DWORD CurrentTime;
    SOCKET NewSocket;
    sockaddr_in Addr;
    int      AddrLen = sizeof(Addr);

    CurrentTime = GET_TICKS();
    if (CurrentTime < ComData[ComPort].ConnectRetryTime)
    {
      return;
    }

    NewSocket = accept(ComData[ComPort].listenSocket, (sockaddr *) &Addr, &AddrLen);

    if (NewSocket == INVALID_SOCKET)
    {
      int errCode = WSAGetLastError();
      if (errCode != WSAEWOULDBLOCK)
      {
        printf("accept failed, error = %d\n", WSAGetLastError());
      }
      ComData[ComPort].ConnectRetryTime = GET_TICKS() + 1000;
    }
    else
    {
      printf("Accept socket from address %08x, port %d\n",
             (unsigned int) ntohl(Addr.sin_addr.S_un.S_addr),
             ntohs(Addr.sin_port));

      ComData[ComPort].commSocket = NewSocket;
      ComData[ComPort].IsSocketConnected = true;

      // Now we are connected assert DCD, CTS and RTS
      UpdateMSR(ComPort, 0xb0); // DCD, CTS and DTR on

      ReevaluateInterrupts(ComPort);
    }
  }
}

static void HandleTCPClient(int ComPort)
{
  if (ComData[ComPort].IsSocketConnected)
  {
    HandleTCPTxRx(ComPort);
  }
  else
  {
    // Get the host we are trying to connect to.
    DWORD CurrentTime;
    struct hostent *host;
    struct in_addr addr;

    CurrentTime = GET_TICKS();
    if (CurrentTime < ComData[ComPort].ConnectRetryTime)
    {
      return;
    }

    addr.s_addr = inet_addr(ComData[ComPort].TCPAddress);
    if (addr.s_addr == INADDR_NONE)
    {
      printf("Getting host by name\n");
      host = gethostbyname(ComData[ComPort].TCPAddress);

      if (host == NULL)
      {
        printf("Error: Could not find host '%s', error %d\n",
          ComData[ComPort].TCPAddress,
          WSAGetLastError());
        return;
      }

      addr.s_addr = ((struct in_addr *)(host->h_addr))->s_addr;
    }

    //
    // Connect to the server
    //
    struct sockaddr_in sin;
    memset( &sin, 0, sizeof sin );
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = addr.s_addr;
    sin.sin_port = htons(atoi(ComData[ComPort].TCPPort));

    if (connect(ComData[ComPort].commSocket, (const struct sockaddr *) &sin, sizeof(sin)) == SOCKET_ERROR)
    {
      int ErrCode = WSAGetLastError();
      if (ErrCode == WSAEISCONN)
      {
        printf("Connected to port: %s\n", ComData[ComPort].TCPPort);
        ComData[ComPort].IsSocketConnected = true;

        // Now we are connected assert DCD, CTS and RTS
        UpdateMSR(ComPort, 0xb0); // DCD, CTS and DTR on

        ReevaluateInterrupts(ComPort);
      }
      else
      {
        /* could not connect to server */
        if (ErrCode != WSAEWOULDBLOCK)
        {
          printf("Error: Could not connect to port: %s, error = %d\n",
            ComData[ComPort].TCPPort,
            ErrCode);
        }

        ComData[ComPort].ConnectRetryTime = GET_TICKS() + 1000;
        return;
      }
    }
    else
    {
      printf("Connected to port: %s\n", ComData[ComPort].TCPPort);
      ComData[ComPort].IsSocketConnected = true;

      // Now we are connected assert CTS and RTS
      unsigned char OldMSR = ComData[ComPort].Reg[6];
      unsigned char NewMSR = OldMSR & 0x0f;

      NewMSR |= 0xb0; // DCD, CTS and DTR on

      NewMSR |= ((NewMSR ^ OldMSR) >> 4) & 0x0b;
      if ((OldMSR & 0x40) & (~NewMSR & 0x40))
      {
        NewMSR |= 0x04;
      }

      ComData[ComPort].Reg[6] = NewMSR;

      ReevaluateInterrupts(ComPort);
    }

  }
}

// =============================================================================
// Exported Functions
//

void SERIAL_Initialise(void)
{
  for (int i = 0 ; i < 4 ; i++)
  {
    ComData[i].Mapping = SERIAL_UNUSED;
    strcpy(ComData[i].TCPAddress, "127.0.0.1");
    sprintf(ComData[i].TCPPort, "%d", 5001+i);

    ComData[i].listenSocket = INVALID_SOCKET;
    ComData[i].commSocket   = INVALID_SOCKET;
    ComData[i].IsSocketConnected = false;

    for (int j = 0 ; j < 8 ; j++)
    {
      ComData[i].Reg[j] = 0;
    }

    ComData[i].DivisorLatch = false;
    ComData[i].DivisorL = 12;
    ComData[i].DivisorH = 0;
    ComData[i].Divisor = 12;
    ComData[i].BaudRate = 9600;
    ComData[i].DataBits = 8;
    ComData[i].StopBits = SERIAL_STOPBITS_1;
    ComData[i].Parity = SERIAL_PARITY_NONE;

    ComData[i].RxTriggerLevel = 1;

    ComData[i].Reg[2] = 0x01;
    ComData[i].Reg[3] = 0x03;
    ComData[i].Reg[5] = 0x60;

    ComData[i].RxBufferLen = 0;
    ComData[i].RxHead = 0;
    ComData[i].RxTail = 0;

    ComData[i].RTS_High = false;
    ComData[i].DTR_High = false;

    ComData[i].TxBufferLen = 0;
    ComData[i].TxBufferLenI = 0;
    ComData[i].TxHead = 0;
    ComData[i].TxTail = 0;

    ComData[i].IIR = 0x01;
  }

  ComData[0].IRQ = 4;
  ComData[1].IRQ = 3;
  ComData[2].IRQ = 4;
  ComData[3].IRQ = 3;

#if defined(_WIN32)
  // Initialise winsock
  WORD wVersionRequested;
  WSADATA wsaData;
  int err;

  wVersionRequested = MAKEWORD(2, 0);

  err = WSAStartup(wVersionRequested, &wsaData);
  if (err != 0)
  {
    // Tell the user that we couldn't find a usable
    // WinSock DLL.
    printf("WSAStartup() failed.\n");
    return;
  }

  // Confirm that the WinSock DLL supports 2.0.
  // Note that if the DLL supports versions greater
  // than 2.0 in addition to 2.0, it will still return
  // 2.0 in wVersion since that is the version we
  // requested.

  if ((LOBYTE(wsaData.wVersion) != 2) ||
      (HIBYTE(wsaData.wVersion) != 0))
  {
    // Tell the user that we couldn't find a usable WinSock DLL.
    printf("Winsock: Requested version not available.\n");
    WSACleanup();
    return;
  }

  WinsockInitialised = true;
#endif
}

void SERIAL_Reset(void)
{
  MousePowerOn = false;
  MouseSendOK = false;

  ComData[0].IRQ = 4;
  ComData[1].IRQ = 3;
  ComData[2].IRQ = 4;
  ComData[3].IRQ = 3;

  for (int i = 0 ; i < 4 ; i++)
  {
    for (int j = 0 ; j < 8 ; j++)
      ComData[i].Reg[j] = 0;

    ComData[i].DivisorLatch = false;
    ComData[i].DivisorL = 12;
    ComData[i].DivisorH = 0;
    ComData[i].Divisor = 12;
    ComData[i].BaudRate = 9600;
    ComData[i].DataBits = 8;
    ComData[i].StopBits = SERIAL_STOPBITS_1;
    ComData[i].Parity = SERIAL_PARITY_NONE;

    ComData[i].RxTriggerLevel = 1;

    ComData[i].Reg[3] = 0x01;
    ComData[i].Reg[3] = 0x03;
    ComData[i].Reg[5] = 0x60;

    ComData[i].RxBufferLen = 0;
    ComData[i].RxHead = 0;
    ComData[i].RxTail = 0;

    ComData[i].RTS_High = false;
    ComData[i].DTR_High = false;

    ComData[i].TxBufferLen = 0;
    ComData[i].TxBufferLenI = 0;
    ComData[i].TxHead = 0;
    ComData[i].TxTail = 0;

    ComData[i].IIR = 0x01;

    ConfigureComPort(i);
  }

}

void SERIAL_Cleanup(void)
{
  for (int i = 0 ; i < 4 ; i++)
  {
    SERIAL_Configure(i, SERIAL_UNUSED, NULL);
  }

#if defined(_WIN32)
  if (WinsockInitialised)
  {
    WSACleanup();
  }
#endif
}

void SERIAL_ReadConfig(FILE *fp)
{
  char Line[256];
  char Token[64];
  int ComPort;
  int len;

  // Close all serial ports

  for (ComPort = 0 ; ComPort < 4 ; ComPort++)
  {
    SERIAL_Configure(ComPort, SERIAL_UNUSED, NULL);
  }

  SerialMousePort = -1;

  // Read serial settings

  for (ComPort = 0 ; ComPort < 4 ; ComPort++)
  {
    sprintf(Token, "[COM%1d]", ComPort+1);
    fgets(Line, 256, fp);
    if (strncmp(Line, Token, 6) != 0) return;

    fgets(Line, 256, fp);
    len = strlen(Line)-1;
    while ((len > 0) && (!isprint(Line[len]))) Line[len--] = 0;

    if (strcmp(Line, "UNUSED") == 0)
    {
      SERIAL_Configure(ComPort, SERIAL_UNUSED, NULL);
    }
    else if (strcmp(Line, "MOUSE") == 0)
    {
      SERIAL_Configure(ComPort, SERIAL_MOUSE, NULL);
    }
    else if (strncmp(Line, "SERIAL_SERVER:", 14) == 0)
    {
      strncpy(ComData[ComPort].TCPPort, Line+14, 64);
      ComData[ComPort].TCPPort[63] = 0;

      SERIAL_Configure(ComPort, SERIAL_TCP_SERVER, NULL);
    }
    else if (strncmp(Line, "SERIAL_CLIENT:", 14) == 0)
    {
      char *tmp = strchr(Line+14, ':');
      *tmp = 0;

      strncpy(ComData[ComPort].TCPAddress, Line+14, 128);
      ComData[ComPort].TCPAddress[127] = 0;

      strncpy(ComData[ComPort].TCPPort, tmp+1, 64);
      ComData[ComPort].TCPPort[63] = 0;

      SERIAL_Configure(ComPort, SERIAL_TCP_CLIENT, NULL);
    }
    else if (strncmp(Line, "COM:", 3) == 0)
    {
      SERIAL_Configure(ComPort, SERIAL_COM, (const char *) Line+4);
    }
  }
}

void SERIAL_WriteConfig(FILE *fp)
{
  int ComPort;

  for (ComPort = 0 ; ComPort < 4 ; ComPort++)
  {
    fprintf(fp, "[COM%1d]\n", ComPort+1);

    if (ComData[ComPort].Mapping == SERIAL_UNUSED)
    {
      fprintf(fp, "UNUSED\n");
    }
    else if (ComData[ComPort].Mapping == SERIAL_MOUSE)
    {
      fprintf(fp, "MOUSE\n");
    }
    else if (ComData[ComPort].Mapping == SERIAL_TCP_SERVER)
    {
      fprintf(fp, "TCP_SERVER:%s", ComData[ComPort].TCPPort);
    }
    else if (ComData[ComPort].Mapping == SERIAL_TCP_CLIENT)
    {
      fprintf(fp, "TCP_CLIENT:%s:%s", ComData[ComPort].TCPAddress, ComData[ComPort].TCPPort);
    }
    else
    {
      fprintf(fp, "COM:%s\n", ComData[ComPort].ComName);
    }

  }
}

void SERIAL_GetConfig(
  int ComPort,
  SerialMapping_t &Mapping,
  char * &ComName,
  char * &TCPAddress,
  char * &TCPPort)
{
  Mapping = ComData[ComPort].Mapping;
  ComName = ComData[ComPort].ComName;
  TCPAddress = ComData[ComPort].TCPAddress;
  TCPPort = ComData[ComPort].TCPPort;
}

bool SERIAL_Configure(int ComPort, SerialMapping_t Mapping, const char *ComName)
{
  if ((ComPort < 0) || (ComPort > 3)) return false;

  if (SerialMousePort == ComPort) SerialMousePort = -1;
  if (ComData[ComPort].Mapping == SERIAL_COM)
  {
    // Close HW serial port
    SERIAL_HW_Close(ComPort);
  }

  // Close any open sockets
  if (ComData[ComPort].listenSocket != INVALID_SOCKET)
  {
    closesocket(ComData[ComPort].listenSocket);
    ComData[ComPort].listenSocket = INVALID_SOCKET;
  }
  if (ComData[ComPort].commSocket != INVALID_SOCKET)
  {
    closesocket(ComData[ComPort].commSocket);
    ComData[ComPort].commSocket = INVALID_SOCKET;
  }
  ComData[ComPort].IsSocketConnected = false;

  // Set new serial port mapping

  ComData[ComPort].Mapping = Mapping;

  if (Mapping == SERIAL_UNUSED)
  {
    // Do nothing
  }
  else if (Mapping == SERIAL_MOUSE)
  {
    if (SerialMousePort != -1)
    {
      return false;
    }
    else
    {
      printf("Mouse on COM%d\n", ComPort+1);
      SerialMousePort = ComPort;
    }
  }
  else if (Mapping == SERIAL_TCP_SERVER)
  {
    // Create the listen socket
    CreateServerListenSocket(ComPort);
  }
  else if (Mapping == SERIAL_TCP_CLIENT)
  {
    // Create the client socket.
    CreateClientSocket(ComPort);
  }
  else if (Mapping == SERIAL_COM)
  {
    // Open HW serial port
    strcpy(ComData[ComPort].ComName, ComName);
    SERIAL_HW_Open(ComPort, ComData[ComPort].ComName);
  }

  ConfigureComPort(ComPort);

  return true;
}

void SERIAL_MouseMove(int dx, int dy, bool LButtonDown, bool RButtonDown)
{
  if (SerialMousePort == -1) return;

  Mouse_dx += dx;
  Mouse_dy += dy;
  Mouse_LBPressed = LButtonDown;
  Mouse_RBPressed = RButtonDown;
  MouseEventPending = true;
}

void SERIAL_HandleSerial()
{
  DWORD CurrentTime = GET_TICKS();

  if ((SerialMousePort != -1) &&
      ((ComData[SerialMousePort].Reg[4] & 0x10) == 0))
  {
    // Serial mouse emulation is enabled and the serial mouse port is
    // not in loop back mode so process mouse events
    if (CurrentTime >= MouseEventTime)
    {
      if (MouseSendOK && MousePowerOn && MouseEventPending)
      {
        // Mouse port is correctly configured to receive mouse events
        // So send a mouse event.
        unsigned char EventByte1 = 0x40;
        unsigned char EventByte2 = 0x00;
        unsigned char EventByte3 = 0x00;

        if (Mouse_dx > 127) Mouse_dx = 127;
        if (Mouse_dx < -128) Mouse_dx = -128;
        if (Mouse_dy > 127) Mouse_dy = 127;
        if (Mouse_dy < -128) Mouse_dy = -128;

        if (Mouse_LBPressed) EventByte1 |= 0x20;
        if (Mouse_RBPressed) EventByte1 |= 0x10;
        EventByte1 |= (Mouse_dx >> 6) & 0x03;
        EventByte1 |= (Mouse_dy >> 4) & 0x0c;
        EventByte2 = Mouse_dx & 0x3F;
        EventByte3 = Mouse_dy & 0x3F;

        AddRxByte(SerialMousePort, EventByte1);
        AddRxByte(SerialMousePort, EventByte2);
        AddRxByte(SerialMousePort, EventByte3);

        MouseEventPending = false;
      }

      Mouse_dx = 0;
      Mouse_dy = 0;

      MouseEventTime += 25;
      if (MouseEventTime < CurrentTime)
      {
        MouseEventTime = CurrentTime + 25;
      }
    }
  }

  for (int ComPort = 0 ; ComPort < 4 ; ComPort++)
  {
    if ((ComData[ComPort].Reg[4] & 0x10) == 0)
    {
      // Not in loop back mode, so process serial port
      if (ComData[ComPort].Mapping == SERIAL_COM)
      {
        HandleHWComPort(ComPort);
      }
      else if (ComData[ComPort].Mapping == SERIAL_TCP_SERVER)
      {
        HandleTCPServer(ComPort);
      }
      else if (ComData[ComPort].Mapping == SERIAL_TCP_CLIENT)
      {
        HandleTCPClient(ComPort);
      }
    }
  }

}

bool SERIAL_WritePort(int Address, unsigned char Val)
{
  bool handled = false;
  int ComPort = -1;
  int RegAddr = -1;

  ComPort = IOAddressToComPort(Address, RegAddr);
  if (ComPort == -1) return false;

  // printf("W COM%d A=%d D=%02X\n", ComPort+1, RegAddr, Val);

  handled = true;

  switch (RegAddr)
  {
    case 0:
      // Transmit Holding Register / Divisor Latch LSB
      if (ComData[ComPort].DivisorLatch)
      {
        ComData[ComPort].DivisorL = Val;
        SetBaud(ComPort);
      }
      else
      {
        if ((ComData[ComPort].Reg[4] & 0x10) != 0)
        {
          // loop back enabled
          AddRxByte(ComPort, Val);
        }
        else
        {
          // loop back disabled
          AddTxByte(ComPort, Val);
        }
      }
      break;

    case 1:
      // Divisor Latch MSB / Interrupt Enable Register
      if (ComData[ComPort].DivisorLatch)
      {
        ComData[ComPort].DivisorH = Val;
        SetBaud(ComPort);
      }
      else
      {
        // Bits 4 ..7 of IER are always 0
        ComData[ComPort].Reg[RegAddr] = (Val & 0x0f);

        if (Val & 0x02)
        {
          // Enable THRE will trigger immediately if THR is empty
          ComData[ComPort].TxBufferLenI = 1;
        }

        ReevaluateInterrupts(ComPort);
      }
      break;

    case 2:
      // FIFO Control Register
      ComData[ComPort].Reg[RegAddr] = Val;

      if (Val & 0x02)
      {
        // Clear receive FIFO
        ComData[ComPort].RxBufferLen = 0;
        ComData[ComPort].RxHead = 0;
        ComData[ComPort].RxTail = 0;
      }

      if (Val & 0x04)
      {
        // Clear transmit FIFO
        ComData[ComPort].TxBufferLen = 0;
        ComData[ComPort].TxBufferLenI = 0;
        ComData[ComPort].TxHead = 0;
        ComData[ComPort].TxTail = 0;

        ComData[ComPort].Reg[5] |= 0x40; // Set Transmitter Empty
        ComData[ComPort].Reg[5] |= 0x20; // Set Transmitter Holding Register Empty
      }
      break;

    case 3:
      // Line Control Register
      ComData[ComPort].Reg[3] = Val;
      ComData[ComPort].DivisorLatch = ((Val & 0x80) != 0);
      ComData[ComPort].DataBits = 5 + (Val & 0x03);
      if (ComData[ComPort].DataBits == 5)
      {
        ComData[ComPort].StopBits = (Val & 0x04) ? SERIAL_STOPBITS_1_5 : SERIAL_STOPBITS_1;
      }
      else
      {
        ComData[ComPort].StopBits = (Val & 0x04) ? SERIAL_STOPBITS_2 : SERIAL_STOPBITS_1;
      }
      if ((Val & 0x08) != 0)
      {
        // Parity is enabled
        if ((Val & 0x20) != 0)
        {
          // Sticky parity
          ComData[ComPort].Parity = (Val & 0x10) ? SERIAL_PARITY_MARK : SERIAL_PARITY_SPACE;
        }
        else
        {
          ComData[ComPort].Parity = (Val & 0x10) ? SERIAL_PARITY_ODD : SERIAL_PARITY_EVEN;
        }
      }
      else
      {
        ComData[ComPort].Parity = SERIAL_PARITY_NONE;
      }

      ConfigureComPort(ComPort);
      break;

    case 4:
    {
      // Modem Control Register
      ComData[ComPort].Reg[RegAddr] = (Val & 0x1F);

      if ((ComData[ComPort].Reg[4] & 0x10) != 0)
      {
        // loop back enabled
        //   Bit 4: CTS = Bit 1: RTS
        //   Bit 5: DSR = Bit 0: DTR
        //   Bit 6: RI  = Bit 2: OUT1
        //   Bit 7: DCD = Bit 3: OUT2
        //  Clears Rx FIFO
        unsigned char NewMSRStateBits = (Val & 0x0c) << 4;
        if (Val & 0x01) NewMSRStateBits |= 0x20;
        if (Val & 0x02) NewMSRStateBits |= 0x10;
        UpdateMSR(ComPort, NewMSRStateBits);
      }
      else
      {
        // loop back disabled
        ComData[ComPort].RTS_High = (Val & 0x02) != 0;

        if (ComPort == SerialMousePort)
        {
          if (!ComData[ComPort].DTR_High && ((Val & 0x01) != 0))
          {
            if (MouseSendOK)
            {
              ComData[ComPort].RxBufferLen = 0;
              ComData[ComPort].RxHead = 0;
              ComData[ComPort].RxTail = 0;

              MouseEventTime = GET_TICKS() + 25;

              AddRxByte(ComPort, 'M');
            }
          }

          // Mouse power off if RTS is low, power on if RTS is high

          if (!MousePowerOn && ComData[ComPort].RTS_High)
          {
            // Mouse just powered up, so clear the Rx buffer and output
            // the startup id
            ComData[ComPort].RxBufferLen = 0;
            ComData[ComPort].RxHead = 0;
            ComData[ComPort].RxTail = 0;

            MouseEventTime = GET_TICKS() + 25;

            AddRxByte(ComPort, 'M');
          }
          MousePowerOn = ComData[ComPort].RTS_High;
        }

        ComData[ComPort].DTR_High = (Val & 0x01) != 0;

        if (ComData[ComPort].Mapping == SERIAL_COM)
        {
          // Set HW DTR & RTS
          SERIAL_HW_SetDTR(ComPort, ComData[ComPort].DTR_High);
          SERIAL_HW_SetRTS(ComPort, ComData[ComPort].RTS_High);
        }
      }

      break;
    }

    case 5:
      // Line Status Register - read only
      break;

    case 6:
      // Modem Status Register - read only
      break;

    case 7:
      // Scratch Register
      ComData[ComPort].Reg[RegAddr] = Val;
      break;
  }

  return handled;
}

bool SERIAL_ReadPort(int Address, unsigned char &Val)
{
  bool handled = false;
  int ComPort = -1;
  int RegAddr = -1;

  ComPort = IOAddressToComPort(Address, RegAddr);
  if (ComPort == -1) return false;

  handled = true;

  switch (RegAddr)
  {
    case 0:
      // Receive Buffer Register / Divisor latch LSB
      if (ComData[ComPort].DivisorLatch)
      {
        Val = ComData[ComPort].DivisorL;
      }
      else
      {
        Val = GetRxByte(ComPort);
      }
      break;

    case 1:
      // Divisor Latch MSB / Interrupt Enable Register
      if (ComData[ComPort].DivisorLatch)
      {
        Val = ComData[ComPort].DivisorH;
      }
      else
      {
        Val = ComData[ComPort].Reg[RegAddr];
      }
      break;

    case 2:
      // Interrupt Identification Register
      ReevaluateInterrupts(ComPort);

      Val = ComData[ComPort].IIR;

      if ((Val & 0x0e) == 0x02)
      {
        // Interrupt reason is transmit holding register empty.
        // Reading the IIR clears this condition.
        ComData[ComPort].TxBufferLenI = ComData[ComPort].TxBufferLen;
        ReevaluateInterrupts(ComPort);
      }
      break;

    case 3:
      // Line Control Register
      Val = ComData[ComPort].Reg[RegAddr];
      break;
    case 4:
      // Modem Control Register
      Val = ComData[ComPort].Reg[RegAddr];
      break;

    case 5:
      // Line Status Register
      Val = ComData[ComPort].Reg[RegAddr];
      break;

    case 6:
      // Modem Status Register.
      // Reading this clears the modem status delta bits
      Val = ComData[ComPort].Reg[RegAddr];

      ComData[ComPort].Reg[RegAddr] &= 0xf0;
      ReevaluateInterrupts(ComPort);
      break;

    case 7:
      // Scratch Register
      Val = ComData[ComPort].Reg[RegAddr];
      break;
  }

  return handled;
}

bool SERIAL_IntPending(int &IntNo)
{
  if ((ComData[1].IIR & 0x01) == 0)
  {
    IntNo = 11;
    return true;
  }

  if ((ComData[3].IIR & 0x01) == 0)
  {
    IntNo = 11;
    return true;
  }

  if ((ComData[0].IIR & 0x01) == 0)
  {
    IntNo = 12;
    return true;
  }

  if ((ComData[2].IIR & 0x01) == 0)
  {
    IntNo = 12;
    return true;
  }

  return false;
}



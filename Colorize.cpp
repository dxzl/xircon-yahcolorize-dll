// Copyright 2015 Scott Swift - This program is distributed under the
// terms of the GNU General Public License.

#define DTS_VERSION "2.55"

// File:     Colorize.cpp
// Author:   Scott Swift, dxzl@live.com
// Date:     July 7, 2000
// Date:     Nov 22, 2005 (Update for PIRCH)
// Date:     Aug 9, 2006 (Update to allow direct send of a line for XiRCON)
// Date:     Aug 3, 2008 (Update,  for every "\" was getting "\\")
// Date:     Nov 1, 2013 (Update for Vortec 2.50)
// Date:     Jan 17, 2014 (Add CTRL_K to terminate space-padded lines 2.51)
// Date:     Jan 22, 2014 (Changed the TCL Script packaged with this DLL 2.52)
// Date:     July 27, 2015 (Added the M_PLAY "WM_PlayCoLoRiZe" message 2.54)
// Date:     Aug 1, 2015 (TCL_DoOneEvent() was locking up Windows Explorer
//             plus added new search paths for Xirc.DLL 2.55)
// bad logic syntax:
//    if (!pDTS_Color->bUseDDE && GlobalString[ii] == '"'
//                                || GlobalString[ii] == '\x5c')
//    if (!pDTS_Color->bUseDDE && (GlobalString[ii] == '"'
//                                || GlobalString[ii] == '\x5c'))
//
// Credits:  Thanks to Dave Gravereaux for his sample code and advice!

// How it works:
// This DLL is loaded by a companion script running on XiRC called
// Colorize.tcl.  A companion program called Colorize.exe will also
// load this DLL when its "Start Play" menu command is triggered.
// "Start Play" calls ColorStart() in this DLL.  ColorStart()
// places the filename, channel and time-delay into a shared
// memory structure (since each application that loads a DLL
// has its own memory which can't be shared, I have to create
// a shared memory space) and then sets a global flag in that
// structure which will tell CmdPoll() that a playback has begun.
//
// Meanwhile, the function CmdPoll() is being called once per
// second from XiRC's "ON TIMER" hook.  CmdPoll gives us a pointer
// to XiRC's Tcl interpreter (essential so that we can evaluate
// XiRC commands).  CmdPoll() checks its shared memory flag and
// finds that it has a file to play.  It then opens the file and
// starts a timer thread, then enters a hard loop.  The loop
// keeps Tcl running smoothly via TclDoOneEvent(), but allows us to
// keep the Tcl interpreter context while simultaneously monitoring
// for a "Stop Play" command from the colorizer.  The loop also
// monitors the timer-thread for new text-lines to be sent to
// Xirc.  This scheme allows text to be sent with precise timing
// from 1ms - 100000+ms!  XiRC normally could only send text either
// all at once or at 1-second intervals.
//
// The trick is in holding the interpreter context without locking
// up XiRC and then using an internal timebase to gate the text.
//
// Use "status" as the channel name in Colorizer.exe to allow testing
// by causing the color-processed data to be sent to XiRC's status
// window.
//
// I've included a new XiRC command called DTS_play which has the following
// format:
//        DTS_play <channel> <filename> <time delay in milliseconds>
//
// (I know this program uses a lot of global vars, but it seemed like
// the best compromize given the multiple threads.  Colorizer is written
// using all automatic ANSI strings in Borland's C++ Builder, but I
// wrote this DLL using LPTSTR and all Windows API calls -- I may
// change this... this serves as a more generic example for programmers
// not familiar with Borland's Delphi/C++ Builder -- I don't use
// Visual C++)
//
// Call DTS_poll from the "ON TIMER" hook in a XiRC script.
// I also added DTS_version which should append the result of the
// DLL version, 1.0, etc. (not tried)
//
// Sept 11, 2013 - using new stolower() to compare string to "status".
// Now, if the line length is 0, I add a \r\n and send it unless we
// are at EOF (in pringstring()).
//
// July 24, 2015 - no changes but have a digital signing folder now. This
// version works ok with YahCoLoRiZe Unicode because I pass it an ANSI
// equivalent file-path after calling GetShortPathNameW() on a Unicode
// file-path.
//
// Enjoy!
// Mr. Swift

// FYI - Here is the XTCL.DLL library functions we can use...
//
//LIBRARY     XTCL.DLL
//
//EXPORTS
//    @__lockDebuggerData$qv         @14  ; __lockDebuggerData()
//    @__unlockDebuggerData$qv       @15  ; __unlockDebuggerData()
//    DllEntryPoint                  @1   ; DllEntryPoint
//    _Tcl_AppendElement             @2   ; _Tcl_AppendElement
//    _Tcl_AppendResult              @3   ; _Tcl_AppendResult
//    _Tcl_CreateCommand             @4   ; _Tcl_CreateCommand
//    _Tcl_CreateInterp              @6   ; _Tcl_CreateInterp
//    _Tcl_DeleteCommand             @5   ; _Tcl_DeleteCommand
//    _Tcl_DeleteInterp              @7   ; _Tcl_DeleteInterp
//    _Tcl_DoOneEvent                @12  ; _Tcl_DoOneEvent
//    _Tcl_Eval                      @8   ; _Tcl_Eval
//    _Tcl_EvalFile                  @9   ; _Tcl_EvalFile
//    _Tcl_GetVar                    @10  ; _Tcl_GetVar
//    _Tcl_GlobalEval                @11  ; _Tcl_GlobalEval
//    __DebuggerHookData             @13  ; __DebuggerHookData

#include <windows.h>
#include <condefs.h>
#include <string.h>
#include <stdio.h>
#include "Colorize.h"
#pragma hdrstop

USERES("Colorize.res");
USEFILE("Colorize.h");
//---------------------------------------------------------------------------
#pragma argsused

// Global vars, handles and arrays
DWORD dwBytesRead;
DWORD dwByteCount;
UINT TimerID = NULL;
LPVOID lpHeap;
HANDLE hHeap = NULL;
HANDLE hFile = NULL;
HINSTANCE hInst;
HINSTANCE hXircTcl;
FARPROC lpDdeProc;  // procedure instance address
bool bEndOfFile = false;
bool bDataReady = false;
bool bPaused = false;
char* GlobalString = NULL;

// Globals for DDE
DWORD idInst = 0;
DWORD dwResult;
HSZ hszService;
HSZ hszTopic;
HSZ hszItem;
HCONV hcnv;

// Structure for shared memory space
DTS_Color *pDTS_Color = NULL;

// XiRC Tcl hooks
dyn_CreateCommand Tcl_CreateCommand = NULL;
dyn_AppendResult Tcl_AppendResult = NULL;
dyn_Eval Tcl_Eval = NULL;
dyn_DoOneEvent Tcl_DoOneEvent = NULL;

// pointer to shared memory
LPVOID lpvMem = NULL;

// handle to file mapping
HANDLE hMapObject = NULL;

// Function prototypes
bool IsPirch(void);
bool IsVortec(void);
bool IsPirchVortec(void);
bool DTS_WriteLineToFile(char * FileNameBuf, char *tString);
void ErrorHandler(LPTSTR Info, LPTSTR Extra = NULL);

int CmdPlay(void *cd, Tcl_Interp *interp, int argc, char *argv[]);
int CmdPoll(void *cd, Tcl_Interp *interp, int argc, char *argv[]);
int CmdVersion(void *cd, Tcl_Interp *interp, int argc, char *argv[]);
int CmdEx(void *cd, Tcl_Interp *interp, int argc, char *argv[]);
int CmdChan(void *cd, Tcl_Interp *interp, int argc, char *argv[]);
void Sendtcl(Tcl_Interp *interp, char *tempstr);
int StartLocalFilePlay(Tcl_Interp *interp);
void SendToColorize(char* pRegWndMsg, char *pData);

void Senddde(char *tempstr);
void QueueNextLineForTransmit(void);
bool PrintString(int Time);
char * stolower(char * p);
void StopPlay(void);

// Callbacks
void CALLBACK OnTimer1(HWND hwnd,UINT uMsg,UINT idEvent,DWORD dwTime);
HDDEDATA CALLBACK DdeCallback(UINT type, UINT fmt, HCONV hconv,
                              HSZ hsz1, HSZ hsz2,
                         HDDEDATA hData, DWORD dwData1, DWORD dwData2);

// DLL Export functions
extern "C" __declspec(dllexport) int Colorize_Init(Tcl_Interp *interp);
extern "C" __declspec(dllexport) bool ColorStart(LPTSTR Service,
              LPTSTR Channel, LPTSTR Filename, int PlayTime, bool bUseFile);
extern "C" __declspec(dllexport) bool ColorPause(void);
extern "C" __declspec(dllexport) bool ColorResume(void);
extern "C" __declspec(dllexport) bool ColorStop(void);
extern "C" __declspec(dllexport) LPTSTR Colorize_Version(void);

extern BOOL WINAPI DllEntryPoint(HINSTANCE hinstDLL,
                    DWORD fdwReason, LPVOID lpvReserved);

#define NULLCHAR '\0'
#define DEFTITLE "Error:"

/*********************************************************************/
/*********************************************************************/
/*                         Windows Functions                         */
/*********************************************************************/

// The DLL entry-point function sets up shared memory using
// a named file-mapping object.
BOOL WINAPI DllEntryPoint(HINSTANCE hinstDLL,  // DLL module handle
    DWORD fdwReason,                    // reason called
    LPVOID lpvReserved)                 // reserved
{

    switch (fdwReason)
    {
          // The DLL is loading due to process
          // initialization or a call to LoadLibrary.
          case DLL_PROCESS_ATTACH:

            // Called each time StartLocalFilePlay() or StopPlay() is called
            // and when Xircon loads the script
      			hInst = hinstDLL;

            // Look for Tcl DLL in three places...
            // This is hard-coded - but to get the folder from the system is
            // a pain... and may cause security level problems in Vista and up.
            // You would need to use SHGetSpecialFolder(CSIDL_PROGRAM_FILESX86)
            // #define CSIDL_PROGRAM_FILES 0x0026
            // #define CSIDL_PROGRAM_FILESX86 0x002a
     			  hXircTcl = LoadLibrary(XTCLPATH1);
            if (hXircTcl <= (HMODULE)HINSTANCE_ERROR)
         			hXircTcl = LoadLibrary(XTCLPATH2);
            if (hXircTcl <= (HMODULE)HINSTANCE_ERROR)
         			hXircTcl = LoadLibrary(XTCLPATH3);

            if (hXircTcl > (HMODULE)HINSTANCE_ERROR)
            {
      				Tcl_CreateCommand = (dyn_CreateCommand)GetProcAddress(hXircTcl,
                                                       "_Tcl_CreateCommand");

      				Tcl_AppendResult = (dyn_AppendResult)GetProcAddress(hXircTcl,
                                                     "_Tcl_AppendResult");

      				Tcl_Eval = (dyn_Eval)GetProcAddress(hXircTcl,
                                                       "_Tcl_Eval");
      				Tcl_DoOneEvent = (dyn_DoOneEvent)GetProcAddress(hXircTcl,
                                                       "_Tcl_DoOneEvent");
              if (Tcl_CreateCommand == NULL ||
                  Tcl_AppendResult == NULL ||
                  Tcl_Eval == NULL ||
                  Tcl_DoOneEvent == NULL)
              {
                ErrorHandler("Could not find all of the library"
                                                "functions in XiRCON");
                return(false);
              }
            }
            // else
            // DO NOT PUT ERROR MESSAGE HERE, ONLY XIRCON
            // NEEDS the tcl part of this DLL!!!!!!!!!!!!

            // Create a named file mapping object.
            hMapObject = CreateFileMapping(
                (HANDLE) 0xFFFFFFFF, // use paging file
                NULL,                // no security attributes
                PAGE_READWRITE,      // read/write access

                0,                   // size: high 32-bits
                sizeof(DTS_Color),   // size: low 32-bits
                "dllmemfilemap");    // name of map object

            if (hMapObject == INVALID_HANDLE_VALUE)
            {
              ErrorHandler("Error creating shared memory");
              return FALSE;
            }

            // Get a pointer to the file-mapped shared memory.
            lpvMem = MapViewOfFile(

                hMapObject,     // object to map view of
                FILE_MAP_WRITE, // read/write access
                0,              // high offset:  map from
                0,              // low offset:   beginning
                0);             // default: map entire file

            if (lpvMem == NULL)
            {
              ErrorHandler("Error getting pointer to shared memory");
              return FALSE;
            }

            pDTS_Color = (DTS_Color*)lpvMem;
            pDTS_Color->Filename[0] = NULLCHAR;
            pDTS_Color->Channel[0] = NULLCHAR;
            pDTS_Color->Service[0] = NULLCHAR;
            pDTS_Color->PlayTime = 0; // PlayTime < 0 means "one-line" mode
            pDTS_Color->FiFoIn = 0;
            pDTS_Color->FiFoOut = 0;
            pDTS_Color->bStart = pDTS_Color->bStop = false;
            pDTS_Color->bPause = pDTS_Color->bResume = false;
            pDTS_Color->bUseDDE = false;
            pDTS_Color->bUseFile = false;

            /* allocate memory for DDE/TCL Command string */
            if ((GlobalString = (char*)malloc(GLOBALSTRINGSIZ)) == NULL)
            {
              ErrorHandler("Error allocating command buffer");
              return FALSE;
            }


            break;

        // The attached process creates a new thread.
        case DLL_THREAD_ATTACH:
            break;

        // The thread of the attached process terminates.
        case DLL_THREAD_DETACH:
            break;

        // The DLL is unloading from a process due to
        // process termination or a call to FreeLibrary
        // for either program.
        case DLL_PROCESS_DETACH:

            // for mIRC, stop immediately, for XiRCON, queue a stop command
            ColorStop();

            if (pDTS_Color->bUseDDE)
            {
              DdeFreeStringHandle(idInst, hszService);
              DdeFreeStringHandle(idInst, hszTopic);
              DdeFreeStringHandle(idInst, hszItem);
              DdeUninitialize(idInst);
            }

            // Unmap shared memory from the process's address space.
            (void)UnmapViewOfFile(lpvMem);

            // Close the process's handle to the file-mapping object.
            if (hMapObject)
            {
              (void)CloseHandle(hMapObject);
              hMapObject = NULL;
            }

            // Finished with the library
            if (hXircTcl)
            {
              FreeLibrary(hXircTcl);
              hXircTcl = NULL;
            }

            // Finished with DDE string buffer
            if (GlobalString != NULL)
            {
              free(GlobalString);
              GlobalString = NULL;
            }

            break;

        default:
          break;
     }

    UNREFERENCED_PARAMETER(lpvReserved);
    return TRUE;
}
/*********************************************************************/
HDDEDATA CALLBACK DdeCallback(UINT type, UINT fmt, HCONV hconv,
      HSZ hsz1, HSZ hsz2, HDDEDATA hData, DWORD dwData1, DWORD dwData2)
{
  UNREFERENCED_PARAMETER(fmt);
  UNREFERENCED_PARAMETER(hconv);
  UNREFERENCED_PARAMETER(hsz1);
  UNREFERENCED_PARAMETER(hsz2);
  UNREFERENCED_PARAMETER(hData);
  UNREFERENCED_PARAMETER(dwData1);
  UNREFERENCED_PARAMETER(dwData2);

  switch (type)
  {
    case XTYP_ADVDATA:
      return (HDDEDATA) DDE_FACK;

    case XTYP_REGISTER:
    case XTYP_UNREGISTER:
    case XTYP_XACT_COMPLETE:
    case XTYP_DISCONNECT:
    default:
      return (HDDEDATA) NULL;
  }
}
/*********************************************************************/
// Purpose: Thread to set up next line from virtual memory which
//          will be pumped out in CmdPoll.
VOID CALLBACK OnTimer1(
                  HWND hwnd,       // handle of window for timer messages
                  UINT uMsg,       // WM_TIMER message
                  UINT idEvent,    // timer identifier
                  DWORD dwTime)     // current system time
{
  if (!bPaused)
    QueueNextLineForTransmit();

  UNREFERENCED_PARAMETER(hwnd);
  UNREFERENCED_PARAMETER(uMsg);
  UNREFERENCED_PARAMETER(idEvent);
  UNREFERENCED_PARAMETER(dwTime);
}
/*********************************************************************/
/*********************************************************************/
/*                           Tcl Functions                           */
/*********************************************************************/
/*********************************************************************/

int CmdPoll(void* cd, Tcl_Interp* interp, int argc, char* argv[])
// Purpose: Called from XiRC's "ON TIMER" hook with custom DTS_Poll
//          command. See above for discription of how this works.
// Args: cd, interp, argc, argv
// Return: Error flag
{
  int retval = TCL_OK;

  if (pDTS_Color == NULL)
    return TCL_ERROR;

  if (pDTS_Color->bUseDDE)
  	return TCL_OK;

  if (pDTS_Color->bPause)
  {
    pDTS_Color->bPause = false;
    bPaused = true;
    Sendtcl(interp, "echo \"Playback Paused!\" status");
  }
  else if (pDTS_Color->bResume)
  {
    pDTS_Color->bResume = false;
    bPaused = false;
    Sendtcl(interp, "echo \"Playback Resumed!\" status");
  }
  else if (pDTS_Color->bStart)
  {
    if (pDTS_Color->bUseFile || pDTS_Color->bUseDDE)
    {
      Sendtcl(interp, "echo \"Playback Started!\" status");
      retval = StartLocalFilePlay(interp);
    }
    else
    {
      if (pDTS_Color->PlayTime < 0 && interp != NULL) // one-line mode?
      {
        // Run loop for XiRCON
        // Stay in loop or we miss data! Also, don't
        // quit until buffer clears...
        while(pDTS_Color->FiFoIn != pDTS_Color->FiFoOut)
        {
          // Unbuffer the text-line...
          if (pDTS_Color->FiFoOut >= 0 && pDTS_Color->FiFoOut < FIFOSIZE)
          {
            strcpy(GlobalString, pDTS_Color->FiFo[pDTS_Color->FiFoOut]);

            if (++pDTS_Color->FiFoOut >= FIFOSIZE)
              pDTS_Color->FiFoOut = 0;

            PrintString(100);
            Sendtcl(interp, GlobalString);
          }
          else // error
          {
            pDTS_Color->FiFoIn = 0;
            pDTS_Color->FiFoOut = 0;
            pDTS_Color->bPause = false;
            pDTS_Color->bResume = false;
            pDTS_Color->bStart = false;
            pDTS_Color->bStop = false;
            break;
          }
        }
      }
    }
  }
  else if (pDTS_Color->bStop)
  {
    pDTS_Color->FiFoIn = 0;
    pDTS_Color->FiFoOut = 0;
    pDTS_Color->bPause = false;
    pDTS_Color->bResume = false;
    pDTS_Color->bStart = false;
    pDTS_Color->bStop = false;
  }
/*
  // This was the old method... but Tcl_DoOneEvent() was locking up
  // Windows Explorer (yes - the entire desktop froze!) in Windows 10
  // so the new methos is above this and seems to work ok - we can't do
  // fast, high-resolution posts, but who cares - IRC isn't high-resolution
  // either!

  else if (pDTS_Color->bStart)
  {
    if (pDTS_Color->bUseFile || pDTS_Color->bUseDDE)
    {
      Sendtcl(interp, "echo \"Playback Started!\" status");
      retval = StartLocalFilePlay(interp);
    }
    else
    {
      if (pDTS_Color->PlayTime < 0 && interp != NULL) // one-line mode?
      {
        // Run loop for XiRCON
        // Stay in loop or we miss data! Also, don't
        // quit until buffer clears...
        while(!pDTS_Color->bStop ||
                   (pDTS_Color->FiFoIn != pDTS_Color->FiFoOut))
        {
          if (pDTS_Color->FiFoIn != pDTS_Color->FiFoOut) // "data ready"
          {
            // Unbuffer the text-line...
            if (pDTS_Color->FiFoOut >= 0 && pDTS_Color->FiFoOut < FIFOSIZE)
            {
              strcpy(GlobalString, pDTS_Color->FiFo[pDTS_Color->FiFoOut]);

              if (++pDTS_Color->FiFoOut >= FIFOSIZE)
                pDTS_Color->FiFoOut = 0;

              PrintString(100);
              Sendtcl(interp, GlobalString);
            }
            else
            {
              pDTS_Color->FiFoOut = 0;
              break;
            }
          }
          else // Yield to other TCL processes
            (void)Tcl_DoOneEvent(TCL_DONT_WAIT);
        }

        pDTS_Color->bStop = false; // Clear
      }

      pDTS_Color->bStart = false; // Clear
      retval = TCL_OK;
    }
  }
*/

  UNREFERENCED_PARAMETER(cd);
  UNREFERENCED_PARAMETER(argc);
  UNREFERENCED_PARAMETER(argv);

	return retval;
}
/*********************************************************************/
int CmdEx(void* cd, Tcl_Interp* interp, int argc, char* argv[])
// Purpose: Allows XiRC script-writers to send text to YahCoLoRiZe
//          for processing /ex <text> command.
// Receive order for argv: text-string
{
  if (argc == 2)
    SendToColorize(M_DATA, argv[1]);
  else
    (*Tcl_Eval)(interp, "echo \"Usage: /cx <text>\"");

  UNREFERENCED_PARAMETER(cd);
  UNREFERENCED_PARAMETER(argc);
  UNREFERENCED_PARAMETER(argv);
	return TCL_OK;
}
/*********************************************************************/
int CmdChan(void *cd, Tcl_Interp *interp, int argc, char *argv[])
// Purpose: Allows XiRC script-writers to change the channel
//          for YahCoLoRiZe via the  /chan <channel> script command.
// Receive order for argv: channel
{
  if (argc == 2)
    SendToColorize(M_CHAN, argv[1]);
  else
    (*Tcl_Eval)(interp, "echo \"Usage: /chan <channel>\"");

  UNREFERENCED_PARAMETER(cd);
  UNREFERENCED_PARAMETER(argc);
  UNREFERENCED_PARAMETER(argv);
	return TCL_OK;
}
/*********************************************************************/
int CmdPlay(void* cd, Tcl_Interp* interp, int argc, char* argv[])
// Purpose: Allows XiRC script-writers to use this high-resolution
//          play command from within XiRC.
// Receive order for argv: channel, file, delay
{
  int time;

  if (argc == 2)
  {
    // Sent start stop pause resume to YahCoLoRiZe if the playback timer
    // locally is not operating
    if (!TimerID)
      SendToColorize(M_PLAY, argv[1]);
    else // pause resume or stop local file playback
    {
      if (!strcmp(strlwr(argv[1]), "stop")) // Convert to lower-case
        ColorStop();
      else if (!strcmp(strlwr(argv[1]), "pause"))
        ColorPause();
      else if (!strcmp(strlwr(argv[1]), "resume"))
        ColorResume();
    }
  }
  else if (argc == 3)
    ColorStart(NULL, argv[1], argv[2], 1500, false);
  else if (argc == 4)
  {
    time = atoi(argv[3]);

    if (time <= 100)
      time = 1500;

    ColorStart(NULL, argv[1], argv[2], time, false);
  }
  else
    (*Tcl_Eval)(interp,
         "echo \"Usage: /play <channel> <filename> <delay in ms>\"");

  UNREFERENCED_PARAMETER(cd);
  UNREFERENCED_PARAMETER(argc);
  UNREFERENCED_PARAMETER(argv);
	return TCL_OK;
}
/*********************************************************************/
int CmdVersion(void* cd, Tcl_Interp* interp, int argc, char* argv[])
// Purpose: Allows XiRC script-writers to get version of DLL.
{
  (*Tcl_AppendResult)(interp,DTS_VERSION);
  UNREFERENCED_PARAMETER(cd);
  UNREFERENCED_PARAMETER(argc);
  UNREFERENCED_PARAMETER(argv);
	return TCL_OK;
}
/*********************************************************************/
void SendToColorize(char* pRegWndMsg, char* pData)
{
  HWND GhwndReplyTo;

  // Find YahCoLoRiZe window-class
  if ((GhwndReplyTo = FindWindow(W_CLASS, 0)) == 0)
  	return;

  COPYDATASTRUCT cds;

  // Try to register WM_ChanCoLoRiZe window's message
  if ((cds.dwData = RegisterWindowMessage(pRegWndMsg)) == 0)
    return;

  cds.lpData = (void*)pData;

  // the size of the string includes the terminating NULL
  cds.cbData = strlen(pData)+1;

  SendMessage(GhwndReplyTo, WM_COPYDATA, (WPARAM)hInst, (LPARAM)&cds);
}
/*********************************************************************/
/*********************************************************************/
/*                       Colorize Functions                          */
/*********************************************************************/
/*********************************************************************/

bool ColorStart(LPTSTR Service, LPTSTR Channel, LPTSTR Filename,
                                      int PlayTime, bool bUseFile)
// Purpose: Called from Colorizer.exe to initiate playback.
// Args: Service, Channel, Filename, PlayTime (saved to shared memory)
// Shared Memory: pDTS_Color structure
//
// If PlayTime < 0, Filename holds a chat-text string!!!!!!!!!!!!!!!
{
  // For XiRCON, DDE Service is Null, but we may call
  // this routine several times before bStart ever gets cleared,
  // we buffer the data in a FIFO (the Tcl loop pulls data
  // out.
  if (pDTS_Color == NULL ||
      ((Service != NULL || (Service == NULL && bUseFile)) &&
         (pDTS_Color->bStart || Filename == NULL)))
    return(false);

  // Truncate if the string is too long
  if (strlen(Filename) >= sizeof(pDTS_Color->Filename))
    Filename[sizeof(pDTS_Color->Filename)-1] = '\0';
  // Move data to shared memory structure
  strcpy(pDTS_Color->Filename, Filename);

  if (Channel == NULL)
    // Move data to shared memory structure
    strcpy(pDTS_Color->Channel, "status");
  else
  {
    // Truncate if the string is too long
    if (strlen(Channel) >= sizeof(pDTS_Color->Channel))
      Channel[sizeof(pDTS_Color->Channel)-1] = '\0';

    // Move data to shared memory structure
    strcpy(pDTS_Color->Channel, Channel);
  }

  pDTS_Color->PlayTime = PlayTime; // if < 0 we go into "one line" mode!!!!
  pDTS_Color->bStart = true;

  pDTS_Color->bStop = false; // flush any pending commands
  pDTS_Color->bPause = false;
  pDTS_Color->bResume = false;

  pDTS_Color->bUseFile = bUseFile;

  if (Service == NULL)
  {
    // XiRCON -- use TCL to communicate
    pDTS_Color->bUseDDE = false;

    // Buffer the text-line...
    if (PlayTime < 0)
    {
      strcpy(pDTS_Color->FiFo[pDTS_Color->FiFoIn], pDTS_Color->Filename);

      if (++pDTS_Color->FiFoIn >= FIFOSIZE)
        pDTS_Color->FiFoIn = 0;
    }
  }
  else // use DDE to communicate
  {
    pDTS_Color->bUseDDE = true;

    if (strlen(Service) >= sizeof(pDTS_Color->Service))
      Service[sizeof(pDTS_Color->Service)-1] = '\0';

    strcpy(pDTS_Color->Service, Service);

    lpDdeProc = MakeProcInstance((FARPROC) DdeCallback, hInst);

    //Initialize a DDE conversation
    if (DdeInitialize(&idInst, (PFNCALLBACK) lpDdeProc,
        CBF_FAIL_EXECUTES|CBF_FAIL_POKES, 0) != DMLERR_NO_ERROR)
    {
      ErrorHandler("Unable to initialize DDEML library!");
      return(false);
    }

    //Set the application service and topic
    hszService = DdeCreateStringHandle(idInst, pDTS_Color->Service, CP_WINANSI);
    hszItem = DdeCreateStringHandle(idInst, "active", CP_WINANSI);

    if (IsPirchVortec())
      hszTopic = DdeCreateStringHandle(idInst, "IRC_COMMAND", CP_WINANSI);
    else
      hszTopic = DdeCreateStringHandle(idInst, "COMMAND", CP_WINANSI);

    // Here we only want to call StartLocalFilePlay if we are going
    // to be reading a master file (via our timer) that
    // was written by YahCoLoRiZE.  If this is a "one-line"
    // mode (text line is in the file-name string) we want to
    // do a PrintString directly... PlayTime is < 0 to flag
    // this special mode.
    if (PlayTime < 0)
    {
      strcpy(GlobalString, Filename);
      PrintString(100); // Write the text to a temp-file and format GlobalString

      // Send the /play tempfilename string to client
      Senddde(GlobalString);
      pDTS_Color->bStart = false;
    }
    else
    {
//      if (IsPirchVortec())
//        Senddde("/display \"Playback Started!\"");
//      else
//        Senddde("/echo -s \"Playback Started!\"");

      (void)StartLocalFilePlay(NULL);  // Kick off the timer
    }
  }

  return true;
}
/*********************************************************************/
bool ColorStop(void)
// Purpose: Called from Colorizer.exe to stop playback.
// Shared Memory: pDTS_Color structure
{
  if (pDTS_Color == NULL || pDTS_Color->bStop)
    return(false);

// DON'T CLEAR bStart!!!!  Tcl polling delay means
// a pending start could be (and was being) aborted...
//  pDTS_Color->bStart = false; // flush any pending commands

  pDTS_Color->bPause = false;
  pDTS_Color->bResume = false;

  if (pDTS_Color->bUseDDE)
  {
    if  (TimerID != NULL)
    {
      StopPlay();

//      if (IsPirchVortec())
//        Senddde("/display \"Playback Stopped!\"");
//      else
//        Senddde("/echo -s \"Playback Stopped!\"");
    }
  }
  else
    pDTS_Color->bStop = true;

  return(true);
}
/*********************************************************************/
bool ColorPause(void)
// Purpose: Called from Colorizer.exe to pause playback.
// Shared Memory: pDTS_Color structure
{
  if (pDTS_Color == NULL || pDTS_Color->bPause)
    return(false);

  pDTS_Color->bStop = false; // flush any pending commands
  pDTS_Color->bResume = false;
  pDTS_Color->bStart = false;

  if (pDTS_Color->bUseDDE)
  {
    if (IsPirchVortec())
      Senddde("/display \"Playback Paused!\"");
    else
      Senddde("/echo -s \"Playback Paused!\"");

    bPaused = true;
  }
  else
    pDTS_Color->bPause = true;

  return(true);
}
/*********************************************************************/
bool ColorResume(void)
// Purpose: Called from Colorizer.exe to resume playback
// after pausing.
// Shared Memory: pDTS_Color structure
{
  if (pDTS_Color == NULL || pDTS_Color->bResume)
    return(false);

  pDTS_Color->bStop = false; // flush any pending commands
  pDTS_Color->bPause = false;
  pDTS_Color->bStart = false;

  if (pDTS_Color->bUseDDE)
  {
    if (IsPirchVortec())
      Senddde("/display \"Playback Resumed!\"");
    else
      Senddde("/echo -s \"Playback Resumed!\"");

    bPaused = false;
  }
  else
    pDTS_Color->bResume = true;

  return(true);
}
/*********************************************************************/
int Colorize_Init(Tcl_Interp *interp)
// Called by XiRC when it loads this DLL
{
	if (Tcl_CreateCommand && Tcl_AppendResult && Tcl_Eval)
  {
		(*Tcl_CreateCommand)(interp, "DTS_play", CmdPlay, NULL, NULL);
		(*Tcl_CreateCommand)(interp, "DTS_poll", CmdPoll, NULL, NULL);
		(*Tcl_CreateCommand)(interp, "DTS_version", CmdVersion, NULL, NULL);
		(*Tcl_CreateCommand)(interp, "DTS_ex", CmdEx, NULL, NULL);
		(*Tcl_CreateCommand)(interp, "DTS_chan", CmdChan, NULL, NULL);
  	return TCL_OK;
  }

	return TCL_ERROR;
}
/*********************************************************************/
LPTSTR Colorize_Version(void)
// Purpose: Called from Colorizer.exe to request version.
{
  return DTS_VERSION;
}
/*********************************************************************/
/*********************************************************************/
/*                         General Functions                         */
/*********************************************************************/
/*********************************************************************/

int StartLocalFilePlay(Tcl_Interp *interp)
{
  DWORD dwFileSizeHigh;
  DWORD dwFileSize;

  pDTS_Color->bStart = false; // Do this to prevent reentrant call
                              // from DoOneEvent
  bPaused = false;

  if (pDTS_Color->Filename != NULL && pDTS_Color->Channel != NULL)
  {
    StopPlay(); // Stop any play in-progress

    // Open file
    if ((hFile = CreateFile(pDTS_Color->Filename,
            GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,NULL)) == INVALID_HANDLE_VALUE)
    {
      ErrorHandler("Could not open file.",pDTS_Color->Filename);
    	return TCL_ERROR;
    }

    // Determine file's size
    dwFileSize = GetFileSize(hFile,&dwFileSizeHigh);
    if (dwFileSizeHigh)
    {
      ErrorHandler("File is too big!");
    	return TCL_ERROR;
    }

    if (dwFileSize == 0)
    {
      ErrorHandler("Play file is empty!");
    	return TCL_ERROR;
    }

    // Create a heap in virtual memory
    if ((hHeap = HeapCreate(0,dwFileSize,0)) == INVALID_HANDLE_VALUE)
    {
      ErrorHandler("Cannot create heap!");
    	return TCL_ERROR;
    }

    // Allocate space for the file
    if ((lpHeap = HeapAlloc(hHeap,0,dwFileSize)) == NULL)
    {
      ErrorHandler("Insufficient heap space!");
    	return TCL_ERROR;
    }

    // Read the entire file into virtual memory
    (void)ReadFile(hFile, lpHeap, dwFileSize, &dwBytesRead, NULL);
    if (dwBytesRead != dwFileSize)
    {
      ErrorHandler("Could not read entire file!");
    	return TCL_ERROR;
    }

    // Finished with the file
    CloseHandle(hFile);

    // Initialize vars and flags
    bDataReady = bEndOfFile = bPaused = false;
    dwByteCount = 0; // Counts total bytes processed from heap buffer

    // Go ahead and queue first line
    QueueNextLineForTransmit();

    // Begin timer thread
    TimerID = SetTimer(NULL,      // no main window handle in a DLL
          0,                      // timer identifier
          pDTS_Color->PlayTime,   // delay in ms
          (TIMERPROC) OnTimer1);  // timer callback

    // Fetch and send text lines on timer event, yielding to XiRC
    // XiRC only calls our polling routine once per second,
    // but we can send data to a chat window much faster, so this
    // code gates data to XiRC at a programmable rate set in Colorizer.exe
    // while still allowing other XiRC scripts to run.
    if (!pDTS_Color->bUseDDE && interp != NULL)
    {
      // Run loop for XiRCON
      while((!bEndOfFile || bDataReady) && !pDTS_Color->bStop)
      {
        if (bDataReady) // bDataDeady triggered by timer callback event
        {
          Sendtcl(interp, GlobalString);
          bDataReady = false;
        }
        else if (!pDTS_Color->bUseDDE) // Yield to other TCL processes
          (void)Tcl_DoOneEvent(TCL_DONT_WAIT);
      }

      if (pDTS_Color->bStop)
        Sendtcl(interp, "echo \"Playback Stopped!\" status");
      else
        Sendtcl(interp, "echo \"Playback Ended!\" status");

      StopPlay();
    }
  }

  return TCL_OK;
}
/*********************************************************************/
void StopPlay(void)
{
  pDTS_Color->bStop = false;

  // Finished with the timer
  if (TimerID)
  {
    KillTimer(NULL, TimerID);
    TimerID = NULL;
  }

  // Finished with the heap
  if (hHeap != NULL)
  {
    HeapDestroy(hHeap);
    hHeap = NULL;
  }

  // Finished with the file
  if (hFile != NULL)
  {
    CloseHandle(hFile);
    hFile = NULL;
  }

  bPaused = false;

// Nice idea to delete the temp files BUT - we will call stop
// when the chat-client is still reading files :) so - nice try but
// no cigar...
/*
  // Delete files
  char *FileNameBuf;

  // Allocate file-name buffer
  if ((FileNameBuf = (char *)malloc(MAX_PATH)) == NULL)
  {
    ErrorHandler("Error creating filename buffer");
    return;
  }

  GetTempPath(MAX_PATH, FileNameBuf);
  strcat(FileNameBuf, TEMPFILE_0);
  DeleteFile(FileNameBuf);
  GetTempPath(MAX_PATH, FileNameBuf);
  strcat(FileNameBuf, TEMPFILE_1);
  DeleteFile(FileNameBuf);
  GetTempPath(MAX_PATH, FileNameBuf);
  strcat(FileNameBuf, TEMPFILE_2);
  DeleteFile(FileNameBuf);
  GetTempPath(MAX_PATH, FileNameBuf);
  strcat(FileNameBuf, TEMPFILE_3);
  DeleteFile(FileNameBuf);

  free(FileNameBuf);
*/
}
/*********************************************************************/
void Senddde(char* tempstr)
{
  try
  {
    //Connect to the service and request the topic
    if ((hcnv = DdeConnect(idInst, hszService, hszTopic, NULL)) == 0)
    {
      ColorStop();
      return;
    }

    //Start a DDE transaction
    if (DdeClientTransaction((LPBYTE)tempstr, strlen(tempstr)+1,
          hcnv, hszItem, CF_TEXT, XTYP_POKE, 5000, &dwResult) == 0)
    {
      UINT result = DdeGetLastError(idInst);
      switch (result)
      {
        case DMLERR_ADVACKTIMEOUT:
        case DMLERR_BUSY:
        case DMLERR_DATAACKTIMEOUT:
        case DMLERR_DLL_NOT_INITIALIZED:
        case DMLERR_EXECACKTIMEOUT:
        case DMLERR_INVALIDPARAMETER:
        case DMLERR_MEMORY_ERROR:
        case DMLERR_NO_CONV_ESTABLISHED:
        case DMLERR_NO_ERROR:
        case DMLERR_NOTPROCESSED:
        case DMLERR_POKEACKTIMEOUT:
        case DMLERR_POSTMSG_FAILED:
        case DMLERR_REENTRANCY:
        case DMLERR_SERVER_DIED:
        case DMLERR_UNADVACKTIMEOUT:
        default:
          ColorStop();
      }
    }

    //Free DDE
    DdeDisconnect(hcnv);
  }
  catch(...)
  {
    ErrorHandler("Exception thrown in Senddde()!");
  }
}
/*********************************************************************/
void Sendtcl(Tcl_Interp *interp, char *tempstr)
{
  try
  {
    (*Tcl_Eval)(interp, tempstr);
  }
  catch(...)
  {
    ErrorHandler("Exception thrown in Sendtcl()!");
  }
}
/*********************************************************************/
void ErrorHandler(LPTSTR Info, LPTSTR Extra)
// Purpose: Close open handles and display message box for error
// Args: String with information to display in message box
// Globals Used: hHeap, TimerID, hFile
{
  char TempString[300];

  StopPlay();
  strcpy(TempString,"Colorize.dll:");
  strcat(TempString,Info);
  if (Extra != NULL)
  {
    strcat(TempString,"\r\n");
    strcat(TempString,Extra);
  }
  MessageBox(NULL,TempString,DEFTITLE,MB_OK|MB_SETFOREGROUND);
}
/*********************************************************************/
void QueueNextLineForTransmit(void)
// Purpose: Format data from virtual memory buffer into GlobalString
// Globals Used: bEndOfFile, bDataReady, lpHeap, dwByteCount, dwBytesRead,
//               GlobalString
// Custom Functions Called: PrintString()
{
  if (dwBytesRead == 0)
  {
    StopPlay();
    return;
  }

  // immediate stop-play for mIRC if detaching or user-stop
  if (pDTS_Color->bUseDDE && pDTS_Color->bStop)
  {
    StopPlay();

//    if (IsPirchVortec())
//      Senddde("/display \"Playback Stopped!\"");
//    else
//      Senddde("/echo -s \"Playback Stopped!\"");

    return;
  }

  DWORD dwStringCount;
  char* lpBuf;

  // Queue data for Eval
  if (!bEndOfFile && !bDataReady)
  {
    // Read through the file (now in virtual memory)
    lpBuf = (char*)lpHeap; // Point to start of heap

    dwStringCount = 0;

    for(; dwByteCount < dwBytesRead ; dwByteCount++)
    {
      if (lpBuf[dwByteCount] != '\r' && lpBuf[dwByteCount] != '\n' &&
                   dwStringCount < GLOBALSTRINGSIZ-1)
        GlobalString[dwStringCount++] = lpBuf[dwByteCount];
      else if (lpBuf[dwByteCount] == '\n')
      {
        // Add a terminating CTRL_K if space(s) at end of line to prevent
        // them from being trimmed off by some clients
        if (dwStringCount && GlobalString[dwStringCount-1] == ' ')
          GlobalString[dwStringCount++] = CTRL_K;
        GlobalString[dwStringCount] = NULLCHAR;
//        PrintString(pDTS_Color->PlayTime);
        PrintString(0); // play file with no delay!
        bDataReady = true;
        dwByteCount++;
        break;
      }
    }

    // Finished reading the file?
    if (dwByteCount >= dwBytesRead)
    {
      if (!bDataReady)
      {
        // Add a terminating CTRL_K if space(s) at end of line to prevent
        // them from being trimmed off by some clients
        if (dwStringCount && GlobalString[dwStringCount-1] == ' ')
          GlobalString[dwStringCount++] = CTRL_K;
        GlobalString[dwStringCount] = NULLCHAR;

        if (dwStringCount)
        {
//          PrintString(pDTS_Color->PlayTime);
          PrintString(0); // play file with no delay!
          bDataReady = true;
        }
      }

      bEndOfFile = true;
    }
  }

  // pump data directly via DDE if this is mIRC
  if (pDTS_Color->bUseDDE)
  {
    //Start a DDE transaction
    if (bDataReady)
    {
      Senddde(GlobalString);
      bDataReady = false;
    }

    if (bEndOfFile)
    {
      StopPlay();

//      if (IsPirchVortec())
//        Senddde("/display \"Playback Ended!\"");
//      else
//        Senddde("/echo -s \"Playback Ended!\"");
    }
  }
}
/*********************************************************************/
bool PrintString(int Time)
// Purpose: Convert data from GlobalString into
//    a file-play command-string to send to
//    client via ether DDE or Tcl.
// Globals Used: GlobalString, String
// Shared Memory Vars: pDTS_Color->Channel
{
  UINT length = strlen(GlobalString);

  if (length == 0)
    length = sprintf(GlobalString, "\r\n");

  char *tString;

  // Allocate a buffer large enough to handle a case where every
  // char was a " or a \ (requiring insertion of \ escape chars
  // plus a NULL
  if ((tString = (char*)malloc(2*length+1)) == NULL)
  {
    ErrorHandler("Error allocating command buffer");
    return false;
   }

  char* FileNameBuf;

  // Allocate file-name buffer
  if ((FileNameBuf = (char*)malloc(MAX_PATH)) == NULL)
  {
    ErrorHandler("Error creating filename buffer");
    return false;
  }

  UINT ii,jj;

  // Scan string for " characters or \ characters
  // \\ and \" are needed for text between quotes in C
  for (ii = 0, jj = 0 ; ii < length ; ii++, jj++)
  {
    if (!pDTS_Color->bUseDDE)
    {
      if (GlobalString[ii] == '"' || GlobalString[ii] == '\x5c')
        // allow " and \ chars
        tString[jj++] = '\x5c';

      tString[jj] = GlobalString[ii];
    }
    else // mIRC will interpret $# as a parameter! (replace with ' ')
    {
      if (ii+1 < length && GlobalString[ii] == '$' &&
                                            isdigit(GlobalString[ii+1]))
        tString[jj] = ' ';
      else
        tString[jj] = GlobalString[ii];
    }
  }

  tString[jj] = NULLCHAR;

  if (!strcmp("status", stolower(pDTS_Color->Channel)))
  {
    if (pDTS_Color->bUseDDE) // echo to mIRC
    {
      // Writing one line to a file and using the /play command
      // eliminates the mIRC bug of stripping out spaces...
      //
      // ORIGINALLY, setting UseFile triggered mode of writing to a
      // temp-file... but NOW, YahCoLoRiZe can locally do /msg driven
      // playback or send just the /play file command when UseDll is
      // unchecked.  When UseDll is checked, we want to have two modes
      // in the dll.  If UseFile is checked, we want to play data
      // from the file written by YahCoLoRiZe (a full file), one line
      // at a time via /play and through a temp-file here.
      //
      // If UseFile us NOT checked, YahCoLoRiZe does not write a
      // big file for us to play here, instead it used a "one-line"
      // mode (via setting PlayTime < 0)... but HERE, we still
      // want to buffer that text through a temp-file to keep
      // mIRC/PIRCH from stripping spaces out...
      //if (pDTS_Color->bUseFile)

      if (IsPirchVortec())
      {
        // Pirch bug seems to require a leading CTRL_K or else the first
        // color-sequence CTRL_K is skipped...
        for (int ii = strlen(tString) ; ii >= 0  ; ii--)
          tString[ii+1] = tString[ii];
        tString[0] = '\003'; // leading CTRL_K
      }

      if (DTS_WriteLineToFile(FileNameBuf, tString) == false)
      {
        ErrorHandler("Error writing main temp file");
        free(FileNameBuf);
        return false;
      }

      // send to status window
      if (IsPirchVortec())
        // this won't work - pirch has no switch for status...
        sprintf(GlobalString, "/playfile -s %s", FileNameBuf);
      else // we only play a one-line file, NOTE: DO NOT USE -p!
        sprintf(GlobalString, "/play -s %s %i", FileNameBuf, Time);

//      if (IsPirchVortec())
        // Pirch bug seems to require a leading CTRL_K
        // or else the first color-sequence CTRL_K is skipped
//        sprintf(GlobalString, "/display %c%s",'\003',tString);
//      else
//        sprintf(GlobalString, "/echo -s %s",tString);
    }
    else // echo to XiRCON
      sprintf(GlobalString, "echo \"%s\" status",tString);
  }
  else // not to status window
  {
    if (pDTS_Color->bUseDDE) // msg to mIRC via file
    {
      // Writing one to a temp file and using the /play command
      // eliminates the mIRC bug of stripping out spaces...
      if (DTS_WriteLineToFile(FileNameBuf, tString) == false)
      {
        ErrorHandler("Error writing mIRC temp file");
        free(FileNameBuf);
        return false;
      }

      if (IsPirchVortec())
        sprintf(GlobalString, "/playfile %s %s", pDTS_Color->Channel,
                                                          FileNameBuf);
      else // we only play a one-line file, NOTE: DO NOT USE -p!
        sprintf(GlobalString, "/play %s %s %i", pDTS_Color->Channel,
                                                   FileNameBuf, Time);
    }
    else // msg to XiRCON
      sprintf(GlobalString, "/msg %s \"%s\"",pDTS_Color->Channel,tString);
  }

  free(tString);
  free(FileNameBuf);
  return true;
}
/*********************************************************************/
char* stolower(char* p)
{
  char* savep = p;

  int limit = GLOBALSTRINGSIZ;

  while(limit--)
  {
    char c = *p;

    if (c == NULLCHAR)
      return savep;

    *p++ = (char)tolower(c);
  }

  return savep;
}
/*********************************************************************/
bool DTS_WriteLineToFile(char* FileNameBuf, char* tString)
// We write one line at a time to a file then play that file
// via a DDE command "/playfile" for PIRCH, "/play" for mIRC.
// We do this because sending using /msg will strip spaces...
// playing a file sends the line as-is (good for ascii-art,
// lyrics, etc)
{
  static int Unique = 0;

  HANDLE hWriteFile;
  unsigned long BytesWritten;

  GetTempPath(MAX_PATH, FileNameBuf);

  // Create set of 4 temp files
  int n = ++Unique % 4;

  if (n == 0)
    strcat(FileNameBuf, TEMPFILE_0);
  else if (n == 1)
    strcat(FileNameBuf, TEMPFILE_1);
  else if (n == 2)
    strcat(FileNameBuf, TEMPFILE_2);
  else
    strcat(FileNameBuf, TEMPFILE_3);

  // Try to create in virtual memory... FILE_ATTRIBUTE_TEMPORARY
  if ((hWriteFile = CreateFile(FileNameBuf,
        GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY,NULL)) == INVALID_HANDLE_VALUE)
    return(false);

  if (WriteFile(hWriteFile, tString,
            strlen(tString)+1, &BytesWritten, NULL) == 0)
    return(false);

  CloseHandle(hWriteFile);
  return true;
}
/*********************************************************************/
bool IsPirchVortec(void)
{
  return IsPirch() || IsVortec();
}
/*********************************************************************/
bool IsPirch(void)
{
  return !strcmp("pirch", stolower(pDTS_Color->Service));
}
/*********************************************************************/
bool IsVortec(void)
{
  return !strcmp("vortec", stolower(pDTS_Color->Service));
}
/*********************************************************************/
/*********************************************************************/
/*********************************************************************/


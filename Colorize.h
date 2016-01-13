// Copyright 2015 Scott Swift - This program is distributed under the
// terms of the GNU General Public License.

#ifndef __colorize_h
#define __colorize_h

#define TCL_OK 0
#define TCL_ERROR 1

#define FIFOSIZE 4
#define GLOBALSTRINGSIZ 5000

// YahCoLoRiZe class-name
#define W_CLASS "TDTSColor"
// Custom message strings
#define M_CHAN "WM_ChanCoLoRiZe"
#define M_DATA "WM_DataCoLoRiZe"
#define M_PLAY "WM_PlayCoLoRiZe"

// Places to look (in this order) for Xtcl.dll
#define XTCLPATH1 "Xtcl.dll"
#define XTCLPATH2 "\\Program Files (x86)\\XiRCON\\Xtcl.dll"
#define XTCLPATH3 "\\Program Files\\XiRCON\\Xtcl.dll"
/*
 * When a TCL command returns, the string pointer interp->result points to
 * a string containing return information from the command.  In addition,
 * the command procedure returns an integer value, which is one of the
 * following:
 *
 * TCL_OK		Command completed normally;  interp->result contains
 *			the command's result.
 * TCL_ERROR		The command couldn't be completed successfully;
 *			interp->result describes what went wrong.
 * TCL_RETURN		The command requests that the current procedure
 *			return;  interp->result contains the procedure's
 *			return value.
 * TCL_BREAK		The command requests that the innermost loop
 *			be exited;  interp->result is meaningless.
 * TCL_CONTINUE		Go on to the next iteration of the current loop;
 *			interp->result is meaningless.
 */
#define TCL_OK          0
#define TCL_ERROR       1
#define TCL_RETURN      2
#define TCL_BREAK       3
#define TCL_CONTINUE    4

/*
 * Flag values passed to variable-related procedures.
 */
#define TCL_GLOBAL_ONLY      1
#define TCL_LEAVE_ERR_MSG    0x200

/*
 * Flag values to pass to Tcl_DoOneEvent to disable searches
 * for some kinds of events:
 */
#define TCL_DONT_WAIT       (1<<1)
#define TCL_WINDOW_EVENTS   (1<<2)
#define TCL_FILE_EVENTS     (1<<3)
#define TCL_TIMER_EVENTS    (1<<4)
#define TCL_IDLE_EVENTS     (1<<5)  /* WAS 0x10 ???? */
#define TCL_ALL_EVENTS      (~TCL_DONT_WAIT)

typedef void (Tcl_FreeProc) (char *blockPtr);

#define TCL_STATIC      ((Tcl_FreeProc *) 0)
#define TCL_VOLATILE    ((Tcl_FreeProc *) 1)
#define TCL_DYNAMIC     ((Tcl_FreeProc *) 3)

// Temp file names (delete on Stop())
#define TEMPFILE_0 "mrc5290.tmp"
#define TEMPFILE_1 "mrc5291.tmp"
#define TEMPFILE_2 "mrc5292.tmp"
#define TEMPFILE_3 "mrc5293.tmp"

// Terminate outgoing lines with this to prevent some clients from
// trimming off trailing spaces...
#define CTRL_K 0x03

typedef struct {
	char * result;
	void (*freeProc)(char *blockPtr);
	int errorline;
} Tcl_Interp;

typedef struct {
  bool bStart, bStop, bPause, bResume, bUseDDE, bUseFile;
	int PlayTime;
	int FiFoIn;
	int FiFoOut;
  char Service[64];
  char Channel[128];
  char Filename[2048]; // big enough for a chat-text line...
  char FiFo[FIFOSIZE][2048]; // fifo buffer...
} DTS_Color;

typedef int Tcl_CmdProc(void *cd, Tcl_Interp *interp, int argc, char *argv[]);

/* Typedefed Tcl functions */
typedef void (*dyn_CreateCommand)(Tcl_Interp *,const char *,
                                  Tcl_CmdProc *,void *,void *);
typedef int  (*dyn_AppendResult)(void *, ...);
typedef int  (*dyn_DoOneEvent)(int);
typedef int  (*dyn_Eval)(Tcl_Interp *,char *);

#endif /* __colorize_h */


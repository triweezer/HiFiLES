#include "stdafx.h"
#include "MASTER.h"

#define TECPLOTENGINEMODULE

/*
******************************************************************
******************************************************************
*******                                                   ********
******  (C) 1988-2008 Tecplot, Inc.                        *******
*******                                                   ********
******************************************************************
******************************************************************
*/
/*
 *   Revision History
 *
 *   10/06/94 - Added TECFIL routine.
 *   03/21/94 - Added TECTXT, TECGEO, and TECLAB routines 
 *   03/23/94 - Fixed for SUN compiler.
BEGIN CODELOG TECXXX
C 04/16/96 (BDP)
C   Added LONGUSES8BYTES compiler directive.
C   (needed for some IRIX machines)
C 03/14/97 (BDP)
C   Incorporated into TecUtil_ functions in
C   main tecplot core.
C 10/28/97 (CAM)
C   Changed routines to be __STDCALL in Windows
C 10/30/97 (CAM)
C   Changed TECDAT to use void * 
C 11/04/97 (CAM)
C   Changed TECDAT, et al to check for write
C   failures.  Revised error messages.  Added
C   in V7.5 code that mops up TECEND correctly.
C   "ifdef" out all printf debugging code.
C 01/28/98 (DTO)
C   Added 'char *mfc' param to TECTXT and TECGEO 
C 07/30/98 (DTO) 
C   Added 'char *DupList' param to TECZNE 
C 10/12/98 (CAM/DTO) 
C   Fixed output of debug info for Windows
C   Also added version info the the Windows DLL (CAM)
B 10/15/98 (CAM) 
B   Fixed problems with AlphaNT not linking with FORTRAN.
B   Also, fixed crash on AlphaNT related to generic problem
B   with TECGEO for squares, circles, rectangles, and ellipses
B 12/01/1999 (BDP) 
B   Fixed problem with duplist when you duplicate both variables
B   and the connectivity list.
V
V Version history out of date 01/31/2003
V
C 3/24/2003 (DET)
C   Changed TECXXX functions to TECXXX100, added AUX and FACE functions.
C 6/11/2003 (DTO)
C   Added back V9 functions for backward compatibility
C 03/17/2004 (CAM)
C   Win64 compatability
C 11/29/2005 (RMS)
C   Changed TECXXX and TECXXX100 functions to TECXXX110. Also added three new
C   parameters to TECZNE110: solution time, strand ID, and passive var list.
END CODELOG
 */


#include "GLOBAL.h"
#include "TASSERT.h"
#include "Q_UNICODE.h"
#include "SYSTEM.h"
#include "FILESTREAM.h"
#if defined TECPLOTKERNEL
/* CORE SOURCE CODE REMOVED */
#endif
#include "DATAIO4.h"
#include "DATASET0.h"
#include "TECXXX.h"
#include "DATAUTIL.h"
#include "ALLOC.h"

#if !defined MAKEARCHIVE
#include "AUXDATA.h"
#endif /* MAKEARCHIVE */

#ifdef MSWIN
#  include <io.h>
#endif

#ifdef UNIXX
#  include <stdio.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#endif


#if defined MAKEARCHIVE 
#  if defined MSWIN && defined _DEBUG
     /* For debug .dll builds, send debug info to debug window. */
#    define PRINT0(s) do { OutputDebugString(s); } while (0)
#    define PRINT1(s,a1) do { char buffer[512]; sprintf(buffer,s,a1); OutputDebugString(buffer); } while (0)
#    define PRINT2(s,a1,a2) do { char buffer[512]; sprintf(buffer,s,a1,a2); OutputDebugString(buffer); } while (0)
#  else
     /* For all other builds (including release .dll), send debug info to stdout. */
#    define PRINT0(s) printf(s)
#    define PRINT1(s,a1) printf(s,a1)
#    define PRINT2(s,a1,a2) printf(s,a1,a2)
#  endif
#else
#  if defined MSWIN
     /* For nonarchive, Windows, don't send debug info. */
#    define PRINT0(s) ((void)0)
#    define PRINT1(s,a1) ((void)0)
#    define PRINT2(s,a1,a2) ((void)0)
#  else
     /* For nonarchive, nonwindows, send debug info to stdout. */
#    define PRINT0(s) printf(s)
#    define PRINT1(s,a1) printf(s,a1)
#    define PRINT2(s,a1,a2) printf(s,a1,a2)
#  endif
#endif

typedef char     *FNameType;
typedef FILE     *FilePtr;

#define MaxNumFiles    10
#define MAX_DUPLIST_VARS  50 /* maybe crank up in the future */

#define TECIO_NO_NEIGHBORING_ELEM 0
#define TECIO_NO_NEIGHBORING_ZONE 0

#if defined MAKEARCHIVE
static LgIndex_t     DebugLevel[MaxNumFiles] = {0,0,0,0,0,0,0,0,0,0};
#endif
static INTEGER4      IsOpen[MaxNumFiles]  = {0,0,0,0,0,0,0,0,0,0};
static INTEGER4      NumErrs[MaxNumFiles] = {0,0,0,0,0,0,0,0,0,0};
static LgIndex_t     NumVars[MaxNumFiles];
static FNameType     DestFName[MaxNumFiles] = {NULL,NULL,NULL,NULL,NULL,
                                              NULL,NULL,NULL,NULL,NULL};
static FNameType     BlckFName[MaxNumFiles] = {NULL,NULL,NULL,NULL,NULL,
                                              NULL,NULL,NULL,NULL,NULL};
static FileStream_s *BlckFile[MaxNumFiles];
static FileStream_s *HeadFile[MaxNumFiles];
static FileOffset_t  MinMaxOffset[MaxNumFiles][MaxNumZonesOrVars];
static double        VarMinValue[MaxNumFiles][MaxNumZonesOrVars/*dim:Var*/];
static double        VarMaxValue[MaxNumFiles][MaxNumZonesOrVars/*dim:Var*/];
static INTEGER4      DoWriteForeign = FALSE; /* ...default is to write native */
static INTEGER4      IsWritingNative[MaxNumFiles];
static INTEGER4      IsBlock[MaxNumFiles];
static INTEGER4      ZoneType[MaxNumFiles];
static LgIndex_t     IMax[MaxNumFiles]; /* ones based indices */
static LgIndex_t     JMax[MaxNumFiles]; /* ones based indices */
static LgIndex_t     KMax[MaxNumFiles]; /* ones based indices */
static LgIndex_t     TotalNumFaceNodes[MaxNumFiles][MaxNumZonesOrVars];/* zones per file */
static LgIndex_t     TotalNumFaceBndryFaces[MaxNumFiles];
static LgIndex_t     TotalNumFaceBndryConns[MaxNumFiles];
static LgIndex_t     ICellMax[MaxNumFiles];
static LgIndex_t     JCellMax[MaxNumFiles];
static LgIndex_t     KCellMax[MaxNumFiles];
static INTEGER4      NumFaceConnections[MaxNumFiles][MaxNumZonesOrVars];/* zones per file */
static INTEGER4      FaceNeighborMode[MaxNumFiles];
static INTEGER4      FaceNeighborsOrMapWritten[MaxNumFiles][MaxNumZonesOrVars];/* flag per zone per file */
static INTEGER4      NumIndices[MaxNumFiles];
static LgIndex_t     NumDataValuesWritten[MaxNumFiles];
static LgIndex_t     NumOrderedCCDataValuesWritten[MaxNumFiles]; /* CC data only */
static LgIndex_t     NumDataValuesToWrite[MaxNumFiles];
static LgIndex_t     NumRunningVarValues[MaxNumFiles][MaxNumZonesOrVars/*dim:Var*/];
static Boolean_t     IsSharedVar[MaxNumFiles][MaxNumZonesOrVars/*dim:Var*/];
static Boolean_t     IsPassiveVar[MaxNumFiles][MaxNumZonesOrVars/*dim:Var*/];
static INTEGER4      CurZone[MaxNumFiles]; /* zero based zone numbers */
static INTEGER4      CurVar[MaxNumFiles];  /* zero based var numbers */
static INTEGER4      FieldDataType;
static INTEGER4      CurFile = -1;
static Boolean_t     IsCellCentered[MaxNumFiles][MaxNumZonesOrVars/*dim:Var*/];
static Boolean_t     HasFECONNECT[MaxNumFiles];
static INTEGER4      FileTypes[MaxNumFiles];
static INTEGER4      NumConnectivityNodes[MaxNumFiles][MaxNumZonesOrVars];/* zones per file */
static Boolean_t     ConnectivityWritten[MaxNumFiles][MaxNumZonesOrVars];/* flag per zone per file */
/*
 * From preplot.c:
 *
 * ZoneType 0=ORDERED,1=FELINESEG,2=FETRIANGLE,
 *          3=FEQUADRILATERAL,4=FETETRAHEDRON,5=FEBRICK,
 *          6=FEPOLYGON,7=FEPOLYHEDRON
 */
#define ORDERED 0
#define FELINESEG 1
#define FETRIANGLE 2
#define FEQUADRILATERAL 3
#define FETETRAHEDRON 4
#define FEBRICK 5
#define FEPOLYGON 6
#define FEPOLYHEDRON 7
/*
 * FileType 0=FULLFILE,1=GRIDFILE,2=SOLUTIONFILE
 */
#define FULLFILE 0
#define GRIDFILE 1
#define SOLUTIONFILE 2

#ifdef MAKEARCHIVE
static char *ZoneTypes[] =
  {
    "ORDERED",
    "FELINESEG",
    "FETRIANGLE",
    "FEQUADRILATERAL",
    "FETETRAHEDRON",
    "FEBRICK",
    "FEPOLYGON",
    "FEPOLYHEDRON"
  };
#endif /* MAKEARCHIVE */


static void WriteErr(const char *routine_name)
{
#ifdef MAKEARCHIVE
  PRINT2("Err: (%s) Write failure on file %d.\n", routine_name, CurFile+1);
#endif
  NumErrs[CurFile]++;
}

static LgIndex_t TecXXXZoneNum = 0;

static Boolean_t ParseDupList(LgIndex_t **ShareVarFromZone,
                              LgIndex_t  *ShareConnectivityFromZone,
                              const char *DupList)
{
  Boolean_t IsOk = TRUE;

  REQUIRE(VALID_REF(ShareVarFromZone) && *ShareVarFromZone == NULL);
  REQUIRE(VALID_REF(ShareConnectivityFromZone));
  REQUIRE(VALID_REF(DupList));
  
  while (IsOk && *DupList)
    {
      /* skip leading spaces */
      while (*DupList && *DupList == ' ')
        DupList++;

      /* is this the FECONNECT keyword? */
      if(*DupList && !strncmp(DupList,"FECONNECT",9))
        *ShareConnectivityFromZone = TecXXXZoneNum;

      else if (*DupList && !isdigit(*DupList))
        IsOk = FALSE; /* syntax error */

      else if(*DupList)
        {
          int WhichVar = atoi(DupList);

          if (0 < WhichVar && WhichVar < MaxNumZonesOrVars)
            {
              if (!(*ShareVarFromZone))
                {
                  *ShareVarFromZone = ALLOC_ARRAY(MaxNumZonesOrVars,LgIndex_t,"Variable sharing list");
                  if (*ShareVarFromZone)
                    memset(*ShareVarFromZone, (char)0, MaxNumZonesOrVars * sizeof(LgIndex_t));
                }

              if (*ShareVarFromZone)
                (*ShareVarFromZone)[WhichVar - 1] = TecXXXZoneNum;
              else
                IsOk = FALSE;
            }
          else
            {
              /* Invalid var num */
              IsOk = FALSE;
            }
        }   

      /*
       * Skip to the comma. This
       * will also allow the syntax error
       * of more than one consecutive comma
       */

      while (*DupList && *DupList != ',')
        DupList++;

      /* skip past the comma (can handle the syntax error of more than 1 comma) */
      while (*DupList && *DupList == ',')
        DupList++;
    }  
  
  return IsOk;
}

/**
 */
static FileStream_s *OpenFileStream(const char *FilePath,
                                    const char *AccessMode,
                                    Boolean_t   IsByteOrderNative)
{
  REQUIRE(VALID_REF(FilePath));
  REQUIRE(VALID_REF(AccessMode));

  FileStream_s *Result = NULL;
  FILE         *File   = FOPEN(FilePath, AccessMode);
  if (File != NULL)
    {
      Result = FileStreamAlloc(File, IsByteOrderNative);
      if (Result == NULL)
        FCLOSE(File);
    }

  ENSURE((VALID_REF(Result) && VALID_REF(Result->File)) || Result == NULL);
  return Result;
}

/**
 */
static void CloseFileStream(FileStream_s **FileStream)
{
  REQUIRE(VALID_REF(FileStream));
  REQUIRE(VALID_REF(*FileStream) || *FileStream == NULL);

  if (*FileStream != NULL)
    {
      FCLOSE((*FileStream)->File);
      FileStreamDealloc(FileStream);
    }

  ENSURE(*FileStream == NULL);
}

/**
 * Get the best terminator (separator) character to use for the string. First
 * precedence goes to the new line then the command and finally by default the
 * space. NOTE: We use a do loop to allow it to be used as a single statement.
 */
#define GET_BEST_TERMINATOR_CHAR(CompoundStr, TerminatorChar) \
          do \
            { \
              if (strchr((CompoundStr), '\n') != NULL) \
                (TerminatorChar) = '\n'; \
              else if (strchr((CompoundStr), ',') != NULL) \
                (TerminatorChar) = ','; \
              else \
                (TerminatorChar) = ' '; \
            } while (0)


/**
 * TECINIXXX
 */
INTEGER4 LIBCALL TECINI111(char     *Title,
                           char     *Variables,
                           char     *FName,
                           char     *ScratchDir,
                           INTEGER4 *FileType,
                           INTEGER4 *Debug,
                           INTEGER4 *VIsDouble)
{
  size_t L;
  int    I;
  char   RName[80];
  char  *CPtr;
  int    NewFile = -1;

  /*
   * Note that users should not mix TECXXX, TEC100XXX, and TEC110XXX calls, but
   * just in case, initialize the TecXXXZoneNum variable.  It may not help, but
   * it doesn't hurt...
   */
  TecXXXZoneNum = 0;

#if defined MAKEARCHIVE
  InitInputSpecs();
#endif

  for (I = 0; (I < MaxNumFiles) && (NewFile == -1); I++)
    {
      if (!IsOpen[I])
        NewFile = I;
    }

  if (NewFile == -1)
    {
#ifdef MAKEARCHIVE
      PRINT1("Err: (TECINI111) Too many files (%d) opened for printing.\n",NewFile);
#endif
      return (-1);
    }

  if (CurFile == -1)
    CurFile = 0;

#if defined MAKEARCHIVE
  DebugLevel[NewFile] = *Debug;
#endif

  CurZone[NewFile] = -1;
  L = 0;
  if (FName != NULL)
    L = strlen(FName);
  if (L == 0)
    {
#ifdef MAKEARCHIVE
      PRINT1("Err: (TECINI111) Bad file name for file %d.\n",NewFile);
#endif
      return (-1);
    }
  DestFName[NewFile] = ALLOC_ARRAY(L+1,char,"data set fname");
  strcpy(DestFName[NewFile],FName);

# if defined (DOS)
    {
      sprintf(RName,"BLCKFILE.%03d",(int)(NewFile+1));
    }
# else
    {
      sprintf(RName,"tp%1dXXXXXX",NewFile+1);
    }
# endif

  L = strlen(RName);
  if (ScratchDir != NULL)
    L += strlen(ScratchDir) + 1; /* +1 for the slash delimeter */
  BlckFName[NewFile] = ALLOC_ARRAY(L+1,char,"data set fname");
  if (ScratchDir != NULL)
    {
      strcpy(BlckFName[NewFile],ScratchDir);
#     if defined DOS || defined MSWIN
        {
          strcat(BlckFName[NewFile],"\\");
        }
#     else
        {
          strcat(BlckFName[NewFile],"/");
        }
#     endif
    }
  else
    BlckFName[NewFile][0] = '\0';

  strcat(BlckFName[NewFile],RName);
  CHECK(strlen(BlckFName[NewFile]) <= L);

# ifdef MSWIN
    {
      _mktemp(BlckFName[NewFile]);
    }
# elif defined UNIXX
    {
      /*
       * POSIX compiant behavior is to make
       * sure umask is set correctly first.
       */
      mode_t OrigUmask = umask(0022); /* ...should produce rw------- */
      int FileDesc = mkstemp(BlckFName[NewFile]);
      if (FileDesc != -1)
        close(FileDesc);
      umask(OrigUmask);
    }
# endif

#ifdef MAKEARCHIVE
  if (DebugLevel[NewFile])
    {
      PRINT2("Scratch File #%d: %s\n",NewFile+1,BlckFName[NewFile]);
      PRINT2("Dest    File #%d: %s\n",NewFile+1,DestFName[NewFile]);
    }
#endif

  IsWritingNative[NewFile] = !DoWriteForeign;

#if defined TECPLOTKERNEL
/* CORE SOURCE CODE REMOVED */
#endif

  HeadFile[NewFile] = OpenFileStream(DestFName[NewFile],"wb", IsWritingNative[NewFile]);
  BlckFile[NewFile] = OpenFileStream(BlckFName[NewFile],"wb", IsWritingNative[NewFile]);

  if (BlckFile[NewFile] == NULL)
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECINI111) Cannot open scratch file for output.\n");
      PRINT0("     Check permissions in scratch directory.\n");
#endif
      NumErrs[NewFile]++;
      return (-1);
    }
  if (HeadFile[NewFile] == NULL)
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECINI111) Cannot open plot file.  Check permissions.\n");
#endif
      NumErrs[NewFile]++;
      return (-1);
    }

  WriteBinaryMagicAndVersion(HeadFile[NewFile]);

  /* Write file type */
  if (*FileType >= FULLFILE && *FileType <= SOLUTIONFILE)
    FileTypes[NewFile] = *FileType;
  else
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECINI111) Bad filetype argument.  Check documentation.\n");
#endif
      NumErrs[NewFile]++;
      return (-1);
    }

  CHECK(TecplotBinaryFileVersion == 111);
  if ( !WriteBinaryInt32(HeadFile[NewFile],(LgIndex_t)FileTypes[NewFile]) )
    {
      WriteErr("TECINI111");
      return (-1);
    }

  if ( !DumpDatafileString(HeadFile[NewFile],
                           Title,
                           TRUE) )
    {
      WriteErr("TECINI111");
      return (-1);
    }

  NumVars[NewFile] = 0;
  CPtr    = Variables;


  /*
   * Three possible variable name separators are accepted with the following
   * precidence: newline, comma, and space.
   */
    {
      char terminator;

      GET_BEST_TERMINATOR_CHAR(CPtr, terminator);
      while (*CPtr)
        {
          /* strip leading spaces */
          while (*CPtr && *CPtr == ' ')
            CPtr++;

          if (*CPtr)
            {
              NumVars[NewFile]++;
              /* skip to terminating character */
              while (*CPtr && *CPtr != terminator)
                CPtr++;
              /* skip past terminating character */
              if (*CPtr)
                CPtr++;
            }
        }
    }

#if 0
  /* A grid file can have no variables in it as long as there is a connectivity list */
  if (NumVars[NewFile] == 0 && FileTypes[NewFile] != GRIDFILE)
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECINI110) No variable names were defined.\n");
#endif
      NumErrs[NewFile]++;
      return (-1);
    }
#endif

#ifdef MAKEARCHIVE
  if (DebugLevel[NewFile])
    PRINT1("NumVars=%d\n",NumVars[NewFile]);
#endif

  if ( !WriteBinaryInt32(HeadFile[NewFile],(LgIndex_t)NumVars[NewFile]) )
    {
      WriteErr("TECINI110");
      return (-1);
    }

  CPtr = Variables;
    {
      char terminator;
      char TString[MaxChrsVarName+1];
      int I;

      GET_BEST_TERMINATOR_CHAR(CPtr, terminator);
      while (*CPtr)
        {
          /* skip leading space characters */
          while (*CPtr && *CPtr == ' ')
            CPtr++;
          if (*CPtr)
            {
              I=0;
              /* skip to terminator */
              while (*CPtr && *CPtr != terminator)
                {
                  TString[I++] = *CPtr++;
                }
              /* skip past terminator */
              if (*CPtr)
                CPtr++;

              /* strip trailing spaces */
              I--;
              while (I >=0 && TString[I] == ' ') 
                I--;

              TString[I+1] = '\0';

              if ( !DumpDatafileString(HeadFile[NewFile],TString,TRUE) )
                {
                  WriteErr("TECINI110");
                  return (-1);
                }
            }
        }
    }

  IsOpen[NewFile] = 1;

  if (*VIsDouble)
    FieldDataType = FieldDataType_Double;
  else
    FieldDataType = FieldDataType_Float;

  return (0);
}

INTEGER4 LIBCALL TECINI110(char     *Title,
                           char     *Variables,
                           char     *FName,
                           char     *ScratchDir,
                           INTEGER4 *Debug,
                           INTEGER4 *VIsDouble)
{
  INTEGER4 FType = FULLFILE;

  TecXXXZoneNum = 0;
  return TECINI111(Title,
                   Variables,
                   FName,
                   ScratchDir,
                   &FType,
                   Debug,
                   VIsDouble);
}

INTEGER4 LIBCALL TECINI100(char     *Title,
                           char     *Variables,
                           char     *FName,
                           char     *ScratchDir,
                           INTEGER4 *Debug,
                           INTEGER4 *VIsDouble)
{
  INTEGER4 FType = FULLFILE;

  TecXXXZoneNum = 0;
  return TECINI111(Title,
                   Variables,
                   FName,
                   ScratchDir,
                   &FType,
                   Debug,
                   VIsDouble);
}

INTEGER4 LIBCALL TECINI(char     *Title,
                        char     *Variables,
                        char     *FName,
                        char     *ScratchDir,
                        INTEGER4 *Debug,
                        INTEGER4 *VIsDouble)
{
  INTEGER4 FType = FULLFILE;

  TecXXXZoneNum = 0;
  return TECINI111(Title,
                   Variables,
                   FName,
                   ScratchDir,
                   &FType,
                   Debug,
                   VIsDouble);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL tecini111_(char     *Title,
                                        char     *Variables,
                                        char     *FName,
                                        char     *ScratchDir,
                                        INTEGER4 *FileType,
                                        INTEGER4 *Debug,
                                        INTEGER4 *VIsDouble)
{
  return TECINI111(Title,Variables,FName,ScratchDir,FileType,Debug,VIsDouble);
}

LIBFUNCTION INTEGER4 LIBCALL tecini110_(char     *Title,
                                        char     *Variables,
                                        char     *FName,
                                        char     *ScratchDir,
                                        INTEGER4 *Debug,
                                        INTEGER4 *VIsDouble)
{
  INTEGER4 FType = FULLFILE;
  return TECINI111(Title,Variables,FName,ScratchDir,&FType,Debug,VIsDouble);
}

LIBFUNCTION INTEGER4 LIBCALL tecini100_(char     *Title,
                                        char     *Variables,
                                        char     *FName,
                                        char     *ScratchDir,
                                        INTEGER4 *Debug,
                                        INTEGER4 *VIsDouble)
{
  INTEGER4 FType = FULLFILE;
  return TECINI111(Title,Variables,FName,ScratchDir,&FType,Debug,VIsDouble);
}

LIBFUNCTION INTEGER4 LIBCALL tecini_(char     *Title,
                                     char     *Variables,
                                     char     *FName,
                                     char     *ScratchDir,
                                     INTEGER4 *Debug,
                                     INTEGER4 *VIsDouble)
{
  INTEGER4 FType = FULLFILE;
  return TECINI111(Title,
                   Variables,
                   FName,
                   ScratchDir,
                   &FType,
                   Debug,
                   VIsDouble);
}
#endif


static int CheckData(const char *routine_name)
{

  if ( NumDataValuesToWrite[CurFile] != NumDataValuesWritten[CurFile] )
    {
#ifdef MAKEARCHIVE
      PRINT2("Err: (%s) Wrong number of data values in file %d:\n", routine_name, CurFile+1);
      PRINT2("     %d data values for Zone %d were processed,\n", NumDataValuesWritten[CurFile], CurZone[CurFile]+1);
      PRINT1("     %d data values were expected.\n", NumDataValuesToWrite[CurFile]);
#endif
      NumErrs[CurFile]++;
      return (-1);
    }
  return (0);
}

static int CheckFile(const char *routine_name)
{
  if ( (CurFile == -1) || (!IsOpen[CurFile]) )
    {
#ifdef MAKEARCHIVE
      PRINT2("Err: (%s) Attempt to use invalid file (%d).\n",
             routine_name, CurFile+1);
#endif
      return (-1);
    }
  return (0);
}

/**
 * Advances CurVar[CurFile] to the next non-shared active variable. TECDATXXX
 * clients should not supply values for shared or passive variables.
 */
static void AdvanceToNextVarWithValues(void)
{
  /* search for the next variable with values */
  do
    {
      CurVar[CurFile]++;
    } while (CurVar[CurFile] < NumVars[CurFile] &&
             (IsSharedVar[CurFile][CurVar[CurFile]] ||
              IsPassiveVar[CurFile][CurVar[CurFile]]));
}

/**
 * TECZNEXXX
 */
INTEGER4 LIBCALL TECZNE111(char     *ZnTitle,
                           INTEGER4 *ZnType,
                           INTEGER4 *IMxOrNumPts,
                           INTEGER4 *JMxOrNumElements,
                           INTEGER4 *KMxOrNumFaces,
                           INTEGER4 *ICellMx,
                           INTEGER4 *JCellMx,
                           INTEGER4 *KCellMx,
                           double   *SolutionTime,
                           INTEGER4 *StrandID,
                           INTEGER4 *ParentZone,
                           INTEGER4 *IsBlk,
                           INTEGER4 *NumFaceConn,
                           INTEGER4 *FNMode,
                           INTEGER4 *NumFaceNodes,
                           INTEGER4 *NumFaceBndryFaces,
                           INTEGER4 *NumFaceBndryConns,
                           INTEGER4 *PassiveVarList,
                           INTEGER4 *ValueLocation,
                           INTEGER4 *ShareVarFromZone,
                           INTEGER4 *ShareConnectivityFromZone)
{
  int        I;
  int        IsOk = 1;

  if ( CheckFile("TECZNE111") < 0 )
    return (-1);

  if ( CurZone[CurFile] > -1 )
    {
      if ( CheckData("TECZNE111") < 0 )
        return (-1);
    }

  if (NumVars[CurFile] == 0)
    {
      WriteErr("TECZNE111");
#ifdef MAKEARCHIVE
      PRINT1("Err: (TECZNE111) Cannot write out zones if numvars is equal to zero (file %d).\n",
             CurFile+1);
#endif
      return (-1);
    }

  if ( CurZone[CurFile] > MaxNumZonesOrVars - 2 ) /* -1 based */
    {
      WriteErr("TECZNE111");
#ifdef MAKEARCHIVE
      PRINT2("Err: (TECZNE111) Exceeded max number of zones (%d) in file %d.\n",
             MaxNumZonesOrVars, CurFile+1);
#endif
      return (-1);
    }

  if ( *StrandID < -1 )
    {
#ifdef MAKEARCHIVE
      PRINT2("Err: (TECZNE111) Invalid StrandID supplied for file %d, zone %d.\n",
             CurFile+1,CurZone[CurFile]+1+1);
#endif
      return (-1);
    }

  if ( *ParentZone < 0 )
    {
#ifdef MAKEARCHIVE
      PRINT2("Err: (TECZNE111) Invalid ParentZone supplied for file %d, zone %d.\n",
             CurFile+1,CurZone[CurFile]+1+1);
#endif
      return (-1);
    }

  NumDataValuesWritten[CurFile]          = 0;
  NumOrderedCCDataValuesWritten[CurFile] = 0;
  CurZone[CurFile]++;
  ZoneType[CurFile] = *ZnType;
  IMax[CurFile] = *IMxOrNumPts;
  JMax[CurFile] = *JMxOrNumElements;
  KMax[CurFile] = *KMxOrNumFaces;
  ICellMax[CurFile] = *ICellMx;
  JCellMax[CurFile] = *JCellMx;
  KCellMax[CurFile] = *KCellMx;
  /* Set the flags that connectivity, face neighbors or face map hasn't been written for the zone yet. */
  FaceNeighborsOrMapWritten[CurFile][CurZone[CurFile]] = FALSE;  
  ConnectivityWritten[CurFile][CurZone[CurFile]] = FALSE;

  if (ZoneType[CurFile] == ZoneType_FEPolygon ||
      ZoneType[CurFile] == ZoneType_FEPolyhedron)
    {
      NumFaceConnections[CurFile][CurZone[CurFile]] = 0; /* ...not used for polytope data */
      FaceNeighborMode[CurFile]   = 0; /* ...not used for polytope data */
      NumConnectivityNodes[CurFile][CurZone[CurFile]] = 0; /* ...not used for polytope data */

      IsBlock[CurFile]                             = TRUE; /* ...polytope data is always block */
      TotalNumFaceNodes[CurFile][CurZone[CurFile]] = *NumFaceNodes;
      TotalNumFaceBndryFaces[CurFile]              = *NumFaceBndryFaces;
      TotalNumFaceBndryConns[CurFile]              = *NumFaceBndryConns;
    }
  else /* ...classic data */
    {
      IsBlock[CurFile]                              = *IsBlk;
      NumFaceConnections[CurFile][CurZone[CurFile]] = *NumFaceConn;
      FaceNeighborMode[CurFile]                     = *FNMode;

      TotalNumFaceNodes[CurFile][CurZone[CurFile]] = 0; /* ...not used for classic data */
      TotalNumFaceBndryFaces[CurFile]              = 0; /* ...not used for classic data */
      TotalNumFaceBndryConns[CurFile]              = 0; /* ...not used for classic data */
    }

  WriteBinaryReal(HeadFile[CurFile],
                  (double)ZoneMarker,
                  FieldDataType_Float);
  if ( !DumpDatafileString(HeadFile[CurFile],
                           ZnTitle,
                           TRUE) )
    {
      WriteErr("TECZNE111");
      return (-1);
    }

  if ((ShareVarFromZone && *ShareConnectivityFromZone) &&
      CurZone[CurFile] == 0)
    {
      /* can't have a duplist if there's nothing to duplicate */
      IsOk = 0;
    }

  if (IsOk == 0)
    {
#ifdef MAKEARCHIVE
      PRINT1("Err: (TECZNE111) Bad zone format for file %d.\n",CurFile+1);
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  switch (ZoneType[CurFile])
    {
      case ORDERED:
        NumIndices[CurFile] = 0;
        break;
      case FELINESEG:
        NumIndices[CurFile] = 2;
        break;
      case FETRIANGLE:
        NumIndices[CurFile] = 3;
        break;
      case FEQUADRILATERAL:
        NumIndices[CurFile] = 4;
        break;
      case FETETRAHEDRON:
        NumIndices[CurFile] = 4;
        break;
      case FEBRICK:
        NumIndices[CurFile] = 8;
        break;
    }
  
  /* ...not used for poly or ordered data and don't count sharing. */
  if (ZoneType[CurFile] != ZoneType_FEPolygon &&
      ZoneType[CurFile] != ZoneType_FEPolyhedron &&
      *ShareConnectivityFromZone == 0)
    NumConnectivityNodes[CurFile][CurZone[CurFile]] = NumIndices[CurFile] * JMax[CurFile];

  /*
   * We do not check any return values until the end. If these calls fail,
   * WriteFieldDataType below should fail as well.
   */
  WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)(*ParentZone)-1); /* ...ParentZone is zero based for binary file */
  WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)(*StrandID)-1);   /* ...StrandID is zero based for binary file */
  WriteBinaryReal(HeadFile[CurFile],*SolutionTime, FieldDataType_Double);
  WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)-1); /* No Zone Color Assignment */
  WriteBinaryInt32(HeadFile[CurFile],ZoneType[CurFile]);
  WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)(IsBlock[CurFile] ? 0 : 1));
  NumDataValuesToWrite[CurFile] = 0;
  for (I = 0; I < NumVars[CurFile]; I++)
    {
      IsSharedVar[CurFile][I]  = (ShareVarFromZone != NULL && ShareVarFromZone[I] != 0); /* ...shared? */
      IsPassiveVar[CurFile][I] = (PassiveVarList   != NULL && PassiveVarList[I]   == 1); /* ...passive? */
    }

  WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)(ValueLocation != NULL ? 1 : 0)); /* ...are var locations specified? */
  if (ValueLocation)
    {
      for (I = 0; I < NumVars[CurFile]; I++)
        {
          int        VIndex;
          LgIndex_t  NumNodes;
          LgIndex_t  NumCells;

          if (ZoneType[CurFile] == ORDERED)
            {
              NumNodes = IMax[CurFile] * JMax[CurFile] * KMax[CurFile];
              NumCells = (MAX(IMax[CurFile] - 1, 1) *
                          MAX(JMax[CurFile] - 1, 1) *
                          MAX(KMax[CurFile] - 1, 1));
            }
          else
            {
              NumNodes = IMax[CurFile];
              NumCells = JMax[CurFile];
            }

          if (IsSharedVar[CurFile][I])
            VIndex = ShareVarFromZone[I] - 1;
          else
            VIndex = I;

          if (VIndex == 0)
            NumRunningVarValues[CurFile][I] = 0;
          else
            NumRunningVarValues[CurFile][VIndex] = NumRunningVarValues[CurFile][VIndex-1];

          IsCellCentered[CurFile][VIndex] = (ValueLocation[I] == ValueLocation_CellCentered);
          if (ValueLocation[I] == ValueLocation_CellCentered)
            {
              WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)1);
              if (!IsSharedVar[CurFile][I] && !IsPassiveVar[CurFile][I])
                {
                  NumDataValuesToWrite[CurFile]        += NumCells;
                  NumRunningVarValues[CurFile][VIndex] += NumCells;
                }
            }
          else if (ValueLocation[I] == ValueLocation_Nodal)
            {
              WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)0);
              if (!IsSharedVar[CurFile][I] && !IsPassiveVar[CurFile][I])
                {
                  NumDataValuesToWrite[CurFile]        += NumNodes;
                  NumRunningVarValues[CurFile][VIndex] += NumNodes;
                }
            }
          else
            {
#ifdef MAKEARCHIVE
              PRINT2("Err: (TECZNE111) Bad zone value location for file %d, variable %d.\n",CurFile+1,I+1);
#endif
              NumErrs[CurFile]++;
              return(-1);
            }
        }
    }
  else
    {
      LgIndex_t NumNodes;
      if (ZoneType[CurFile] == ORDERED)
        {
          NumNodes = IMax[CurFile] * JMax[CurFile] * KMax[CurFile];
        }
      else
        {
          NumNodes = IMax[CurFile];
        }

      for(I = 0; I < NumVars[CurFile]; I++)
        {
          int VIndex;
          if (IsSharedVar[CurFile][I])
            VIndex = ShareVarFromZone[I] - 1;
          else
            VIndex = I;

          if (VIndex == 0)
            NumRunningVarValues[CurFile][I] = 0;
          else
            NumRunningVarValues[CurFile][VIndex] = NumRunningVarValues[CurFile][VIndex-1];

          IsCellCentered[CurFile][VIndex] = FALSE;
          if (!IsSharedVar[CurFile][I] && !IsPassiveVar[CurFile][I])
            {
              NumDataValuesToWrite[CurFile]        += NumNodes;
              NumRunningVarValues[CurFile][VIndex] += NumNodes;
            }
        }
    }

  /*
   * As of binary version 108 Tecplot introduced
   * the ability to output its auto-generated face
   * neighbor array in its raw form. For now
   * TecIO will always decline to perform this
   * step and instead fall back to the delivering
   * one neighbor at a time.
   */
  WriteBinaryInt32(HeadFile[CurFile], (LgIndex_t)0); /* IsRawFNAvailable */

  WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)NumFaceConnections[CurFile][CurZone[CurFile]]);
  if (NumFaceConnections[CurFile][CurZone[CurFile]] > 0)
    {
      WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)FaceNeighborMode[CurFile]);
      if (ZoneType[CurFile] != ORDERED)
        WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)0); /* FEFaceNeighborsComplete */
    }

  WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)IMax[CurFile]);
  if (ZoneType[CurFile] == FEPOLYGON ||
      ZoneType[CurFile] == FEPOLYHEDRON)
    {
      WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)KMax[CurFile]);

      /*
       * As of binary version 111 these items moved from the data section to
       * the header.
       */
      WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)TotalNumFaceNodes[CurFile][CurZone[CurFile]]);
      if (TotalNumFaceBndryFaces[CurFile] > 0)
        {
          /* Each boundary face must have >= 1 boundary connection. */
          if (TotalNumFaceBndryConns[CurFile] < TotalNumFaceBndryFaces[CurFile])
            {
#ifdef MAKEARCHIVE
              PRINT1("Err: (TECZNE111) There must be at least 1 boundary connection for each boundary face in zone %d.\n",
                     CurZone[CurFile]+1);
              PRINT2("     %d boundary faces and %d boundary connections were specified.\n",
                     TotalNumFaceBndryFaces[CurFile], TotalNumFaceBndryConns[CurFile]);
#endif
              NumErrs[CurFile]++;
              return(-1);
            }

          /*
           * As a convenience for the ASCII format, TecUtil, and TECIO layers if any
           * boundary connections exists we automatically add a no-neighboring
           * connection as the first item so that they can user 0 for no-neighboring
           * element in the element list regardless if they have boundary connections
           * or not.
           */
          WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)TotalNumFaceBndryFaces[CurFile]+1); /* ...add a boundary face for no neighboring element as a convenience */
        }
      else
        WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)TotalNumFaceBndryFaces[CurFile]);
      WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)TotalNumFaceBndryConns[CurFile]);
    }
  WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)JMax[CurFile]);

  if (ZoneType[CurFile] == ORDERED)
    {
      WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)KMax[CurFile]);
    }
  else
    {
      WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)ICellMax[CurFile]);
      WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)JCellMax[CurFile]);
      WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)KCellMax[CurFile]);
    }

  /*
   * Aux data. This has to be over-written by the aux data writing routine.
   * Because it currently at the end of the header section we don't need to
   * keep track of the position for seeking back to it.
   */
  WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)0);

  WriteBinaryReal(BlckFile[CurFile],
                  (double)ZoneMarker,
                  FieldDataType_Float);

  for (I = 0; I < NumVars[CurFile]; I++)
    {
      if ( !WriteFieldDataType(BlckFile[CurFile],
                               (FieldDataType_e)FieldDataType,
                               TRUE) )
        {
          WriteErr("TECZNE111");
          return (-1);
        }
    }

  /* Passive variable identification */
  if (PassiveVarList)
    {
      WriteBinaryInt32(BlckFile[CurFile], 1);
      for (I=0;I < NumVars[CurFile]; I++)
        WriteBinaryInt32(BlckFile[CurFile],PassiveVarList[I]);
    }
  else
    WriteBinaryInt32(BlckFile[CurFile], 0);

  /* get the CurVar[CurFile] on the first active variable */
  CurVar[CurFile] = -1;
  AdvanceToNextVarWithValues();

  /* Variable & Connectivity Sharing */
  if (ShareVarFromZone)
    {
      WriteBinaryInt32(BlckFile[CurFile], 1);
      for (I=0;I < NumVars[CurFile]; I++)
        WriteBinaryInt32(BlckFile[CurFile],ShareVarFromZone[I] - 1);
    }
  else
    WriteBinaryInt32(BlckFile[CurFile], 0);
  WriteBinaryInt32(BlckFile[CurFile],*ShareConnectivityFromZone - 1);

  /*
   * Create place holders or the variable min/max value. We will come back
   * later after writing the data portion with the real min/max values. In the
   * mean time, keep track of the starting point so we can seek back to this
   * place.
   */
  MinMaxOffset[CurFile][CurZone[CurFile]] = (FileOffset_t)FTELL(BlckFile[CurFile]->File);
  for (I=0;I < NumVars[CurFile]; I++)
    {
      /* initialize to unset values */
      VarMinValue[CurFile][I] =  LARGEDOUBLE;
      VarMaxValue[CurFile][I] = -LARGEDOUBLE;

      if (!IsSharedVar[CurFile][I] && !IsPassiveVar[CurFile][I])
        {
          WriteBinaryReal(BlckFile[CurFile],0.0,FieldDataType_Double);
          WriteBinaryReal(BlckFile[CurFile],0.0,FieldDataType_Double);
        }
    }

#ifdef MAKEARCHIVE
  if (DebugLevel[CurFile])
    {
      PRINT1("Writing Zone %d:\n",CurZone[CurFile]+1);
      PRINT1("      Title = %s\n",ZnTitle);
      PRINT1("      Type  = %s\n",ZoneTypes[ZoneType[CurFile]]);
      PRINT1("      IMax  = %d\n",IMax[CurFile]);
      PRINT1("      JMax  = %d\n",JMax[CurFile]);
      PRINT1("      KMax  = %d\n",KMax[CurFile]);
      if (ShareVarFromZone)
        {
          char DupList[1024] = "";

          for(I = 0; I < NumVars[CurFile]; I++)
            {
              if (I > 0)
                strcat(DupList, ",");
              sprintf(&DupList[strlen(DupList)], "%d", ShareVarFromZone[I]);
            }
          PRINT1("      DupList = %s\n",DupList);
        }
    }
#endif

  return (0);
}

INTEGER4 LIBCALL TECZNE110(char     *ZnTitle,
                           INTEGER4 *ZnType,
                           INTEGER4 *IMxOrNumPts,
                           INTEGER4 *JMxOrNumElements,
                           INTEGER4 *KMx,
                           INTEGER4 *ICellMx,
                           INTEGER4 *JCellMx,
                           INTEGER4 *KCellMx,
                           double   *SolutionTime,
                           INTEGER4 *StrandID,
                           INTEGER4 *ParentZone,
                           INTEGER4 *IsBlk,
                           INTEGER4 *NumFaceConn,
                           INTEGER4 *FNMode,
                           INTEGER4 *PassiveVarList,
                           INTEGER4 *ValueLocation,
                           INTEGER4 *ShareVarFromZone,
                           INTEGER4 *ShareConnectivityFromZone)
{
  INTEGER4 NumFaceNodes      = 0;
  INTEGER4 NumFaceBndryFaces = 0;
  INTEGER4 NumFaceBndryConns = 0;

  return TECZNE111(ZnTitle,
                   ZnType,
                   IMxOrNumPts,
                   JMxOrNumElements,
                   KMx,
                   ICellMx,
                   JCellMx,
                   KCellMx,
                   SolutionTime,
                   StrandID,
                   ParentZone,
                   IsBlk,
                   NumFaceConn,
                   FNMode,
                   &NumFaceNodes,
                   &NumFaceBndryFaces,
                   &NumFaceBndryConns,
                   PassiveVarList,
                   ValueLocation,
                   ShareVarFromZone,
                   ShareConnectivityFromZone);
}

INTEGER4 LIBCALL TECZNE100(char     *ZnTitle,
                           INTEGER4 *ZnType,
                           INTEGER4 *IMxOrNumPts,
                           INTEGER4 *JMxOrNumElements,
                           INTEGER4 *KMx,
                           INTEGER4 *ICellMx,
                           INTEGER4 *JCellMx,
                           INTEGER4 *KCellMx,
                           INTEGER4 *IsBlk,
                           INTEGER4 *NumFaceConn,
                           INTEGER4 *FNMode,
                           INTEGER4 *ValueLocation,
                           INTEGER4 *ShareVarFromZone,
                           INTEGER4 *ShareConnectivityFromZone)
{
  double   SolutionTime = 0.0;
  INTEGER4 StrandID   = STRAND_ID_STATIC+1; /* TECXXX is ones based for StrandID */
  INTEGER4 ParentZone = BAD_SET_VALUE+1;    /* TECXXX is ones based for ParentZone */
  INTEGER4 NumFaceNodes      = 0;
  INTEGER4 NumFaceBndryFaces = 0;
  INTEGER4 NumFaceBndryConns = 0;

  return TECZNE111(ZnTitle,
                   ZnType,
                   IMxOrNumPts,
                   JMxOrNumElements,
                   KMx,
                   ICellMx,
                   JCellMx,
                   KCellMx,
                   &SolutionTime,
                   &StrandID,
                   &ParentZone,
                   IsBlk,
                   NumFaceConn,
                   FNMode,
                   &NumFaceNodes,
                   &NumFaceBndryFaces,
                   &NumFaceBndryConns,
                   NULL, /* PassiveVarList */
                   ValueLocation,
                   ShareVarFromZone,
                   ShareConnectivityFromZone);
}

INTEGER4 LIBCALL TECZNE(char     *ZoneTitle,
                        INTEGER4 *IMx,
                        INTEGER4 *JMx,
                        INTEGER4 *KMx,
                        char     *ZFormat,
                        char     *DupList)
{

  LgIndex_t  ZoneType;
  LgIndex_t  IsBlock;
  LgIndex_t *ShareVarFromZone = NULL;
  LgIndex_t  ShareConnectivityFromZone;
  LgIndex_t  Result = 0;


  if (ZFormat == NULL)
    Result = -1;
  else if (!strcmp(ZFormat,"BLOCK"))
    {
      IsBlock = 1;
      ZoneType = ZoneType_Ordered;
    }
  else if (!strcmp(ZFormat,"FEBLOCK"))
    {
      IsBlock = 1;
      switch(*KMx)
        {
        /*
         * From preplot.c:
         *
         * ZoneType 0=ORDERED,1=FELINESEG,2=FETRIANGLE,
         * 3=FEQUADRILATERAL,4=FETETRAHEDRON,5=FEBRICK
         */
          case 0: /* Triangular. */
            ZoneType = 2;
            break;
          case 1: /* Quadrilateral */
            ZoneType = 3;
            break;
          case 2: /* Tetrahedral */
            ZoneType = 4;
            break;
          case 3: /* Brick. */
            ZoneType = 5;
            break;
        }
    }
  else if (!strcmp(ZFormat,"POINT"))
    {
      IsBlock = 0;
      ZoneType = ZoneType_Ordered;
    }
  else if (!strcmp(ZFormat,"FEPOINT"))
    {
      IsBlock = 0;
      switch(*KMx)
        {
          case 0: /* Triangular. */
            ZoneType = 2;
            break;
          case 1: /* Quadrilateral */
            ZoneType = 3;
            break;
          case 2: /* Tetrahedral */
            ZoneType = 4;
            break;
          case 3: /* Brick. */
            ZoneType = 5;
            break;
        }
    }
  else
    Result = -1;
  
  ShareConnectivityFromZone = 0;
  
  
  if (Result == 0 &&
      DupList &&
      !ParseDupList(&ShareVarFromZone, &ShareConnectivityFromZone, DupList))
    {
      Result = -1;
    } 

  /*Result = TECZNE((char *)ZoneTitle, IMx, JMx, KMx, (char *)ZFormat,(char*)DupList);*/
  if (Result == 0)
    {
      INTEGER4 ICellMx = 0;
      INTEGER4 JCellMx = 0;
      INTEGER4 KCellMx = 0;
      INTEGER4 NumFaceConnections = 0;
      INTEGER4 FaceNeighborMode   = FaceNeighborMode_LocalOneToOne;
      double   SolutionTime = 0.0;
      INTEGER4 StrandID   = STRAND_ID_STATIC+1; /* TECXXX is ones based for StrandID */
      INTEGER4 ParentZone = BAD_SET_VALUE+1;    /* TECXXX is ones based for ParentZone */
      INTEGER4 NumFaceNodes      = 0;
      INTEGER4 NumFaceBndryFaces = 0;
      INTEGER4 NumFaceBndryConns = 0;

      Result = TECZNE111((char *)ZoneTitle,
                         &ZoneType,
                         IMx,
                         JMx,
                         KMx,
                         &ICellMx,
                         &JCellMx,
                         &KCellMx,
                         &SolutionTime,
                         &StrandID,
                         &ParentZone,
                         &IsBlock,
                         &NumFaceConnections,
                         &FaceNeighborMode,
                         &NumFaceNodes,
                         &NumFaceBndryFaces,
                         &NumFaceBndryConns,
                         NULL, /* PassiveVarList */
                         NULL, /* ValueLocation */
                         DupList ? ShareVarFromZone : NULL,
                         &ShareConnectivityFromZone);
      TecXXXZoneNum++;
    }

  if (ShareVarFromZone)
    FREE_ARRAY(ShareVarFromZone, "Variable sharing list");

  return (INTEGER4) Result;
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL teczne111_(char     *ZoneTitle,
                                        INTEGER4 *ZnType,
                                        INTEGER4 *IMxOrNumPts,
                                        INTEGER4 *JMxOrNumElements,
                                        INTEGER4 *KMx,
                                        INTEGER4 *ICellMx,
                                        INTEGER4 *JCellMx,
                                        INTEGER4 *KCellMx,
                                        double   *SolutionTime,
                                        INTEGER4 *StrandID,
                                        INTEGER4 *ParentZone,
                                        INTEGER4 *IsBlk,
                                        INTEGER4 *NumFaceConn,
                                        INTEGER4 *FNMode,
                                        INTEGER4 *NumFaceNodes,
                                        INTEGER4 *NumFaceBndryFaces,
                                        INTEGER4 *NumFaceBndryConns,
                                        INTEGER4 *PassiveVarList,
                                        INTEGER4 *ValueLocation,
                                        INTEGER4 *ShareVarFromZone,
                                        INTEGER4 *ShareConnectivityFromZone)
{
  return TECZNE111(ZoneTitle,
                   ZnType,
                   IMxOrNumPts,
                   JMxOrNumElements,
                   KMx,
                   ICellMx,
                   JCellMx,
                   KCellMx,
                   SolutionTime,
                   StrandID,
                   ParentZone,
                   IsBlk,
                   NumFaceConn,
                   FNMode,
                   NumFaceNodes,
                   NumFaceBndryFaces,
                   NumFaceBndryConns,
                   PassiveVarList,
                   ValueLocation,
                   ShareVarFromZone,
                   ShareConnectivityFromZone);
}

LIBFUNCTION INTEGER4 LIBCALL teczne110_(char     *ZoneTitle,
                                        INTEGER4 *ZnType,
                                        INTEGER4 *IMxOrNumPts,
                                        INTEGER4 *JMxOrNumElements,
                                        INTEGER4 *KMx,
                                        INTEGER4 *ICellMx,
                                        INTEGER4 *JCellMx,
                                        INTEGER4 *KCellMx,
                                        double   *SolutionTime,
                                        INTEGER4 *StrandID,
                                        INTEGER4 *ParentZone,
                                        INTEGER4 *IsBlk,
                                        INTEGER4 *NumFaceConn,
                                        INTEGER4 *FNMode,
                                        INTEGER4 *PassiveVarList,
                                        INTEGER4 *ValueLocation,
                                        INTEGER4 *ShareVarFromZone,
                                        INTEGER4 *ShareConnectivityFromZone)
{
  INTEGER4 NumFaceNodes      = 0;
  INTEGER4 NumFaceBndryFaces = 0;
  INTEGER4 NumFaceBndryConns = 0;

  return TECZNE111(ZoneTitle,
                   ZnType,
                   IMxOrNumPts,
                   JMxOrNumElements,
                   KMx,
                   ICellMx,
                   JCellMx,
                   KCellMx,
                   SolutionTime,
                   StrandID,
                   ParentZone,
                   IsBlk,
                   NumFaceConn,
                   FNMode,
                   &NumFaceNodes,
                   &NumFaceBndryFaces,
                   &NumFaceBndryConns,
                   PassiveVarList,
                   ValueLocation,
                   ShareVarFromZone,
                   ShareConnectivityFromZone);
}

LIBFUNCTION INTEGER4 LIBCALL teczne100_(char     *ZoneTitle,
                                        INTEGER4 *ZnType,
                                        INTEGER4 *IMxOrNumPts,
                                        INTEGER4 *JMxOrNumElements,
                                        INTEGER4 *KMx,
                                        INTEGER4 *ICellMx,
                                        INTEGER4 *JCellMx,
                                        INTEGER4 *KCellMx,
                                        INTEGER4 *IsBlk,
                                        INTEGER4 *NumFaceConn,
                                        INTEGER4 *FNMode,
                                        INTEGER4 *ValueLocation,
                                        INTEGER4 *ShareVarFromZone,
                                        INTEGER4 *ShareConnectivityFromZone)
{
  return TECZNE100(ZoneTitle,
                   ZnType,
                   IMxOrNumPts,
                   JMxOrNumElements,
                   KMx,
                   ICellMx,
                   JCellMx,
                   KCellMx,
                   IsBlk,
                   NumFaceConn,
                   FNMode,
                   ValueLocation,
                   ShareVarFromZone,
                   ShareConnectivityFromZone);
}

LIBFUNCTION INTEGER4 LIBCALL teczne_(char     *ZoneTitle,
                                     INTEGER4 *IMx,
                                     INTEGER4 *JMx,
                                     INTEGER4 *KMx,
                                     char     *ZFormat,
                                     char     *DupList)
{
  return TECZNE(ZoneTitle,
                IMx,
                JMx,
                KMx,
                ZFormat,
                DupList);
}
#endif

/**
 * Rewrite the var min/max place holders which currently have zero in them.
 */
static void RewritePendingMinMaxValues(void)
{
  FileOffset_t CurrentOffset = (FileOffset_t)FTELL(BlckFile[CurFile]->File);

  FSEEK(BlckFile[CurFile]->File, MinMaxOffset[CurFile][CurZone[CurFile]], SEEK_SET);
  int I;
  for (I = 0; I < NumVars[CurFile]; I++)
    {
      if (!IsSharedVar[CurFile][I] && !IsPassiveVar[CurFile][I])
        {
          WriteBinaryReal(BlckFile[CurFile], VarMinValue[CurFile][I], FieldDataType_Double);
          WriteBinaryReal(BlckFile[CurFile], VarMaxValue[CurFile][I], FieldDataType_Double);
        }
    }

  /* return the original position */
  FSEEK(BlckFile[CurFile]->File, CurrentOffset, SEEK_SET);
}

/**
 * TECDATXXX
 */
INTEGER4 LIBCALL TECDAT111(INTEGER4 *N,
                           void     *Data,
                           INTEGER4 *IsDouble)
{
  LgIndex_t  I;
  double    *dptr = (double *)Data;
  float     *fptr =  (float *)Data;

  if ( CheckFile("TECDAT111") < 0 )
    return (-1);

#ifdef MAKEARCHIVE
  if ( DebugLevel[CurFile] && (*N > 1) )
  {
    PRINT2("Writing %d values to file %d.\n",*N, CurFile+1);
  }
#endif




  for (I = 0; I < *N; I++)
    {
      double Value = (*IsDouble == 1 ? dptr[I] : fptr[I]);

#ifdef MAKEARCHIVE
      if ( DebugLevel[CurFile] && (*N > 1) )
      {
        PRINT2("IsDouble=%d\n",*IsDouble,CurFile+1);
        PRINT2("Value=%16.21f\n",Value,CurFile+1);
      }
#endif

      /* keep track of var min/max */
      if (Value < VarMinValue[CurFile][CurVar[CurFile]])
        VarMinValue[CurFile][CurVar[CurFile]] = Value;
      if (Value > VarMaxValue[CurFile][CurVar[CurFile]])
        VarMaxValue[CurFile][CurVar[CurFile]] = Value;

      if ( !WriteBinaryReal(BlckFile[CurFile],Value,(FieldDataType_e)FieldDataType) )
        {
          WriteErr("TECDAT111");
          return (-1);
        }

      /*
       * As of version 103 Tecplot writes binary data files so that the ordered
       * cell centered field data includes the ghost cells. This makes it much
       * easier for Tecplot to map the data when reading by simply writing out
       * field data's as a block. As of version 104 the ghost cells of the
       * slowest moving index are not included.
       */
      if (IsCellCentered[CurFile][CurVar[CurFile]] && ZoneType[CurFile] == ORDERED)
        {
          CHECK(IsBlock[CurFile]); /* ...ordered CC data must be block format */
          LgIndex_t PIndex = (NumOrderedCCDataValuesWritten[CurFile]);
          LgIndex_t FinalIMax = MAX(IMax[CurFile]-1, 1);
          LgIndex_t FinalJMax = MAX(JMax[CurFile]-1, 1);
          LgIndex_t FinalKMax = MAX(KMax[CurFile]-1, 1);
          LgIndex_t IIndex = (PIndex % IMax[CurFile]);
          LgIndex_t JIndex = ((PIndex % (IMax[CurFile]*JMax[CurFile])) / IMax[CurFile]);
          LgIndex_t KIndex = (PIndex / (IMax[CurFile]*JMax[CurFile]));
          LgIndex_t IMaxAdjust = 0;
          LgIndex_t JMaxAdjust = 0;
          LgIndex_t KMaxAdjust = 0;
          if (KMax[CurFile] > 1)
            KMaxAdjust = 1; /* ...K is slowest */
          else if (JMax[CurFile] > 1)
            JMaxAdjust = 1; /* ...J is slowest */
          else if (IMax[CurFile] > 1)
            IMaxAdjust = 1; /* ...I is slowest */

          if (IIndex+1 == FinalIMax && FinalIMax < IMax[CurFile]-IMaxAdjust)
            {
              NumOrderedCCDataValuesWritten[CurFile]++;
              if ( !WriteBinaryReal(BlckFile[CurFile],0.0,(FieldDataType_e)FieldDataType) )
                {
                  WriteErr("TECDAT111");
                  return (-1);
                }
            }
          if (IIndex+1 == FinalIMax &&
              (JIndex+1 == FinalJMax && FinalJMax < JMax[CurFile]-JMaxAdjust))
            {
              LgIndex_t II;
              for (II = 1; II <= IMax[CurFile]-IMaxAdjust; II++)
                {
                  NumOrderedCCDataValuesWritten[CurFile]++;
                  if ( !WriteBinaryReal(BlckFile[CurFile],0.0,(FieldDataType_e)FieldDataType) )
                    {
                      WriteErr("TECDAT111");
                      return (-1);
                    }
                }
            }
          if (IIndex+1 == FinalIMax &&
              JIndex+1 == FinalJMax &&
              (KIndex+1 == FinalKMax && FinalKMax < KMax[CurFile]-KMaxAdjust))
            {
              LgIndex_t JJ,II;
              for (JJ = 1; JJ <= JMax[CurFile]-JMaxAdjust; JJ++)
              for (II = 1; II <= IMax[CurFile]-IMaxAdjust; II++)
                {
                  NumOrderedCCDataValuesWritten[CurFile]++;
                  if ( !WriteBinaryReal(BlckFile[CurFile],0.0,(FieldDataType_e)FieldDataType) )
                    {
                      WriteErr("TECDAT111");
                      return (-1);
                    }
                }
            }

          /* increment for the original cell value */
          NumOrderedCCDataValuesWritten[CurFile]++;
        }

      /* update the number of data points written */
      NumDataValuesWritten[CurFile]++;

      if (IsBlock[CurFile])
        {
          /* for block format update the variable when all values have been given */
          if ( NumRunningVarValues[CurFile][CurVar[CurFile]] == NumDataValuesWritten[CurFile] )
            {
              AdvanceToNextVarWithValues(); /* ...move on to the next variable */
              if (CurVar[CurFile] < NumVars[CurFile]       &&
                  IsCellCentered[CurFile][CurVar[CurFile]] &&
                  ZoneType[CurFile] == ORDERED)
                NumOrderedCCDataValuesWritten[CurFile] = 0; /* reset for next CC variable */
            }
        }
      else
        {
          /* for point format update the varaible after each value */
          AdvanceToNextVarWithValues();
          if (CurVar[CurFile] >= NumVars[CurFile])
            {
              /* reset to the first active variable */
              CurVar[CurFile] = -1;
              AdvanceToNextVarWithValues();
            }
        }      

#if defined MAKEARCHIVE
      if (DebugLevel[CurFile] > 1)
        PRINT2("%d %G\n",NumDataValuesWritten[CurFile]+I+1,Value);
#endif
    }

  /*
   * If this is the last call to TECDAT110,
   * then we may have to set the 'repeat adjacency list'
   * flag in the file.
   */
  if ( HasFECONNECT[CurFile] &&

       /* (essentialy this is CheckData() but we don't want to print
          an error message) */
       (NumDataValuesToWrite[CurFile] == NumDataValuesWritten[CurFile]))
    {
      if (!WriteBinaryInt32(BlckFile[CurFile],(LgIndex_t)1))
        {
          WriteErr("TECDAT111");
          return (-1);
        }
    }

  /* re-write min/max values when all data has been delivered */
  if (NumDataValuesToWrite[CurFile] == NumDataValuesWritten[CurFile])
    RewritePendingMinMaxValues();

  return (0);
}
     
INTEGER4 LIBCALL TECDAT110(INTEGER4  *N,
                           void      *FieldData,
                           INTEGER4  *IsDouble)
{
  return TECDAT111(N,
                   FieldData,
                   IsDouble);
}

INTEGER4 LIBCALL TECDAT100(INTEGER4  *N,
                           void      *FieldData,
                           INTEGER4  *IsDouble)
{
  return TECDAT111(N,
                   FieldData,
                   IsDouble);
}

INTEGER4 LIBCALL TECDAT(INTEGER4  *N,
                        void      *FieldData,
                        INTEGER4  *IsDouble)
{
  return TECDAT111(N,
                   FieldData,
                   IsDouble);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL tecdat111_(INTEGER4  *N,
                                        void      *Data,
                                        INTEGER4  *IsDouble)
{
  return TECDAT111(N,Data,IsDouble);
}

LIBFUNCTION INTEGER4 LIBCALL tecdat110_(INTEGER4  *N,
                                        void      *Data,
                                        INTEGER4  *IsDouble)
{
  return TECDAT111(N,Data,IsDouble);
}

LIBFUNCTION INTEGER4 LIBCALL tecdat100_(INTEGER4  *N,
                                        void      *Data,
                                        INTEGER4  *IsDouble)
{
  return TECDAT111(N,Data,IsDouble);
}

LIBFUNCTION INTEGER4 LIBCALL tecdat_(INTEGER4  *N,
                        void      *FieldData,
                        INTEGER4  *IsDouble)
{
  return TECDAT111(N,
                   FieldData,
                   IsDouble);
}
#endif

/**
 * TECNODXXX
 */
INTEGER4 LIBCALL TECNOD111(INTEGER4 *NData)
{
  LgIndex_t L = NumConnectivityNodes[CurFile][CurZone[CurFile]];
  LgIndex_t I;

  ConnectivityWritten[CurFile][CurZone[CurFile]] = TRUE;

  if ( CheckFile("TECNOD111") < 0 )
    return (-1);

  if (ZoneType[CurFile] == FEPOLYGON ||
      ZoneType[CurFile] == FEPOLYHEDRON)
    {
      /* Wrong way to specify connectivity for polygons and polyhedrons */
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECNOD111) Cannot call TECNOD111 for polygonal or polyhedral zones.\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  if (HasFECONNECT[CurFile])
    {
      /*
       * The connectivity list is duplicated,
       * so we shouldn't be calling TECNOD111()
       */
      return (-1);
    }

  if (FileTypes[CurFile] == SOLUTIONFILE)
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECNOD111) Cannot call TECNOD111 if file type is SOLUTIONFILE.\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  if (ZoneType[CurFile] == ORDERED)
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECNOD111) Cannot call TECNOD110 if zone type is ORDERED.\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  if ( CheckData("TECNOD111") < 0 )
    return (-1);

  for ( I = 0; I < L; I++ )
    {
      if ((NData[I] > IMax[CurFile]) ||
          (NData[I] < 1))
        {
#ifdef MAKEARCHIVE
          PRINT1("Err: (TECNOD111) Invalid node map value at position %d:\n", I);
          PRINT2("     node map value = %d, max value = %d.\n", NData[I], IMax[CurFile]);
#endif
          NumErrs[CurFile]++;
          return (-1);
        }
      /*
       * As of version 103 Tecplot assumes that node maps are zero based
       * instead of ones based. Since we have to maintain the contract we
       * subtract 1 for the caller.
       */
      if ( !WriteBinaryInt32(BlckFile[CurFile],NData[I]-1) ) /* zero based */
        {
          WriteErr("TECNOD111");
          return (-1);
        }
    }
  return (0);
}

INTEGER4 LIBCALL TECNOD110(INTEGER4 *NData)
{
  return TECNOD111(NData);
}

INTEGER4 LIBCALL TECNOD100(INTEGER4 *NData)
{
  return TECNOD111(NData);
}

INTEGER4 LIBCALL TECNOD(INTEGER4 *NData)
{
  return TECNOD111(NData);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL tecnod111_(INTEGER4 *NData)
{
  return TECNOD111(NData);
}

LIBFUNCTION INTEGER4 LIBCALL tecnod110_(INTEGER4 *NData)
{
  return TECNOD111(NData);
}

LIBFUNCTION INTEGER4 LIBCALL tecnod100_(INTEGER4 *NData)
{
  return TECNOD111(NData);
}

LIBFUNCTION INTEGER4 LIBCALL tecnod_(INTEGER4 *NData)
{
  return TECNOD111(NData);
}
#endif

/**
 * TECENDXXX
 */
INTEGER4 LIBCALL TECEND111(void)
{
  short C;
  int RetVal = 0;

  /**
   * Validate that all zone data was given for the file since 
   * there are no more chances to give it. 
   */
  for ( int ZoneIndex = 0; (RetVal == 0) && (ZoneIndex <= CurZone[CurFile]); ZoneIndex++ )
    {
      if ( ((NumConnectivityNodes[CurFile][ZoneIndex] > 0) &&
            (ConnectivityWritten[CurFile][ZoneIndex] == FALSE)) )
        {
#if defined MAKEARCHIVE
          PRINT1("Err: (TECEND111) File %d is being closed without writing connectivity data.\n",CurFile+1);
          PRINT1("     Zone %d was defined with a Classic FE zone type but TECNOD111() was not called.\n",ZoneIndex+1);
#endif
          NumErrs[CurFile]++;
          RetVal = -1;
        }
      if ( ((NumFaceConnections[CurFile][ZoneIndex] > 0) && 
            (FaceNeighborsOrMapWritten[CurFile][ZoneIndex] == FALSE)) )           
        {
#if defined MAKEARCHIVE
          PRINT1("Err: (TECEND111) File %d is being closed without writing face neighbor data.\n",CurFile+1);
          PRINT2("     %d connections were specified for zone %d but TECFACE111() was not called.\n",
                 NumFaceConnections[CurFile][ZoneIndex], ZoneIndex+1);
#endif
          NumErrs[CurFile]++;
          RetVal = -1;
        }
      else if ( ((TotalNumFaceNodes[CurFile][ZoneIndex] > 0) &&
                 (FaceNeighborsOrMapWritten[CurFile][ZoneIndex] == FALSE)) )
        {
#if defined MAKEARCHIVE
          PRINT1("Err: (TECEND111) File %d is being closed without writing face map data.\n",CurFile+1);
          PRINT2("     %d face nodes were specified for zone %d but TECPOLY111() was not called.\n",
                 TotalNumFaceNodes[CurFile][ZoneIndex], ZoneIndex+1);
#endif
          NumErrs[CurFile]++;
          RetVal = -1;
        }
    }

  if ( RetVal == 0 )
    {
      if ( CheckFile("TECEND111") < 0 )
        RetVal = -1;
    }

  if ( RetVal == 0 )
    {
      if ( CheckData("TECEND111") < 0 )
        RetVal = -1;
    }

  if ( RetVal == 0 )
    if ( !WriteBinaryReal(HeadFile[CurFile],EndHeaderMarker,FieldDataType_Float) )
      {
        WriteErr("TECEND111");
        RetVal = -1;
      }

  CloseFileStream(&BlckFile[CurFile]);

  if (RetVal == 0)
    {
      BlckFile[CurFile] = OpenFileStream(BlckFName[CurFile],"rb",IsWritingNative[CurFile]);

      while ((RetVal == 0) && 
             ((C = getc(BlckFile[CurFile]->File)) != EOF))
        {
          if (fputc(C,HeadFile[CurFile]->File) == EOF)
            {
              /* do not call WriteErr, use custom message instead */
#if defined MAKEARCHIVE
              PRINT1("Err: (TECEND111) Write failure during repack on file %d.\n",CurFile+1);
#endif
              NumErrs[CurFile]++;
              RetVal = -1;
            }
        }
      CloseFileStream(&BlckFile[CurFile]);
    }

  unlink(BlckFName[CurFile]);

  CloseFileStream(&HeadFile[CurFile]);

#if defined MAKEARCHIVE
  if (DebugLevel[CurFile])
    {
      PRINT1("File %d closed.\n",CurFile+1);
      if (NumErrs[CurFile])
        {
          PRINT0("********************************************\n"); 
          PRINT1("      %d Errors occurred on this file\n",NumErrs[CurFile]);
          PRINT0("********************************************\n"); 
        }
    }
#endif

  NumErrs[CurFile] = 0;
  IsOpen[CurFile] = 0;
  if (DestFName[CurFile])
    FREE_ARRAY(DestFName[CurFile],"data set fname");
  if (BlckFName[CurFile])
    FREE_ARRAY(BlckFName[CurFile],"data set fname");
  BlckFName[CurFile] = NULL;
  DestFName[CurFile] = NULL;
  CurFile = 0;
  while ((CurFile < MaxNumFiles) && !IsOpen[CurFile])
    CurFile++;

  if (CurFile == MaxNumFiles)
    CurFile = -1;

  return RetVal;
}

INTEGER4 LIBCALL TECEND110(void)
{
  return TECEND111();
}

INTEGER4 LIBCALL TECEND100(void)
{
  return TECEND111();
}

INTEGER4 LIBCALL TECEND(void)
{
  return TECEND111();
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL tecend111_(void)
{
  return TECEND111();
}

LIBFUNCTION INTEGER4 LIBCALL tecend110_(void)
{
  return TECEND111();
}

LIBFUNCTION INTEGER4 LIBCALL tecend100_(void)
{
  return TECEND111();
}

LIBFUNCTION INTEGER4 LIBCALL tecend_(void)
{
  return TECEND111();
}
#endif




static void GetNextLabel(const char **CPtr,
                         char        *NextLabel)
{
  int N = 0;
  char *NPtr = NextLabel;
  *NPtr = '\0';
  /* Find label start */
  while ((**CPtr) && (**CPtr != '"'))
    (*CPtr)++;
  if (**CPtr)
    (*CPtr)++;
  while ((N < 60) && (**CPtr) && (**CPtr != '"'))
    {
      if (**CPtr == '\\')
        {
          (*CPtr)++;
        }
      *NPtr++ = **CPtr;
      N++;
      (*CPtr)++;
    }
  if (**CPtr)
    (*CPtr)++;
  *NPtr = '\0';
}


/**
 * TECLABXXX
 */
INTEGER4 LIBCALL TECLAB111(char *S)
{
  const char *CPtr = (const char *)S;
  LgIndex_t   N = 0;
  char        Label[60];

  if ( CheckFile("TECLAB111") < 0 )
    return (-1);

#ifdef MAKEARCHIVE
  if (DebugLevel[CurFile])
    PRINT0("\nInserting Custom Labels:\n");
#endif

  do
    {
      GetNextLabel(&CPtr,Label);
      if (*Label)
        N++;
    }
  while (*Label);

  if ( N == 0 )
    {
#ifdef MAKEARCHIVE
      PRINT1("Err: (TECLAB111) Invalid custom label string: %s\n",
             (S ? S : " "));
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  WriteBinaryReal(HeadFile[CurFile],CustomLabelMarker,FieldDataType_Float);
  if ( !WriteBinaryInt32(HeadFile[CurFile],(LgIndex_t)N) )
    {
      WriteErr("TECLAB111");
      return (-1);
    }

  CPtr = (const char *)S;
  do
    {
      GetNextLabel(&CPtr,Label);
      if (*Label)
        {
          if ( !DumpDatafileString(HeadFile[CurFile],Label,TRUE) )
            {
              WriteErr("TECLAB111");
              return (-1);
            }
#ifdef MAKEARCHIVE
          if ( DebugLevel[CurFile] )
            printf("          %s\n",Label);
#endif
        }
    } while (*Label);

  return (0);
}

INTEGER4 LIBCALL TECLAB110(char *S)
{
  return TECLAB111(S);
}

INTEGER4 LIBCALL TECLAB100(char *S)
{
  return TECLAB111(S);
}

INTEGER4 LIBCALL TECLAB(char *S)
{
  return TECLAB111(S);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL teclab111_(char *S)
{
  return TECLAB111(S);
}

LIBFUNCTION INTEGER4 LIBCALL teclab110_(char *S)
{
  return TECLAB111(S);
}

LIBFUNCTION INTEGER4 LIBCALL teclab100_(char *S)
{
  return TECLAB111(S);
}

LIBFUNCTION INTEGER4 LIBCALL teclab_(char *S)
{
  return TECLAB111(S);
}
#endif


/**
 * TECUSRXXX
 */
INTEGER4 LIBCALL TECUSR111(char *S)
{
  if ( CheckFile("TECUSR111") < 0 )
    return (-1);

#ifdef MAKEARCHIVE
  if (DebugLevel[CurFile])
    PRINT1("\nInserting UserRec: %s\n",S);
#endif

  if ((S == NULL) || (*S == '\0'))
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECUSR111) Invalid TECUSR110 string\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  WriteBinaryReal(HeadFile[CurFile], UserRecMarker, FieldDataType_Float);
  if ( !DumpDatafileString(HeadFile[CurFile], S, TRUE) )
    {
#if defined MAKEARCHIVE
      if ( DebugLevel[CurFile] )
        printf("Err: (TECUSR111) Write failure for file %d\n", CurFile+1);
#endif
      NumErrs[CurFile]++;
      return (-1);
    }
  return (0);
}

INTEGER4 LIBCALL TECUSR110(char *S)
{
  return TECUSR111(S);
}

INTEGER4 LIBCALL TECUSR100(char *S)
{
  return TECUSR111(S);
}

INTEGER4 LIBCALL TECUSR(char *S)
{
  return TECUSR111(S);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL tecusr111_(char *S)
{
  return TECUSR111(S);
}

LIBFUNCTION INTEGER4 LIBCALL tecusr110_(char *S)
{
  return TECUSR111(S);
}

LIBFUNCTION INTEGER4 LIBCALL tecusr100_(char *S)
{
  return TECUSR111(S);
}

LIBFUNCTION INTEGER4 LIBCALL tecusr_(char *S)
{
  return TECUSR111(S);
}
#endif

#if defined NOT_CURRENTLY_USED
static int WriteGeomDataBlock(float    *Data,
                              LgIndex_t NumPts)
{
  LgIndex_t I;

  for (I = 0; I < NumPts; I++)
    {
      if ( !WriteBinaryReal(HeadFile[CurFile],Data[I],FieldDataType_Float) )
        {
          return (-1);
        }
    }
  return (0);
}


static void ShowDebugColor(LgIndex_t Color)
{
#ifdef MAKEARCHIVE
  switch (Color)
    {
      case 0 : PRINT0("BLACK\n"); break;
      case 1 : PRINT0("RED\n"); break;
      case 2 : PRINT0("GREEN\n"); break;
      case 3 : PRINT0("BLUE\n"); break;
      case 4 : PRINT0("CYAN\n"); break;
      case 5 : PRINT0("YELLOW\n"); break;
      case 6 : PRINT0("PURPLE\n"); break;
      case 7 : PRINT0("WHITE\n"); break;
      case 8 : 
      case 9 : 
      case 10: 
      case 11: 
      case 12: 
      case 13: 
      case 14: 
      case 15: PRINT1("CUSTOM%1d\n",Color-7); break;
      default : PRINT0("INVALID\n");
    }
#endif
}
#endif /* NOT_CURRENTLY_USED */


/**
 * TECGEOXXX
 */
INTEGER4 LIBCALL TECGEO111(double    *XOrThetaPos,
                           double    *YOrRPos,
                           double    *ZPos,
                           INTEGER4  *PosCoordMode, /* 0=Grid, 1=Frame, 3=Grid3D */
                           INTEGER4  *AttachToZone,
                           INTEGER4  *Zone,
                           INTEGER4  *Color,
                           INTEGER4  *FillColor,
                           INTEGER4  *IsFilled,
                           INTEGER4  *GeomType,
                           INTEGER4  *LinePattern,
                           double    *PatternLength,
                           double    *LineThickness,
                           INTEGER4  *NumEllipsePts,
                           INTEGER4  *ArrowheadStyle,
                           INTEGER4  *ArrowheadAttachment,
                           double    *ArrowheadSize,
                           double    *ArrowheadAngle,
                           INTEGER4  *Scope,
                           INTEGER4  *Clipping,
                           INTEGER4  *NumSegments,
                           INTEGER4  *NumSegPts,
                           float     *XOrThetaGeomData,
                           float     *YOrRGeomData,
                           float     *ZGeomData,
                           char      *mfc)
{
  int    I, RetVal; 
  int    RawDataSize = 0;
  double Fract;

  Geom_s Geom;

  if ( CheckFile("TECGEO111") < 0 )
    return (-1);

  Geom.PositionCoordSys = (CoordSys_e)*PosCoordMode;
  if ( Geom.PositionCoordSys == CoordSys_Frame )
    Fract = 0.01;
  else
    Fract = 1.0;

  Geom.AnchorPos.Generic.V1 = (*XOrThetaPos)*Fract;
  Geom.AnchorPos.Generic.V2 = (*YOrRPos)*Fract;
  Geom.AnchorPos.Generic.V3 = (*ZPos)*Fract;
  Geom.AttachToZone         = *AttachToZone != 0;
  Geom.Zone                 = *Zone - 1;
  Geom.BColor               = (ColorIndex_t)*Color;
  Geom.FillBColor           = (ColorIndex_t)*FillColor;
  Geom.IsFilled             = *IsFilled;
  Geom.GeomType             = (GeomType_e)*GeomType;
  Geom.LinePattern          = (LinePattern_e)*LinePattern;
  Geom.PatternLength        = *PatternLength/100.0;
  Geom.LineThickness        = *LineThickness/100.0;
  Geom.NumEllipsePts        = *NumEllipsePts;
  Geom.ArrowheadStyle       = (ArrowheadStyle_e)*ArrowheadStyle;
  Geom.ArrowheadAttachment  = (ArrowheadAttachment_e)*ArrowheadAttachment;
  Geom.ArrowheadSize        = *ArrowheadSize/100.0;
  Geom.ArrowheadAngle       = *ArrowheadAngle/DEGPERRADIANS;
  Geom.Scope                = (Scope_e)*Scope;
  Geom.DrawOrder            = DrawOrder_AfterData;
  Geom.Clipping             = (Clipping_e)*Clipping;
  Geom.NumSegments          = *NumSegments;
  Geom.MacroFunctionCommand = mfc;
  Geom.ImageFileName        = NULL;
  Geom.ImageNumber          = 0;
  Geom.MaintainAspectRatio  = TRUE;
  Geom.PixelAspectRatio     = 1.0;
  Geom.ImageResizeFilter    = ImageResizeFilter_Texture;

  if (Geom.GeomType == GeomType_LineSegs3D)
    {
      Geom.GeomType         = GeomType_LineSegs;
      Geom.PositionCoordSys = CoordSys_Grid3D;
    }

#ifdef MAKEARCHIVE
  if (DebugLevel[CurFile])
    PRINT0("\nInserting Geometry\n");
#endif

  switch ( Geom.GeomType )
    {
      case GeomType_LineSegs :
        {
          int I;
          RawDataSize = 0;
          for (I = 0; I < *NumSegments; I++)
            {
              Geom.NumSegPts[I] = NumSegPts[I];
              RawDataSize += NumSegPts[I];
            }
        } break;
      case GeomType_Rectangle :
      case GeomType_Square :
      case GeomType_Circle :
      case GeomType_Ellipse :
        {
          RawDataSize = 1;
        } break;
      case GeomType_Image :
        {
          CHECK(FALSE); /* Images not allowed in data files. */
        } break;
      default : 
        {
          CHECK(FALSE);
        } break;
    }

  Geom.DataType                = FieldDataType_Float;
  Geom.GeomData.Generic.V1Base = AllocScratchNodalFieldDataPtr(RawDataSize,FieldDataType_Float, TRUE);
  Geom.GeomData.Generic.V2Base = AllocScratchNodalFieldDataPtr(RawDataSize,FieldDataType_Float, TRUE);
  Geom.GeomData.Generic.V3Base = AllocScratchNodalFieldDataPtr(RawDataSize,FieldDataType_Float, TRUE);

  for (I = 0; I < RawDataSize; I++)
    {
      SetFieldValue(Geom.GeomData.Generic.V1Base,I,(double)XOrThetaGeomData[I]*Fract);
      SetFieldValue(Geom.GeomData.Generic.V2Base,I,(double)YOrRGeomData[I]*Fract);
      SetFieldValue(Geom.GeomData.Generic.V3Base,I,(double)ZGeomData[I]*Fract);
    }

  if ( DumpGeometry(HeadFile[CurFile],&Geom,TRUE,FALSE) )
    RetVal = 0;
  else
    RetVal = -1;

  DeallocScratchNodalFieldDataPtr(&Geom.GeomData.Generic.V1Base);
  DeallocScratchNodalFieldDataPtr(&Geom.GeomData.Generic.V2Base);
  DeallocScratchNodalFieldDataPtr(&Geom.GeomData.Generic.V3Base);

  return RetVal;
}

INTEGER4 LIBCALL TECGEO110(double    *XOrThetaPos,
                           double    *YOrRPos,
                           double    *ZPos,
                           INTEGER4  *PosCoordMode, /* 0=Grid, 1=Frame, 3=Grid3D */
                           INTEGER4  *AttachToZone,
                           INTEGER4  *Zone,
                           INTEGER4  *Color,
                           INTEGER4  *FillColor,
                           INTEGER4  *IsFilled,
                           INTEGER4  *GeomType,
                           INTEGER4  *LinePattern,
                           double    *PatternLength,
                           double    *LineThickness,
                           INTEGER4  *NumEllipsePts,
                           INTEGER4  *ArrowheadStyle,
                           INTEGER4  *ArrowheadAttachment,
                           double    *ArrowheadSize,
                           double    *ArrowheadAngle,
                           INTEGER4  *Scope,
                           INTEGER4  *Clipping,
                           INTEGER4  *NumSegments,
                           INTEGER4  *NumSegPts,
                           float     *XOrThetaGeomData,
                           float     *YOrRGeomData,
                           float     *ZGeomData,
                           char      *mfc)
{
  return TECGEO111(XOrThetaPos,
                   YOrRPos,
                   ZPos,
                   PosCoordMode,
                   AttachToZone,
                   Zone,
                   Color,
                   FillColor,
                   IsFilled,
                   GeomType,
                   LinePattern,
                   PatternLength,
                   LineThickness,
                   NumEllipsePts,
                   ArrowheadStyle,
                   ArrowheadAttachment,
                   ArrowheadSize,
                   ArrowheadAngle,
                   Scope,
                   Clipping,
                   NumSegments,
                   NumSegPts,
                   XOrThetaGeomData,
                   YOrRGeomData,
                   ZGeomData,
                   mfc);
}

INTEGER4 LIBCALL TECGEO100(double    *XOrThetaPos,
                           double    *YOrRPos,
                           double    *ZPos,
                           INTEGER4  *PosCoordMode, /* 0=Grid, 1=Frame, 3=Grid3D */
                           INTEGER4  *AttachToZone,
                           INTEGER4  *Zone,
                           INTEGER4  *Color,
                           INTEGER4  *FillColor,
                           INTEGER4  *IsFilled,
                           INTEGER4  *GeomType,
                           INTEGER4  *LinePattern,
                           double    *PatternLength,
                           double    *LineThickness,
                           INTEGER4  *NumEllipsePts,
                           INTEGER4  *ArrowheadStyle,
                           INTEGER4  *ArrowheadAttachment,
                           double    *ArrowheadSize,
                           double    *ArrowheadAngle,
                           INTEGER4  *Scope,
                           INTEGER4  *Clipping,
                           INTEGER4  *NumSegments,
                           INTEGER4  *NumSegPts,
                           float     *XOrThetaGeomData,
                           float     *YOrRGeomData,
                           float     *ZGeomData,
                           char      *mfc)
{
  return TECGEO111(XOrThetaPos,
                   YOrRPos,
                   ZPos,
                   PosCoordMode,
                   AttachToZone,
                   Zone,
                   Color,
                   FillColor,
                   IsFilled,
                   GeomType,
                   LinePattern,
                   PatternLength,
                   LineThickness,
                   NumEllipsePts,
                   ArrowheadStyle,
                   ArrowheadAttachment,
                   ArrowheadSize,
                   ArrowheadAngle,
                   Scope,
                   Clipping,
                   NumSegments,
                   NumSegPts,
                   XOrThetaGeomData,
                   YOrRGeomData,
                   ZGeomData,
                   mfc);
}

INTEGER4 LIBCALL TECGEO(double    *XPos,
                        double    *YPos,
                        double    *ZPos,
                        INTEGER4  *PosCoordMode,
                        INTEGER4  *AttachToZone,
                        INTEGER4  *Zone,
                        INTEGER4  *Color,
                        INTEGER4  *FillColor,
                        INTEGER4  *IsFilled,
                        INTEGER4  *GeomType,
                        INTEGER4  *LinePattern,
                        double    *PatternLength,
                        double    *LineThickness,
                        INTEGER4  *NumEllipsePts,
                        INTEGER4  *ArrowheadStyle,
                        INTEGER4  *ArrowheadAttachment,
                        double    *ArrowheadSize,
                        double    *ArrowheadAngle,
                        INTEGER4  *Scope,
                        INTEGER4  *NumSegments,
                        INTEGER4  *NumSegPts,
                        float     *XGeomData,
                        float     *YGeomData,
                        float     *ZGeomData,
                        char      *mfc)
{
  int Clipping = (int)Clipping_ClipToViewport;
  return TECGEO111(XPos,
                   YPos,
                   ZPos,
                   PosCoordMode,
                   AttachToZone,
                   Zone,
                   Color,
                   FillColor,
                   IsFilled,
                   GeomType,
                   LinePattern,
                   PatternLength,
                   LineThickness,
                   NumEllipsePts,
                   ArrowheadStyle,
                   ArrowheadAttachment,
                   ArrowheadSize,
                   ArrowheadAngle,
                   Scope,
                   &Clipping,
                   NumSegments,
                   NumSegPts,
                   XGeomData,
                   YGeomData,
                   ZGeomData,
                   mfc);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL tecgeo111_(double    *XPos,
                                        double    *YPos,
                                        double    *ZPos,
                                        INTEGER4  *PosCoordMode,
                                        INTEGER4  *AttachToZone,
                                        INTEGER4  *Zone,
                                        INTEGER4  *Color,
                                        INTEGER4  *FillColor,
                                        INTEGER4  *IsFilled,
                                        INTEGER4  *GeomType,
                                        INTEGER4  *LinePattern,
                                        double    *PatternLength,
                                        double    *LineThickness,
                                        INTEGER4  *NumEllipsePts,
                                        INTEGER4  *ArrowheadStyle,
                                        INTEGER4  *ArrowheadAttachment,
                                        double    *ArrowheadSize,
                                        double    *ArrowheadAngle,
                                        INTEGER4  *Scope,
                                        INTEGER4  *Clipping,
                                        INTEGER4  *NumSegments,
                                        INTEGER4  *NumSegPts,
                                        float     *XGeomData,
                                        float     *YGeomData,
                                        float     *ZGeomData,
                                        char      *mfc)
{
  return TECGEO111(XPos,
                   YPos,
                   ZPos,
                   PosCoordMode,
                   AttachToZone,
                   Zone,
                   Color,
                   FillColor,
                   IsFilled,
                   GeomType,
                   LinePattern,
                   PatternLength,
                   LineThickness,
                   NumEllipsePts,
                   ArrowheadStyle,
                   ArrowheadAttachment,
                   ArrowheadSize,
                   ArrowheadAngle,
                   Scope,
                   Clipping,
                   NumSegments,
                   NumSegPts,
                   XGeomData,
                   YGeomData,
                   ZGeomData,
                   mfc);
}

LIBFUNCTION INTEGER4 LIBCALL tecgeo110_(double    *XPos,
                                        double    *YPos,
                                        double    *ZPos,
                                        INTEGER4  *PosCoordMode,
                                        INTEGER4  *AttachToZone,
                                        INTEGER4  *Zone,
                                        INTEGER4  *Color,
                                        INTEGER4  *FillColor,
                                        INTEGER4  *IsFilled,
                                        INTEGER4  *GeomType,
                                        INTEGER4  *LinePattern,
                                        double    *PatternLength,
                                        double    *LineThickness,
                                        INTEGER4  *NumEllipsePts,
                                        INTEGER4  *ArrowheadStyle,
                                        INTEGER4  *ArrowheadAttachment,
                                        double    *ArrowheadSize,
                                        double    *ArrowheadAngle,
                                        INTEGER4  *Scope,
                                        INTEGER4  *Clipping,
                                        INTEGER4  *NumSegments,
                                        INTEGER4  *NumSegPts,
                                        float     *XGeomData,
                                        float     *YGeomData,
                                        float     *ZGeomData,
                                        char      *mfc)
{
  return TECGEO111(XPos,
                   YPos,
                   ZPos,
                   PosCoordMode,
                   AttachToZone,
                   Zone,
                   Color,
                   FillColor,
                   IsFilled,
                   GeomType,
                   LinePattern,
                   PatternLength,
                   LineThickness,
                   NumEllipsePts,
                   ArrowheadStyle,
                   ArrowheadAttachment,
                   ArrowheadSize,
                   ArrowheadAngle,
                   Scope,
                   Clipping,
                   NumSegments,
                   NumSegPts,
                   XGeomData,
                   YGeomData,
                   ZGeomData,
                   mfc);
}

LIBFUNCTION INTEGER4 LIBCALL tecgeo100_(double    *XPos,
                                        double    *YPos,
                                        double    *ZPos,
                                        INTEGER4  *PosCoordMode,
                                        INTEGER4  *AttachToZone,
                                        INTEGER4  *Zone,
                                        INTEGER4  *Color,
                                        INTEGER4  *FillColor,
                                        INTEGER4  *IsFilled,
                                        INTEGER4  *GeomType,
                                        INTEGER4  *LinePattern,
                                        double    *PatternLength,
                                        double    *LineThickness,
                                        INTEGER4  *NumEllipsePts,
                                        INTEGER4  *ArrowheadStyle,
                                        INTEGER4  *ArrowheadAttachment,
                                        double    *ArrowheadSize,
                                        double    *ArrowheadAngle,
                                        INTEGER4  *Scope,
                                        INTEGER4  *Clipping,
                                        INTEGER4  *NumSegments,
                                        INTEGER4  *NumSegPts,
                                        float     *XGeomData,
                                        float     *YGeomData,
                                        float     *ZGeomData,
                                        char      *mfc)
{
  return TECGEO111(XPos,
                   YPos,
                   ZPos,
                   PosCoordMode,
                   AttachToZone,
                   Zone,
                   Color,
                   FillColor,
                   IsFilled,
                   GeomType,
                   LinePattern,
                   PatternLength,
                   LineThickness,
                   NumEllipsePts,
                   ArrowheadStyle,
                   ArrowheadAttachment,
                   ArrowheadSize,
                   ArrowheadAngle,
                   Scope,
                   Clipping,
                   NumSegments,
                   NumSegPts,
                   XGeomData,
                   YGeomData,
                   ZGeomData,
                   mfc);
}

LIBFUNCTION INTEGER4 LIBCALL tecgeo_(double    *XPos,
                                     double    *YPos,
                                     double    *ZPos,
                                     INTEGER4  *PosCoordMode,
                                     INTEGER4  *AttachToZone,
                                     INTEGER4  *Zone,
                                     INTEGER4  *Color,
                                     INTEGER4  *FillColor,
                                     INTEGER4  *IsFilled,
                                     INTEGER4  *GeomType,
                                     INTEGER4  *LinePattern,
                                     double    *PatternLength,
                                     double    *LineThickness,
                                     INTEGER4  *NumEllipsePts,
                                     INTEGER4  *ArrowheadStyle,
                                     INTEGER4  *ArrowheadAttachment,
                                     double    *ArrowheadSize,
                                     double    *ArrowheadAngle,
                                     INTEGER4  *Scope,
                                     INTEGER4  *NumSegments,
                                     INTEGER4  *NumSegPts,
                                     float     *XGeomData,
                                     float     *YGeomData,
                                     float     *ZGeomData,
                                     char      *mfc)
{
  return TECGEO(XPos,
                YPos,
                ZPos,
                PosCoordMode,
                AttachToZone,
                Zone,
                Color,
                FillColor,
                IsFilled,
                GeomType,
                LinePattern,
                PatternLength,
                LineThickness,
                NumEllipsePts,
                ArrowheadStyle,
                ArrowheadAttachment,
                ArrowheadSize,
                ArrowheadAngle,
                Scope,
                NumSegments,
                NumSegPts,
                XGeomData,
                YGeomData,
                ZGeomData,
                mfc);
}
#endif

/**
 * TECTXTXXX
 */
INTEGER4 LIBCALL TECTXT111(double    *XOrThetaPos,
                           double    *YOrRPos,
                           double    *ZOrUnusedPos,
                           INTEGER4  *PosCoordMode,
                           INTEGER4  *AttachToZone,
                           INTEGER4  *Zone,
                           INTEGER4  *BFont,
                           INTEGER4  *FontHeightUnits,
                           double    *FontHeight,
                           INTEGER4  *BoxType,
                           double    *BoxMargin,
                           double    *BoxLineThickness,
                           INTEGER4  *BoxColor,
                           INTEGER4  *BoxFillColor,
                           double    *Angle,
                           INTEGER4  *Anchor,
                           double    *LineSpacing,
                           INTEGER4  *TextColor,
                           INTEGER4  *Scope,
                           INTEGER4  *Clipping,
                           char      *String,
                           char      *mfc)
{
  int    RetVal; 
  Text_s Text;
  double Fract;
  if ( CheckFile("TECTXT111") < 0 )
    return (-1);

  Text.PositionCoordSys    = (CoordSys_e)*PosCoordMode;
  if (Text.PositionCoordSys == CoordSys_Frame)
    Fract = 0.01;
  else
    Fract = 1.0;

  Text.AnchorPos.Generic.V1 = (*XOrThetaPos)*Fract;
  Text.AnchorPos.Generic.V2 = (*YOrRPos)*Fract;
  Text.AnchorPos.Generic.V3 = (*ZOrUnusedPos)*Fract;
  Text.AttachToZone         = *AttachToZone != 0;
  Text.Zone                 = *Zone - 1;
  Text.BColor               = (ColorIndex_t)*TextColor;
  Text.TextShape.Font       = (Font_e)*BFont;
  Text.TextShape.SizeUnits  = (Units_e)*FontHeightUnits;
  if (Text.TextShape.SizeUnits == Units_Frame)
    Text.TextShape.Height   = (*FontHeight)/100.0;
  else
    Text.TextShape.Height   = *FontHeight;
  Text.Box.BoxType          = (TextBox_e)*BoxType;
  Text.Box.Margin           = *BoxMargin/100.0;
  Text.Box.LineThickness    = *BoxLineThickness/100.0;
  Text.Box.BColor           = (ColorIndex_t)*BoxColor;
  Text.Box.FillBColor       = (ColorIndex_t)*BoxFillColor;
  Text.Anchor               = (TextAnchor_e)*Anchor;
  Text.LineSpacing          = *LineSpacing;
  Text.Angle                = *Angle/DEGPERRADIANS;
  Text.Scope                = (Scope_e)*Scope;
  Text.Text                 = String;
  Text.MacroFunctionCommand = mfc;
  Text.Clipping             = (Clipping_e)*Clipping;

#ifdef MAKEARCHIVE
  if ( DebugLevel[CurFile] )
    PRINT1("\nInserting Text: %s\n",String);
#endif

  if ( DumpText(HeadFile[CurFile],&Text,TRUE,FALSE) )
    RetVal = 0;
  else
    RetVal = -1;

  return RetVal;
}

INTEGER4 LIBCALL TECTXT110(double    *XOrThetaPos,
                           double    *YOrRPos,
                           double    *ZOrUnusedPos,
                           INTEGER4  *PosCoordMode,
                           INTEGER4  *AttachToZone,
                           INTEGER4  *Zone,
                           INTEGER4  *BFont,
                           INTEGER4  *FontHeightUnits,
                           double    *FontHeight,
                           INTEGER4  *BoxType,
                           double    *BoxMargin,
                           double    *BoxLineThickness,
                           INTEGER4  *BoxColor,
                           INTEGER4  *BoxFillColor,
                           double    *Angle,
                           INTEGER4  *Anchor,
                           double    *LineSpacing,
                           INTEGER4  *TextColor,
                           INTEGER4  *Scope,
                           INTEGER4  *Clipping,
                           char      *String,
                           char      *mfc)
{
  return TECTXT111(XOrThetaPos,
                   YOrRPos,
                   ZOrUnusedPos,
                   PosCoordMode,
                   AttachToZone,
                   Zone,
                   BFont,
                   FontHeightUnits,
                   FontHeight,
                   BoxType,
                   BoxMargin,
                   BoxLineThickness,
                   BoxColor,
                   BoxFillColor,
                   Angle,
                   Anchor,
                   LineSpacing,
                   TextColor,
                   Scope,
                   Clipping,
                   String,
                   mfc);
}

INTEGER4 LIBCALL TECTXT100(double    *XOrThetaPos,
                           double    *YOrRPos,
                           double    *ZOrUnusedPos,
                           INTEGER4  *PosCoordMode,
                           INTEGER4  *AttachToZone,
                           INTEGER4  *Zone,
                           INTEGER4  *BFont,
                           INTEGER4  *FontHeightUnits,
                           double    *FontHeight,
                           INTEGER4  *BoxType,
                           double    *BoxMargin,
                           double    *BoxLineThickness,
                           INTEGER4  *BoxColor,
                           INTEGER4  *BoxFillColor,
                           double    *Angle,
                           INTEGER4  *Anchor,
                           double    *LineSpacing,
                           INTEGER4  *TextColor,
                           INTEGER4  *Scope,
                           INTEGER4  *Clipping,
                           char      *String,
                           char      *mfc)
{
  return TECTXT111(XOrThetaPos,
                   YOrRPos,
                   ZOrUnusedPos,
                   PosCoordMode,
                   AttachToZone,
                   Zone,
                   BFont,
                   FontHeightUnits,
                   FontHeight,
                   BoxType,
                   BoxMargin,
                   BoxLineThickness,
                   BoxColor,
                   BoxFillColor,
                   Angle,
                   Anchor,
                   LineSpacing,
                   TextColor,
                   Scope,
                   Clipping,
                   String,
                   mfc);
}

INTEGER4 LIBCALL TECTXT(double    *XPos,
                        double    *YPos,
                        INTEGER4  *PosCoordMode,
                        INTEGER4  *AttachToZone,
                        INTEGER4  *Zone,
                        INTEGER4  *BFont,
                        INTEGER4  *FontHeightUnits,
                        double    *FontHeight,
                        INTEGER4  *BoxType,
                        double    *BoxMargin,
                        double    *BoxLineThickness,
                        INTEGER4  *BoxColor,
                        INTEGER4  *BoxFillColor,
                        double    *Angle,
                        INTEGER4  *Anchor,
                        double    *LineSpacing,
                        INTEGER4  *TextColor,
                        INTEGER4  *Scope,
                        char      *Text,
                        char      *mfc)
{
  double    ZPos     = 0.0;
  int       Clipping = (int)Clipping_ClipToViewport;
  return TECTXT111(XPos,
                   YPos,
                   &ZPos,
                   PosCoordMode,
                   AttachToZone,
                   Zone,
                   BFont,
                   FontHeightUnits,
                   FontHeight,
                   BoxType,
                   BoxMargin,
                   BoxLineThickness,
                   BoxColor,
                   BoxFillColor,
                   Angle,
                   Anchor,
                   LineSpacing,
                   TextColor,
                   Scope,
                   &Clipping,
                   Text,
                   mfc);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL tectxt111_(double    *XOrThetaPos,
                                        double    *YOrRPos,
                                        double    *ZOrUnusedPos,
                                        INTEGER4  *PosCoordMode,
                                        INTEGER4  *AttachToZone,
                                        INTEGER4  *Zone,
                                        INTEGER4  *BFont,
                                        INTEGER4  *FontHeightUnits,
                                        double    *FontHeight,
                                        INTEGER4  *BoxType,
                                        double    *BoxMargin,
                                        double    *BoxLineThickness,
                                        INTEGER4  *BoxColor,
                                        INTEGER4  *BoxFillColor,
                                        double    *Angle,
                                        INTEGER4  *Anchor,
                                        double    *LineSpacing,
                                        INTEGER4  *TextColor,
                                        INTEGER4  *Scope,
                                        INTEGER4  *Clipping,
                                        char      *String,
                                        char      *mfc)
{
  return TECTXT111(XOrThetaPos,
                   YOrRPos,
                   ZOrUnusedPos,
                   PosCoordMode,
                   AttachToZone,
                   Zone,
                   BFont,
                   FontHeightUnits,
                   FontHeight,
                   BoxType,
                   BoxMargin,
                   BoxLineThickness,
                   BoxColor,
                   BoxFillColor,
                   Angle,
                   Anchor,
                   LineSpacing,
                   TextColor,
                   Scope,
                   Clipping,
                   String,
                   mfc);
}

LIBFUNCTION INTEGER4 LIBCALL tectxt110_(double    *XOrThetaPos,
                                        double    *YOrRPos,
                                        double    *ZOrUnusedPos,
                                        INTEGER4  *PosCoordMode,
                                        INTEGER4  *AttachToZone,
                                        INTEGER4  *Zone,
                                        INTEGER4  *BFont,
                                        INTEGER4  *FontHeightUnits,
                                        double    *FontHeight,
                                        INTEGER4  *BoxType,
                                        double    *BoxMargin,
                                        double    *BoxLineThickness,
                                        INTEGER4  *BoxColor,
                                        INTEGER4  *BoxFillColor,
                                        double    *Angle,
                                        INTEGER4  *Anchor,
                                        double    *LineSpacing,
                                        INTEGER4  *TextColor,
                                        INTEGER4  *Scope,
                                        INTEGER4  *Clipping,
                                        char      *String,
                                        char      *mfc)
{
  return TECTXT111(XOrThetaPos,
                   YOrRPos,
                   ZOrUnusedPos,
                   PosCoordMode,
                   AttachToZone,
                   Zone,
                   BFont,
                   FontHeightUnits,
                   FontHeight,
                   BoxType,
                   BoxMargin,
                   BoxLineThickness,
                   BoxColor,
                   BoxFillColor,
                   Angle,
                   Anchor,
                   LineSpacing,
                   TextColor,
                   Scope,
                   Clipping,
                   String,
                   mfc);
}

LIBFUNCTION INTEGER4 LIBCALL tectxt100_(double    *XOrThetaPos,
                                        double    *YOrRPos,
                                        double    *ZOrUnusedPos,
                                        INTEGER4  *PosCoordMode,
                                        INTEGER4  *AttachToZone,
                                        INTEGER4  *Zone,
                                        INTEGER4  *BFont,
                                        INTEGER4  *FontHeightUnits,
                                        double    *FontHeight,
                                        INTEGER4  *BoxType,
                                        double    *BoxMargin,
                                        double    *BoxLineThickness,
                                        INTEGER4  *BoxColor,
                                        INTEGER4  *BoxFillColor,
                                        double    *Angle,
                                        INTEGER4  *Anchor,
                                        double    *LineSpacing,
                                        INTEGER4  *TextColor,
                                        INTEGER4  *Scope,
                                        INTEGER4  *Clipping,
                                        char      *String,
                                        char      *mfc)
{
  return TECTXT111(XOrThetaPos,
                   YOrRPos,
                   ZOrUnusedPos,
                   PosCoordMode,
                   AttachToZone,
                   Zone,
                   BFont,
                   FontHeightUnits,
                   FontHeight,
                   BoxType,
                   BoxMargin,
                   BoxLineThickness,
                   BoxColor,
                   BoxFillColor,
                   Angle,
                   Anchor,
                   LineSpacing,
                   TextColor,
                   Scope,
                   Clipping,
                   String,
                   mfc);
}

LIBFUNCTION INTEGER4 LIBCALL tectxt_(double    *XPos,
                                     double    *YPos,
                                     INTEGER4  *PosCoordMode,
                                     INTEGER4  *AttachToZone,
                                     INTEGER4  *Zone,
                                     INTEGER4  *BFont,
                                     INTEGER4  *FontHeightUnits,
                                     double    *FontHeight,
                                     INTEGER4  *BoxType,
                                     double    *BoxMargin,
                                     double    *BoxLineThickness,
                                     INTEGER4  *BoxColor,
                                     INTEGER4  *BoxFillColor,
                                     double    *Angle,
                                     INTEGER4  *Anchor,
                                     double    *LineSpacing,
                                     INTEGER4  *TextColor,
                                     INTEGER4  *Scope,
                                     char      *Text,
                                     char      *mfc)
{
  return TECTXT(XPos,
                YPos,
                PosCoordMode,
                AttachToZone,
                Zone,
                BFont,
                FontHeightUnits,
                FontHeight,
                BoxType,
                BoxMargin,
                BoxLineThickness,
                BoxColor,
                BoxFillColor,
                Angle,
                Anchor,
                LineSpacing,
                TextColor,
                Scope,
                Text,
                mfc);
}
#endif


/**
 * TECFILXXX
 */
INTEGER4 LIBCALL TECFIL111(INTEGER4 *F)
{
  if ( (*F < 1) || (*F > MaxNumFiles) )
    {
#ifdef MAKEARCHIVE
      PRINT1("Err: (TECFIL111) Invalid file number requested (%d).  File not changed.\n",*F);
#endif
      return (-1);
    }

  if ( !IsOpen[*F-1] )
    {
#ifdef MAKEARCHIVE
      int I;
      PRINT1("Err: (TECFIL111) file %d is not open.  File not changed.\n", *F);
      PRINT0("\n\nFile states are:\n");
      for (I = 0; I < MaxNumFiles; I++)
        PRINT2("file %d, IsOpen=%d\n",I+1,IsOpen[I]);
      PRINT1("Current File is: %d\n",CurFile+1);
#endif
      return (-1);
    }
  CurFile = *F-1;
#ifdef MAKEARCHIVE
  if (DebugLevel[CurFile])
    {
      PRINT1("Switching to file #%d\n\n",CurFile+1);
      PRINT0("Current State is:\n");
      PRINT1("  Debug     = %d\n",DebugLevel[CurFile]);
      PRINT1("  NumVars   = %d\n",NumVars[CurFile]);
      PRINT1("  DestFName = %s\n",DestFName[CurFile]);
      PRINT1("  BlckFName = %s\n",BlckFName[CurFile]);
      PRINT1("  ZoneType = %s\n", ZoneTypes[ZoneType[CurFile]]);

      if (ZoneType[CurFile] == ORDERED)
        {
          PRINT1("  IMax      = %d\n",IMax[CurFile]);
          PRINT1("  JMax      = %d\n",JMax[CurFile]);
          PRINT1("  KMax      = %d\n",KMax[CurFile]);
        }
      else
        {
          PRINT1("  NumPoints = %d\n",IMax[CurFile]);
          PRINT1("  NumElmnts = %d\n",JMax[CurFile]);
        }
      PRINT1("  NumDataValuesWritten = %d\n",NumDataValuesWritten[CurFile]);
      PRINT1("  CurZone              = %d\n",CurZone[CurFile]+1);
    }
#endif /* MAKEARCHIVE */
  return (0);
}

INTEGER4 LIBCALL TECFIL110(INTEGER4 *F)
{
  return TECFIL111(F);
}

INTEGER4 LIBCALL TECFIL100(INTEGER4 *F)
{
  return TECFIL111(F);
}

INTEGER4 LIBCALL TECFIL(INTEGER4 *F)
{
  return TECFIL111(F);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL tecfil111_(INTEGER4 *F)
{
  return TECFIL111(F);
}

LIBFUNCTION INTEGER4 LIBCALL tecfil110_(INTEGER4 *F)
{
  return TECFIL111(F);
}

LIBFUNCTION INTEGER4 LIBCALL tecfil100_(INTEGER4 *F)
{
  return TECFIL111(F);
}

LIBFUNCTION INTEGER4 LIBCALL tecfil_(INTEGER4 *F)
{
  return TECFIL111(F);
}
#endif

/**
 * TECFOREIGNXXX
 */
void LIBCALL TECFOREIGN111(INTEGER4 *OutputForeignByteOrder)
{
  REQUIRE(VALID_REF(OutputForeignByteOrder));

  DoWriteForeign = (*OutputForeignByteOrder != 0);
}

void LIBCALL TECFOREIGN110(INTEGER4 *OutputForeignByteOrder)
{
  TECFOREIGN111(OutputForeignByteOrder);
}

void LIBCALL TECFOREIGN100(INTEGER4 *OutputForeignByteOrder)
{
  TECFOREIGN111(OutputForeignByteOrder);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION void LIBCALL tecforeign111_(INTEGER4 *OutputForeignByteOrder)
{
  TECFOREIGN111(OutputForeignByteOrder);
}

LIBFUNCTION void LIBCALL tecforeign110_(INTEGER4 *OutputForeignByteOrder)
{
  TECFOREIGN111(OutputForeignByteOrder);
}

LIBFUNCTION void LIBCALL tecforeign100_(INTEGER4 *OutputForeignByteOrder)
{
  TECFOREIGN111(OutputForeignByteOrder);
}
#endif

#if defined MAKEARCHIVE

/**
 * A valid auxiliary data name character must begin with a '_' or alpha
 * character and may be followed by one or more '_', '.', alpha or digit
 * characters.
 */
static Boolean_t AuxDataIsValidNameChar(char      Char,
                                        Boolean_t IsLeadChar)
{
  Boolean_t IsValidNameChar;

  REQUIRE(0 <= Char && "Char <= 127");
  REQUIRE(VALID_BOOLEAN(IsLeadChar));

  IsValidNameChar = (Char == '_' ||
                     isalpha(Char));
  if (!IsLeadChar)
    IsValidNameChar = (IsValidNameChar ||
                       Char == '.'     ||
                       isdigit(Char));

  ENSURE(VALID_BOOLEAN(IsValidNameChar));
  return IsValidNameChar;
}

/**
 * Indicates if the auxiliary data name is valid. A valid auxiliary data name
 * must begin with a '_' or alpha character and may be followed by one or
 * more '_', '.', alpha or digit characters.
 */
static Boolean_t AuxDataIsValidName(const char *Name)
{
  Boolean_t  IsValidName;
  const char *NPtr;
  REQUIRE(VALID_REF(Name));

  for (NPtr = Name, IsValidName = AuxDataIsValidNameChar(*NPtr,TRUE);
       IsValidName && *NPtr != '\0';
       NPtr++)
    {
      IsValidName = AuxDataIsValidNameChar(*NPtr,FALSE);
    }

  ENSURE(VALID_BOOLEAN(IsValidName));
  return IsValidName;
}

#endif /* MAKEARCHIVE */

/**
 * TECAUXSTRXXX
 */
LIBFUNCTION INTEGER4 LIBCALL TECAUXSTR111(char *Name,
                                          char *Value)
{
  if ( CheckFile("TECAUXSTR111") < 0 )
    return (-1);

#ifdef MAKEARCHIVE
  if (DebugLevel[CurFile])
    PRINT2("\nInserting data set aux data: '%s' = '%s'\n", Name, Value);
#endif

  if ((Name == NULL) || !AuxDataIsValidName(Name))
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECAUXSTR111) Invalid Name string\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  if ((Value == NULL) || (*Value == '\0'))
    {
#ifdef MAKEARCHIVE
      if (DebugLevel[CurFile])
        PRINT0("Err: (TECAUXSTR111) Invalid Value string\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  /*
   * Because the auxiliary data is at the end of the header section we don't
   * need to seek back to it.
   */
  if (!WriteBinaryReal(HeadFile[CurFile], DataSetAuxMarker, FieldDataType_Float)  ||
      !DumpDatafileString(HeadFile[CurFile], Name, TRUE /* WriteBinary */) ||
      !WriteBinaryInt32(HeadFile[CurFile], (LgIndex_t)AuxDataType_String) ||
      !DumpDatafileString(HeadFile[CurFile], (const char *)Value, TRUE /* WriteBinary */))
    {
#if defined MAKEARCHIVE
      if ( DebugLevel[CurFile] )
        printf("Err: (TECAUXSTR111) Write failure for file %d\n", CurFile+1);
#endif
      NumErrs[CurFile]++;
      return (-1);
    }
  return (0);
}

LIBFUNCTION INTEGER4 LIBCALL TECAUXSTR110(char *Name,
                                          char *Value)
{
  return TECAUXSTR111(Name, Value);
}

LIBFUNCTION INTEGER4 LIBCALL TECAUXSTR100(char *Name,
                                          char *Value)
{
  return TECAUXSTR111(Name, Value);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL tecauxstr111_(char *Name,
                                           char *Value)
{
  return TECAUXSTR111(Name, Value);
}

LIBFUNCTION INTEGER4 LIBCALL tecauxstr110_(char *Name,
                                           char *Value)
{
  return TECAUXSTR111(Name, Value);
}

LIBFUNCTION INTEGER4 LIBCALL tecauxstr100_(char *Name,
                                           char *Value)
{
  return TECAUXSTR111(Name, Value);
}
#endif


/**
 * TECZAUXSTRXXX
 */
LIBFUNCTION INTEGER4 LIBCALL TECZAUXSTR111(char *Name,
                                           char *Value)
{
  if ( CheckFile("TECZAUXSTR111") < 0 )
    return (-1);

  if ( CurZone[CurFile] == -1 )
    {
#ifdef MAKEARCHIVE
        PRINT0("Err: (TECZAUXSTR111) Must call TECZNE111 prior to TECZAUXSTR111\n");
#endif
        NumErrs[CurFile]++;
        return (-1);
    }


#ifdef MAKEARCHIVE
  if (DebugLevel[CurFile])
    PRINT2("\nInserting zone aux data: '%s' = '%s'\n", Name, Value);
#endif

  if ((Name == NULL) || !AuxDataIsValidName(Name))
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECZAUXSTR111) Invalid Name string\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  if ((Value == NULL) || (*Value == '\0'))
    {
#ifdef MAKEARCHIVE
      if (DebugLevel[CurFile])
        PRINT0("Err: (TECZAUXSTR111) Invalid Value string\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  /*
   * Have to back over the 0 already written, then write another one afterward.
   */
  if (FSEEK(HeadFile[CurFile]->File, -4, SEEK_CUR) ||
      !WriteBinaryInt32(HeadFile[CurFile], 1)  ||
      !DumpDatafileString(HeadFile[CurFile], Name, TRUE /* WriteBinary */) ||
      !WriteBinaryInt32(HeadFile[CurFile], (LgIndex_t)AuxDataType_String) ||
      !DumpDatafileString(HeadFile[CurFile], (const char *)Value, TRUE /* WriteBinary */) ||
      !WriteBinaryInt32(HeadFile[CurFile], 0))
    {
#if defined MAKEARCHIVE
      if ( DebugLevel[CurFile] )
        printf("Err: (TECZAUXSTR111) Write failure for file %d\n", CurFile+1);
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  return (0);
}

LIBFUNCTION INTEGER4 LIBCALL TECZAUXSTR110(char *Name,
                                           char *Value)
{
  return TECZAUXSTR111(Name, Value);
}

LIBFUNCTION INTEGER4 LIBCALL TECZAUXSTR100(char *Name,
                                           char *Value)
{
  return TECZAUXSTR111(Name, Value);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL teczauxstr111_(char *Name,
                                            char *Value)
{
  return TECZAUXSTR111(Name, Value);
}

LIBFUNCTION INTEGER4 LIBCALL teczauxstr110_(char *Name,
                                            char *Value)
{
  return TECZAUXSTR111(Name, Value);
}

LIBFUNCTION INTEGER4 LIBCALL teczauxstr100_(char *Name,
                                            char *Value)
{
  return TECZAUXSTR111(Name, Value);
}
#endif


/**
 * TECVAUXSTRXXX
 */
LIBFUNCTION INTEGER4 LIBCALL TECVAUXSTR111(INTEGER4 *Var,
                                           char     *Name,
                                           char     *Value)
{
  if ( CheckFile("TECVAUXSTR111") < 0 )
    return (-1);

#ifdef MAKEARCHIVE
  if (DebugLevel[CurFile])
    PRINT2("\nInserting variable aux data: '%s' = '%s'\n", Name, Value);
#endif

  if ((Name == NULL) || !AuxDataIsValidName(Name))
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECVAUXSTR111) Invalid Name string\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  if ((Value == NULL) || (*Value == '\0'))
    {
#ifdef MAKEARCHIVE
      if (DebugLevel[CurFile])
        PRINT0("Err: (TECVAUXSTR111) Invalid Value string\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  if (!WriteBinaryReal(HeadFile[CurFile], VarAuxMarker, FieldDataType_Float)  ||
      !WriteBinaryInt32(HeadFile[CurFile], *Var-1) ||
      !DumpDatafileString(HeadFile[CurFile], Name, TRUE /* WriteBinary */) ||
      !WriteBinaryInt32(HeadFile[CurFile], (LgIndex_t)AuxDataType_String) ||
      !DumpDatafileString(HeadFile[CurFile], (const char *)Value, TRUE /* WriteBinary */))
    {
#if defined MAKEARCHIVE
      if ( DebugLevel[CurFile] )
        printf("Err: (TECVAUXSTR111) Write failure for file %d\n", CurFile+1);
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  return (0);
}

LIBFUNCTION INTEGER4 LIBCALL TECVAUXSTR110(INTEGER4 *Var,
                                           char     *Name,
                                           char     *Value)
{
  return TECVAUXSTR111(Var, Name, Value);
}

LIBFUNCTION INTEGER4 LIBCALL TECVAUXSTR100(INTEGER4 *Var,
                                           char     *Name,
                                           char     *Value)
{
  return TECVAUXSTR111(Var, Name, Value);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL tecvauxstr111_(INTEGER4 *Var,
                                            char     *Name,
                                            char     *Value)
{
  return TECVAUXSTR111(Var, Name, Value);
}

LIBFUNCTION INTEGER4 LIBCALL tecvauxstr110_(INTEGER4 *Var,
                                            char     *Name,
                                            char     *Value)
{
  return TECVAUXSTR111(Var, Name, Value);
}

LIBFUNCTION INTEGER4 LIBCALL tecvauxstr100_(INTEGER4 *Var,
                                            char     *Name,
                                            char     *Value)
{
  return TECVAUXSTR111(Var, Name, Value);
}
#endif


/**
 * TECFACEXXX
 */
LIBFUNCTION INTEGER4 LIBCALL TECFACE111(INTEGER4 *FaceConnections)
{
  INTEGER4 i, *Ptr;

  /* Mark that the face neighbors have been written for the zone even if it fails so as not to add extra error messages. */
  FaceNeighborsOrMapWritten[CurFile][CurZone[CurFile]] = TRUE;

  if ( CheckFile("TECFACE111") < 0 )
    return (-1);

  if (ZoneType[CurFile] == FEPOLYGON ||
      ZoneType[CurFile] == FEPOLYHEDRON)
    {
      /* Wrong way to specify face neighbors for polygons and polyhedrons */
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECFACE111) Cannot call TECFACE111 for polygonal or polyhedral zones.\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  if (FileTypes[CurFile] == SOLUTIONFILE)
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECFACE111) Cannot call TECFACE111 if the file type is SOLUTIONFILE.\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

#ifdef MAKEARCHIVE
  if (DebugLevel[CurFile])
    PRINT0("\nInserting face neighbor data\n");
#endif

  if (FaceConnections == NULL)
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECFACE111) Invalid array\n");
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  /*
   * Face neighbor connection have the following format for both
   * binary:
   *
   *   LOCALONETOONE     3         cz,fz,cz
   *   LOCALONETOMANY    nz+4      cz,fz,oz,nz,cz1,cz2,...,czn
   *   GLOBALONETOONE    4         cz,fz,ZZ,CZ
   *   GLOBALONETOMANY   2*nz+4    cz,fz,oz,nz,ZZ1,CZ1,ZZ2,CZ2,...,ZZn,CZn
   *  
   *   Where:
   *       cz = cell in current zone
   *       fz = face of cell in current zone
   *       oz = face obsuration flag (only applies to one-to-many):
   *              0 = face partially obscured
   *              1 = face entirely obscured
   *       nz = number of cell or zone/cell associations (only applies to one-to-many)
   *       ZZ = remote Zone
   *       CZ = cell in remote zone
   *
   * NOTE:
   *   As of version 103 Tecplot assumes that face neighbors are zero based
   *   instead of ones based. Since we have to maintain the contract we
   *   subtract 1 for the caller.
   */
  Ptr = FaceConnections;
  for(i = 0; i < NumFaceConnections[CurFile][CurZone[CurFile]]; i++)
    {
      INTEGER4 n;
      INTEGER4 NumNum = 0;

      switch(FaceNeighborMode[CurFile])
        {
          case FaceNeighborMode_LocalOneToOne:
            NumNum = 3;
            break;
          case FaceNeighborMode_LocalOneToMany:
            NumNum = 4 + Ptr[3];
            break;
          case FaceNeighborMode_GlobalOneToOne:
            NumNum = 4;
            break;
          case FaceNeighborMode_GlobalOneToMany:
            NumNum = 4 + 2 * Ptr[3];
            break;
          default:
            CHECK(FALSE);
            break;
        }

      n = 0;
      if (FaceNeighborMode[CurFile] == FaceNeighborMode_LocalOneToMany ||
          FaceNeighborMode[CurFile] == FaceNeighborMode_GlobalOneToMany)
        {
          /*
           * Write cz,fz,oz,nz: we do this by hand because the oz and nz values
           * are not zero based values.
           */
          if (!WriteBinaryInt32(BlckFile[CurFile], Ptr[n++]-1) || /* zero based as of version 103 */
              !WriteBinaryInt32(BlckFile[CurFile], Ptr[n++]-1) || /* zero based as of version 103 */
              !WriteBinaryInt32(BlckFile[CurFile], Ptr[n++])   || /* ones based */
              !WriteBinaryInt32(BlckFile[CurFile], Ptr[n++]))     /* ones based */
            {
#if defined MAKEARCHIVE
              if ( DebugLevel[CurFile] )
                printf("Err: (TECFACE111) Write failure for file %d\n", CurFile+1);
#endif
              NumErrs[CurFile]++;
              return (-1);
            }

        }
      /* starting from where we left off, output the remaining values */
      for(; n < NumNum; n++)
        if (!WriteBinaryInt32(BlckFile[CurFile], Ptr[n]-1)) /* zero based as of version 103 */
          {
#if defined MAKEARCHIVE
            if ( DebugLevel[CurFile] )
              printf("Err: (TECFACE111) Write failure for file %d\n", CurFile+1);
#endif
            NumErrs[CurFile]++;
            return (-1);
          }
      Ptr += NumNum;
    }

  return (0);
}

LIBFUNCTION INTEGER4 LIBCALL TECFACE110(INTEGER4 *FaceConnections)
{
  return TECFACE111(FaceConnections);
}

LIBFUNCTION INTEGER4 LIBCALL TECFACE100(INTEGER4 *FaceConnections)
{
  return TECFACE111(FaceConnections);
}

#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL tecface111_(INTEGER4 *FaceConnections)
{
  return TECFACE111(FaceConnections);
}

LIBFUNCTION INTEGER4 LIBCALL tecface110_(INTEGER4 *FaceConnections)
{
  return TECFACE111(FaceConnections);
}

LIBFUNCTION INTEGER4 LIBCALL tecface100_(INTEGER4 *FaceConnections)
{
  return TECFACE111(FaceConnections);
}
#endif


/**
 * TECPOLYXXX
 */
LIBFUNCTION INTEGER4 LIBCALL TECPOLY111(INTEGER4 *FaceNodeCounts,
                                        INTEGER4 *FaceNodes,
                                        INTEGER4 *FaceLeftElems,
                                        INTEGER4 *FaceRightElems,
                                        INTEGER4 *FaceBndryConnectionCounts,
                                        INTEGER4 *FaceBndryConnectionElems,
                                        INTEGER2 *FaceBndryConnectionZones)
{
  INTEGER4 NumFaces = KMax[CurFile];
  INTEGER4 Result = 0;
  LgIndex_t Index;
  LgIndex_t MinNeighborValue = TECIO_NO_NEIGHBORING_ELEM;

  /* Mark that the face map has been written for the zone even if it fails so as not to add extra error messages. */
  FaceNeighborsOrMapWritten[CurFile][CurZone[CurFile]] = TRUE;

  if (NumFaces == 0 ||
      (ZoneType[CurFile] != FEPOLYGON &&
       ZoneType[CurFile] != FEPOLYHEDRON))
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECPOLY111) The zone type must be FEPOLYGON or FEPOLYHEDRON and have NumFaces (KMax) > 0.\n");
      PRINT1("     NumFaces = %d\n", NumFaces);
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  if (ZoneType[CurFile] == FEPOLYHEDRON) /* FEPOLYGON doesn't need TotalNumFaceNodes since this is 2*NumFaces */
    {
      if (TotalNumFaceNodes[CurFile][CurZone[CurFile]] <= 0)
        {
#ifdef MAKEARCHIVE
          PRINT0("Err: (TECPOLY111) TotalNumFaceNodes MUST be specified for polyhedral zones.\n");
          PRINT1("     TotalNumFaceNodes = %d\n", TotalNumFaceNodes[CurFile][CurZone[CurFile]]);
#endif
          NumErrs[CurFile]++;
          return (-1);
        }
    }
  else
    {
      if (TotalNumFaceNodes[CurFile][CurZone[CurFile]] != (2 * NumFaces))
        {
#ifdef MAKEARCHIVE
          PRINT0("Err: (TECPOLY111) TotalNumFaceNodes is specified for the polygonal zone but is not equal to 2 * NumFaces.\n");
          PRINT2("     TotalNumFaceNodes = %d.  If specified, it must be 2 * %d.", TotalNumFaceNodes[CurFile][CurZone[CurFile]], NumFaces);
#endif
          NumErrs[CurFile]++;
          return (-1);
        }
    }

  if ((TotalNumFaceBndryFaces[CurFile] > 0  &&
       TotalNumFaceBndryConns[CurFile] > 0) ||
      (TotalNumFaceBndryFaces[CurFile] == 0 &&
       TotalNumFaceBndryConns[CurFile] == 0))
    {
      if (TotalNumFaceBndryFaces[CurFile] > 0)
        MinNeighborValue = -TotalNumFaceBndryFaces[CurFile];
    }
  else
    {
#ifdef MAKEARCHIVE
      PRINT0("Err: (TECPOLY111) TotalNumFaceBndryFaces and TotalNumFaceBndryConns must both be 0 or both be > 0.\n");
      PRINT2("     TotalNumFaceBndryFaces = %d, TotalNumFaceBndryConns = %d\n", TotalNumFaceBndryFaces[CurFile], TotalNumFaceBndryConns[CurFile]);
#endif
      NumErrs[CurFile]++;
      return (-1);
    }

  /* Write the facenodesoffsets array from the facenodecounts array. */
  if (Result == 0)
    {
      if (ZoneType[CurFile] == FEPOLYHEDRON) /* FEPOLYGON doesn't need to specify facenodesoffsets */
        {
          Int32_t FaceNodeSum = 0;
          if (!WriteBinaryInt32(BlckFile[CurFile], 0))
            Result = -1;
          for (Index = 0; (Result == 0) && (Index < NumFaces); Index++)
            {
              FaceNodeSum += FaceNodeCounts[Index];
              if (FaceNodeCounts[Index] < 3)
                {
#ifdef MAKEARCHIVE
                  PRINT1("Err: (TECPOLY111) Invalid face node count value at face %d.  There must be at least 3 nodes in a face.\n", Index+1);
                  PRINT1("     Face node count value = %d.\n", FaceNodeCounts[Index]);
#endif
                  NumErrs[CurFile]++;
                  return (-1);
                }
              else if (FaceNodeSum > TotalNumFaceNodes[CurFile][CurZone[CurFile]])
                {
#ifdef MAKEARCHIVE
                  PRINT1("Err: (TECPOLY111) The running face node count exceeds the TotalNumFaceNodes (%d) specified.\n", TotalNumFaceNodes[CurFile][CurZone[CurFile]]);
                  PRINT1("     Face node count value = %d.\n", FaceNodeCounts[Index]);
#endif
                  NumErrs[CurFile]++;
                  return (-1);
                }
              else if (!WriteBinaryInt32(BlckFile[CurFile], FaceNodeSum))
                Result = -1;
            }
        }
    }

  /* Write the facenodes array but convert 1-based to 0-based. */
  for (Index = 0; (Result == 0) && (Index < TotalNumFaceNodes[CurFile][CurZone[CurFile]]); Index++)
    {
      if (FaceNodes[Index] < 1 ||
          FaceNodes[Index] > IMax[CurFile])
        {
#ifdef MAKEARCHIVE
          PRINT1("Err: (TECPOLY111) Invalid face node value at node %d:\n", Index+1);
          PRINT2("     face node value = %d, valid values are are 1 to %d (inclusive).\n", FaceNodes[Index], IMax[CurFile]);
#endif
          NumErrs[CurFile]++;
          return (-1);
        }
      else if (!WriteBinaryInt32(BlckFile[CurFile], FaceNodes[Index]-1))
        Result = -1;
    }

  /* Write the left elements array but convert 1-based to 0-based. */
  for (Index = 0; (Result == 0) && (Index < NumFaces); Index++)
    {
      if (FaceLeftElems[Index] < MinNeighborValue ||
          FaceLeftElems[Index] > JMax[CurFile])
        {
#ifdef MAKEARCHIVE
          PRINT1("Err: (TECPOLY111) Invalid left neighbor value at face %d:\n", Index);
          PRINT2("     left neighbor value = %d, min value = %d,", FaceLeftElems[Index], MinNeighborValue);
          PRINT1(" max value = %d.\n", JMax[CurFile]);
#endif
          NumErrs[CurFile]++;
          return (-1);
        }
      else if (!WriteBinaryInt32(BlckFile[CurFile], FaceLeftElems[Index]-1))
        Result = -1;
    }
  /* Write the right elements array but convert 1-based to 0-based. */
  for (Index = 0; (Result == 0) && (Index < NumFaces); Index++)
    {
      if (FaceRightElems[Index] < MinNeighborValue ||
          FaceRightElems[Index] > JMax[CurFile])
        {
#ifdef MAKEARCHIVE
          PRINT1("Err: (TECPOLY111) Invalid right neighbor value at face %d:\n", Index);
          PRINT2("     right neighbor value = %d, min value = %d,", FaceRightElems[Index], MinNeighborValue);
          PRINT1(" max value = %d.\n", JMax[CurFile]);
#endif
          NumErrs[CurFile]++;
          return (-1);
        }
      else if (!WriteBinaryInt32(BlckFile[CurFile], FaceRightElems[Index]-1))
        Result = -1;

      if (Result == 0 &&
          (FaceLeftElems[Index] == TECIO_NO_NEIGHBORING_ELEM &&
           FaceRightElems[Index] == TECIO_NO_NEIGHBORING_ELEM))
        {
#ifdef MAKEARCHIVE
          PRINT1("Err: (TECPOLY111) Both left and right neighbors are set to no neighboring element at face %d.\n", Index);
#endif
          NumErrs[CurFile]++;
          return (-1);
        }
    }

  /* Write the boundary arrays. */
  if (Result == 0 && TotalNumFaceBndryFaces[CurFile] > 0)
    {
      /* Write the boundaryconnectionoffsets array from the boundaryconnectioncounts array. */
      
      /*
       * As a convenience for the ASCII format, TecUtil, and TECIO layers if any
       * boundary connections exists we automatically add a no-neighboring
       * connection as the first item so that they can user 0 for no-neighboring
       * element in the element list regardless if they have boundary connections
       * or not.
       *
       * The first 2 offsets are always 0 so that -1 in the left/right element
       * arrays always indicates "no neighboring element".
       */
      if (!(WriteBinaryInt32(BlckFile[CurFile], 0) &&
            WriteBinaryInt32(BlckFile[CurFile], 0)))
        Result = -1;

      Int32_t BndryConnCount = 0;
      for (Index = 0; (Result == 0) && (Index < TotalNumFaceBndryFaces[CurFile]); Index++)
        {
          BndryConnCount += FaceBndryConnectionCounts[Index];
          if (FaceBndryConnectionCounts[Index] < 0 ||
              BndryConnCount > TotalNumFaceBndryConns[CurFile])
            {
#ifdef MAKEARCHIVE
              PRINT1("Err: (TECPOLY111) Invalid boundary connection count at boundary face %d:\n", Index+1);
              PRINT1("     boundary connection count = %d.\n", FaceBndryConnectionCounts[Index]);
#endif
              NumErrs[CurFile]++;
              return (-1);
            }
          else if (!WriteBinaryInt32(BlckFile[CurFile], BndryConnCount))
            Result = -1;
        }
      if (BndryConnCount != TotalNumFaceBndryConns[CurFile])
        {
#ifdef MAKEARCHIVE
          PRINT0("Err: (TECPOLY111) Invalid number of boundary connections:\n");
          PRINT2("     number of boundary connections written = %d, total number of boundary connections = %d.",
                 BndryConnCount, TotalNumFaceBndryConns[CurFile]);
#endif
          NumErrs[CurFile]++;
          return (-1);
        }

      /* Write the boundary connection elements but convert 1-based to 0-based. */
      BndryConnCount = 0;
      for (Index = 0; (Result == 0) && (Index < TotalNumFaceBndryFaces[CurFile]); Index++)
        {
          for (LgIndex_t BIndex = 0; (Result == 0) && (BIndex < FaceBndryConnectionCounts[Index]); BIndex++)
            {
              if (BIndex > 0 &&
                  FaceBndryConnectionElems[BndryConnCount] == TECIO_NO_NEIGHBORING_ELEM)
                {
#ifdef MAKEARCHIVE
                  PRINT1("Err: (TECPOLY111) Partially obscured faces must specify no neighboring element first. See boundary connections for face %d.\n", Index+1);
#endif
                  NumErrs[CurFile]++;
                  return (-1);
                }
              if (FaceBndryConnectionElems[BndryConnCount] < TECIO_NO_NEIGHBORING_ELEM)
                {
#ifdef MAKEARCHIVE
                  PRINT1("Err: (TECPOLY111) Invalid boundary element value at boundary connections for face %d:\n", Index+1);
#endif
                  NumErrs[CurFile]++;
                  return (-1);
                }
              if (FaceBndryConnectionElems[BndryConnCount] == TECIO_NO_NEIGHBORING_ELEM &&
                  FaceBndryConnectionZones[BndryConnCount] != TECIO_NO_NEIGHBORING_ZONE)
                {
#ifdef MAKEARCHIVE
                  PRINT1("Err: (TECPOLY111) Invalid boundary element/zone pair at boundary connections for face %d:\n", Index+1);
                  PRINT0("     Boundary elements specified as no neighboring element must also specify no neighboring zone.\n");
#endif
                  NumErrs[CurFile]++;
                  return (-1);
                }
              else if (!WriteBinaryInt32(BlckFile[CurFile], FaceBndryConnectionElems[BndryConnCount]-1))
                Result = -1;
              BndryConnCount++;
            }
        }

      /* Write the boundary connection zones but convert 1-based to 0-based. */
      BndryConnCount = 0;
      for (Index = 0; (Result == 0) && (Index < TotalNumFaceBndryFaces[CurFile]); Index++)
        {
          for (LgIndex_t BIndex = 0; (Result == 0) && (BIndex < FaceBndryConnectionCounts[Index]); BIndex++)
            {
              if (FaceBndryConnectionZones[BndryConnCount] < TECIO_NO_NEIGHBORING_ZONE)
                {
#ifdef MAKEARCHIVE
                  PRINT1("Err: (TECPOLY111) Invalid boundary zone value at boundary connections for face %d:\n", Index+1);
#endif
                  NumErrs[CurFile]++;
                  return (-1);
                }
              else if (!WriteBinaryInt16(BlckFile[CurFile], FaceBndryConnectionZones[BndryConnCount]-1))
                Result = -1;
              BndryConnCount++;
            }
        }
    }
  if (Result != 0)
    {
      Result = -1;
      WriteErr("TECPOLY111");
    }

  return Result;
}
#if defined MAKEARCHIVE && !defined _WIN32 /* every platform but Windows Intel */
LIBFUNCTION INTEGER4 LIBCALL tecpoly111_(INTEGER4 *FaceNodeCounts,
                                         INTEGER4 *FaceNodes,
                                         INTEGER4 *FaceLeftElems,
                                         INTEGER4 *FaceRightElems,
                                         INTEGER4 *FaceBndryConnectionOffsets,
                                         INTEGER4 *FaceBndryConnectionElems,
                                         INTEGER2 *FaceBndryConnectionZones)
{
  return TECPOLY111(FaceNodeCounts,
                    FaceNodes,
                    FaceLeftElems,
                    FaceRightElems,
                    FaceBndryConnectionOffsets,
                    FaceBndryConnectionElems,
                    FaceBndryConnectionZones);
}
#endif

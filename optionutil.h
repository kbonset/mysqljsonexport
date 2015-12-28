/* Copyright (C) 2000-2005 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */
#ifndef OPTIONUTIL_H
#define OPTIONUTIL_H
#ifdef __cplusplus
extern "C"
{
#endif
#include <stdint.h>
#include <limits.h>

// Portability stuff
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef WINDOWS
typedef int BOOL;
#endif

// Flags for options and operations.
// Note that these has to be shared, for a number of reasons.
// No flags specified.
#define OPT_FLAG_NONE 0x00000000
// Flag if option may not be NULL.
#define OPT_FLAG_NONULL 0x80000000
// Flag if a NULL string means an empty string.
#define OPT_FLAG_NULLEMPTY 0x40000000
// Flag that this value has it's default value.
#define OPT_FLAG_DEFAULT 0x20000000
// Flag if this option may not be used in a configuration file.
#define OPT_FLAG_NOCFGFILE 0x10000000
// Flag if this option will never have it's value printed.
#define OPT_FLAG_NOPRT 0x08000000
// Flag that this option does not have a default.
#define OPT_FLAG_NODEF 0x04000000
// Flag that the default for this option is a reference.
#define OPT_FLAG_DEFREF 0x02000000
// Flag that this option is a help option.
#define OPT_FLAG_HELP 0x01000000
// Flag that this option is indexed.
#define OPT_FLAG_INDEXED 0x00800000
// Flag that a BOOL/INT/UINT option without an argument on the commandline
// means 1.
#define OPT_FLAG_CLNOARG1 0x00400000
// Flag that this option, when used as a short option, will only
// consume the current argument and not move to the next if this is empty.
#define OPT_FLAG_SHORT1ARG 0x00200000
// Flag to indicate that there is an array of config filenames.
#define OPT_FLAG_CFGFILEARRAY 0x00100000
// Flag that the default for an array type is also an array.
// This is used for SELARRAY and STRARRAY types.
// This is the same as CFGFILEARRAY but also implies DEFREF.
#define OPT_FLAG_DEFARRAY (0x00100000 | OPT_FLAG_DEFREF)
// Flag to ignore unknown option in config file.
#define OPT_FLAG_IGNUNKNOWNOPT 0x00080000
// Flag that this is an array of values.
#define OPT_FLAG_ARRAY 0x00010000
// Flag that CFGFILE default, if used and if there is an array, all files
// will be read. This implies the CFGFILEARRAY flag.
#define OPT_FLAG_CFGREADALLDEF 0x00110000
// Flag that this option is hidden in help and printouts. Defaults are
// used though, if this is a CFGFILEMAIN option. It may be specified.
#define OPT_FLAG_HIDDEN 0x00008000
// Flag that this is an unsigned type.
#define OPT_FLAG_UNSIGNED 0x00004000
// Reverse the meaning of a BOLL argument. This is the same as the UNSIGNED
// flag which is reduced to save some bits.
#define OPT_FLAG_REVERSE 0x00004000
// Flag that this is a password argument, casing command line argument option
// to be overwritten.
#define OPT_FLAG_PWD 0x00001000
// Flag that this option was set in on the commandline.
#define OPT_FLAG_SETONCMDLINE 0x00020000
// Flag that we are processing commandline.
#define OPT_FLAG_CMDLINE 0x00008000

// Flags for operations.
// Flag that we are to print help for option.
#define OPT_FLAG_PRTHELP 0x00040000
// Flag that we are to print non-default values only.
#define OPT_FLAG_PRTNONDEF 0x00020000

// Define sizes for integer types.
#define OPT_FLAG_SIZE64 0x00001C00
#define OPT_FLAG_SIZE32 0x00001800
#define OPT_FLAG_SIZE16 0x00001400
#define OPT_FLAG_SIZE8 0x00001000

// Now define non-portable types.
#define OPT_FLAG_SIZELONG 0x00000C00
#define OPT_FLAG_SIZESHORT 0x00000800
#define OPT_FLAG_SIZEINT 0x00000400

// Mask for the option type only.
#define OPT_MASK_TYPE 0x0000000F
#define OPT_MASK_OPER 0x000000FF
#define OPT_MASK_FLAG 0xFFFFFF00
#define OPT_MASK_SIZE 0x00001C00
#define OPT_MASK_TYPE_SIZE (OPT_MASK_SIZE | OPT_MASK_TYPE)

// Macro to check flags for option.
#define CHECK_OPT_FLAG(X,Y) (((X)->nFlags & OPT_FLAG_ ## Y) == OPT_FLAG_ ## Y)
#define CHECK_OPT_TYPE(X,Y) (((X)->nFlags & OPT_MASK_TYPE) == (OPT_TYPE_ ## Y & OPT_MASK_TYPE))
#define CHECK_OPT_OPER(X,Y) (((X) & OPT_MASK_OPER) == OPT_OPER_ ## Y)
#define CHECK_OPT_OPER_FLAG(X,Y) (((X) & OPT_FLAG_ ## Y) == OPT_FLAG_ ## Y)
#define CHECK_OPT_SIZE(X,Y) (((X)->nFlags & OPT_MASK_SIZE) == OPT_FLAG_ ## Y)
#define SET_OPT_FLAG(X,Y) ((X)->nFlags |= OPT_FLAG_ ## Y)
#define CLR_OPT_FLAG(X,Y) ((X)->nFlags &= ~OPT_FLAG_ ## Y)
#define GET_OPT_FLAGS(X) ((X)->nFlags & ~OPT_MASK_TYPE)
#define GET_OPT_FLAG(X,Y) ((X)->nFlags & OPT_FLAG_ ## Y)
#define GET_OPT_SIZE(X) ((X)->nFlags & OPT_MASK_SIZE)
#define GET_OPT_TYPE_SIZE(X) ((X)->nFlags & OPT_MASK_TYPE_SIZE)
#define CHECK_OPT_8BYTE(X) (CHECK_OPT_SIZE((X), SIZE64) || (CHECK_OPT_SIZE((X), SIZESHORT) && sizeof(short) == 8) \
  || (CHECK_OPT_SIZE((X), SIZEINT) && sizeof(int) == 8) || (CHECK_OPT_SIZE((X), SIZELONG) && sizeof(long) == 8))
#define CHECK_OPT_4BYTE(X) (CHECK_OPT_SIZE((X), SIZE32) || (CHECK_OPT_SIZE((X), SIZESHORT) && sizeof(short) == 4) \
  || (CHECK_OPT_SIZE((X), SIZEINT) && sizeof(int) == 4) || (CHECK_OPT_SIZE((X), SIZELONG) && sizeof(long) == 4))
#define CHECK_OPT_2BYTE(X) (CHECK_OPT_SIZE((X), SIZE16) || (CHECK_OPT_SIZE((X), SIZESHORT) && sizeof(short) == 2) \
  || (CHECK_OPT_SIZE((X), SIZEINT) && sizeof(int) == 2) || (CHECK_OPT_SIZE((X), SIZELONG) && sizeof(long) == 2))
#define CHECK_OPT_1BYTE(X) CHECK_OPT_SIZE((X), SIZE8)
#define GET_OPT_BYTES(X) (CHECK_OPT_8BYTE(X) ? 8 : (CHECK_OPT_4BYTE(X) ? 4 : (CHECK_OPT_2BYTE(X) ? 2 : 1)))

// Option types.
#define OPT_TYPE_NONE 0x00000000
#define OPT_TYPE_INTBASE 0x00000001
#define OPT_TYPE_INT8 (OPT_TYPE_INTBASE | OPT_FLAG_SIZE8)
#define OPT_TYPE_INT16 (OPT_TYPE_INTBASE | OPT_FLAG_SIZE16)
#define OPT_TYPE_INT32 (OPT_TYPE_INTBASE | OPT_FLAG_SIZE32)
#define OPT_TYPE_INT64 (OPT_TYPE_INTBASE | OPT_FLAG_SIZE64)
#define OPT_TYPE_SHORT (OPT_TYPE_INTBASE | OPT_FLAG_SIZESHORT)
#define OPT_TYPE_LONG (OPT_TYPE_INTBASE | OPT_FLAG_SIZELONG)
#define OPT_TYPE_UINT8 (OPT_TYPE_INT8 | OPT_FLAG_UNSIGNED)
#define OPT_TYPE_BYTE OPT_TYPE_UINT8
#define OPT_TYPE_UBYTE OPT_TYPE_UINT8
#define OPT_TYPE_UINT16 (OPT_TYPE_INT16 | OPT_FLAG_UNSIGNED)
#define OPT_TYPE_UINT32 (OPT_TYPE_INT32 | OPT_FLAG_UNSIGNED)
#define OPT_TYPE_UINT64 (OPT_TYPE_INT64 | OPT_FLAG_UNSIGNED)
#define OPT_TYPE_USHORT (OPT_TYPE_SHORT | OPT_FLAG_UNSIGNED)
#define OPT_TYPE_INT (0x00000002 | OPT_FLAG_SIZEINT)
#define OPT_TYPE_UINT (0x00000003 | OPT_FLAG_SIZEINT | OPT_FLAG_UNSIGNED)
#define OPT_TYPE_ULONG (OPT_TYPE_LONG | OPT_FLAG_UNSIGNED)
#define OPT_TYPE_STR 0x00000004
#define OPT_TYPE_STRARRAY (OPT_TYPE_STR | OPT_FLAG_ARRAY)
#define OPT_TYPE_BOOL 0x00000005
#define OPT_TYPE_SEL 0x00000006
#define OPT_TYPE_SELARRAY (OPT_TYPE_SEL | OPT_FLAG_ARRAY)
#define OPT_TYPE_FUNC 0x00000007
#define OPT_TYPE_CFGFILEMAIN 0x00000008
#define OPT_TYPE_CFGFILE 0x00000009
#define OPT_TYPE_KEYVALUELIST 0x0000000A
#define OPT_TYPE_BOOLREVERSE (OPT_TYPE_BOOL | OPT_FLAG_REVERSE)

// These are special ones. Indexed options always have the indexed flag on.
#define OPT_TYPE_INDEXED_STR (OPT_TYPE_STR | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_INT8 (OPT_TYPE_INT8 | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_INT16 (OPT_TYPE_INT16 | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_INT32 (OPT_TYPE_INT32 | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_INT64 (OPT_TYPE_INT64 | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_SHORT (OPT_TYPE_SHORT | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_INT (OPT_TYPE_INT | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_LONG (OPT_TYPE_LONG | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_UINT8 (OPT_TYPE_UINT8 | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_BYTE OPT_TYPE_INDEXED_UINT8
#define OPT_TYPE_INDEXED_UINT16 (OPT_TYPE_UINT16 | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_UINT32 (OPT_TYPE_UINT32 | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_UINT64 (OPT_TYPE_UINT64 | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_USHORT (OPT_TYPE_USHORT | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_UINT (OPT_TYPE_UINT | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_ULONG (OPT_TYPE_ULONG | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_BOOL (OPT_TYPE_BOOL | OPT_FLAG_INDEXED)
#define OPT_TYPE_INDEXED_SEL (OPT_TYPE_SEL | OPT_FLAG_INDEXED)

// The following operations can be applied to an option,
// Set to NULL, set to default, set from commandline, set from config file,
// print, list, execute and free.
#define OPT_OPER_NULL 0
#define OPT_OPER_DEF 1
#define OPT_OPER_SET 2
#define OPT_OPER_PRT 3
#define OPT_OPER_FREE 4

// Option processing errors.
#define OPT_ERR_PRINTED 0x40000000
#define OPT_ERR_ERRMASK 0x00000FFF
#define OPT_ERR_USER 0x00000080
#define OPT_ERR_NONE 0
#define OPT_ERR_MALLOC 1
#define OPT_ERR_FILENOTFOUND 2
#define OPT_ERR_UNKNOWNVALUE 3
#define OPT_ERR_INVALIDSTRING 4
#define OPT_ERR_NULLNOTVALID 5
#define OPT_ERR_MULTIMAINCFG 6
#define OPT_ERR_HELPFOUND 7
#define OPT_ERR_NOHOME 8
#define OPT_ERR_PATHTOOLONG 9
#define OPT_ERR_SECTIONHEAD 10
#define OPT_ERR_UNKNOWNOPTION 11
#define OPT_ERR_MAINCFGINFILE 12
#define OPT_ERR_MUSTHAVEVALUE 13
#define OPT_ERR_WRONGINTVALUE 14
#define OPT_ERR_WRONGINTFMT 15
#define OPT_ERR_WRONGBOOLVALUE 16
#define OPT_ERR_HELPINFILE 17
#define OPT_ERR_OPENFILE 18
#define OPT_ERR_READFILE 19
#define OPT_ERR_ILLEGALFORMAT 20
#define OPT_ERR_UNEXPECTEDEOF 21
#define OPT_ERR_MUSTBESPECIFIED 22
#define OPT_ERR_INVALIDFILENAME 23
#define OPT_ERR_FILENAMEMISSING 24
#define OPT_ERR_UNKNOWNOPER 25
#define OPT_ERR_CFGFILEMAINNOVALUE 26
#define OPT_ERR_DEFCFGFILENF 27
#define OPT_ERR_INTTOOLARGE 28
#define OPT_ERR_UNKNOWNINTSIZE 29
#define OPT_ERR_UNKNOWNFLAG 30
#define OPT_ERR_KEYVALUESPACE 31
#define OPT_ERR_KEYVALUEFORMAT 32
#define OPT_ERR_KEYVALUENULL 33
#define OPT_ERR_OPTNOTINCFG 34
#define OPT_ERR_USER1 (1 | OPT_ERR_USER)
#define OPT_ERR_USER2 (2 | OPT_ERR_USER)
#define OPT_ERR_USER3 (3 | OPT_ERR_USER)
#define OPT_ERR_USER4 (4 | OPT_ERR_USER)
#define OPT_ERR_USER5 (5 | OPT_ERR_USER)
#define CHECK_ERR_CODE(X,Y) (((X) & OPT_ERR_ERRMASK) == OPT_ERR_ ## Y)

// Typedefs.
typedef struct tagOPTIONS
   {
   char *pName;
   uint32_t nFlags;
   void *pValue;
   void *pDefault;
   char *pHelp;
   void *pExtra;
   } OPTIONS, *POPTIONS;

typedef struct tagINDEXEDOPT
   {
   int32_t nId;
   int64_t nValue;
   } INDEXEDOPT, *PINDEXEDOPT;

typedef struct tagKEYVALUE
   {
   struct tagKEYVALUE *pNext;
   char *pKey;
   char *pValue;
   } KEYVALUE, *PKEYVALUE;

// Function prototypes.
uint32_t ou_OptionArraySetNull(POPTIONS);
uint32_t ou_OptionArraySetDef(POPTIONS);
uint32_t ou_OptionArrayPrintHelp(POPTIONS, FILE *, uint32_t);
uint32_t ou_OptionArrayPrintValues(POPTIONS, FILE *, uint32_t);
uint32_t ou_OptionArraySetValues(POPTIONS, int *, char **, uint32_t);
uint32_t ou_OptionArrayFree(POPTIONS);
uint32_t ou_OptionArrayProcessFile(POPTIONS, char *, char *, uint32_t);
uint32_t ou_OptionArrayProcessFiles(POPTIONS, char **, char *, uint32_t);
uint32_t ou_ProcessOption(POPTIONS, POPTIONS, int, uint32_t, void *);
void *ou_GetOptionDefault(POPTIONS, char *);
BOOL ou_IsOptionSet(POPTIONS, char *);
POPTIONS ou_GetOptionByName(POPTIONS, char *);
char *ou_GetIndexedOptString(PINDEXEDOPT *, int32_t);
int64_t ou_GetIndexedOptInt(PINDEXEDOPT *, int32_t);
PINDEXEDOPT ou_GetIndexedOpt(PINDEXEDOPT *, int32_t);
BOOL ou_IsIndexedOptSet(PINDEXEDOPT *, int32_t);
uint32_t ou_GetIndexedOptCount(PINDEXEDOPT *);
char *ou_GetOptionName(POPTIONS, int32_t);
BOOL ou_AddStringToArray(char *, char ***);
BOOL ou_AddIntegerToArray(int64_t, int, void **);
BOOL ou_StrExistsInArray(char *, char **, BOOL);
BOOL ou_KeyValueExists(PKEYVALUE, char *);
char *ou_GetKeyValue(PKEYVALUE, char *);
#ifdef __cplusplus
}
#endif
#endif

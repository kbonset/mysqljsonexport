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

/*
 * Program: optionutil.c
 * Command line and option file handler.
 *
 * Change log
 * Who        Date       Comments
 * ========== ========== ==================================================
 * Karlsson   2011-06-15 Added ALLOWUNKNOWN option.
 * Karlsson   2012-05-26 Fixed issue with NULL OPT_TYP_SEL value, this is
 *                       invalid and caused a core dump, now an error is
 *                       returned instead.
 * Karlsson   2012-05-28 Added support for KEYVALUEARRAY.
 * Karlsson   2012-06-20 Added ou_KeyValueExists() and ou_GetKeyValue().
 * Karlsson   2012-07-23 Removed ALLOWUNKNOWNOPT when reading config file
 *                       and checnged to instead prefix the section name with -.
 *                       Added multiple config file sections separated with ;.
 * Karlsson   2012-07-30 Fixed so that BOOLREVERSE works as expected.
 * Karlsson   2012-08-06 Added integer array support.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
// We will need the PRIxxx macros in inttypes.h
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>
#include "config.h"
#include "optionutil.h"
#ifdef MYALLOC
#include "myalloc.h"
#endif

// Portability macros.
#ifdef WINDOWS
#define strcasecmp(X,Y) stricmp(X,Y)
#define strncasecmp(X,Y,Z) strnicmp(X,Y,Z)
#endif

// Some macros.
#define OPT_ARRAY_INCR 10

// Function prototypes for option vakue type handlers.
static uint32_t HandleStringOption(POPTIONS, POPTIONS, int, uint32_t, void *);
static uint32_t HandleIntOption(POPTIONS, POPTIONS, int, uint32_t, void *);
static void PrintIntegerValue(FILE *, int, BOOL, int64_t);
static uint32_t HandleBoolOption(POPTIONS, POPTIONS, int, uint32_t, void *);
static uint32_t HandleSelectionOption(POPTIONS, POPTIONS, int, uint32_t,
  void *);
static uint32_t HandleStringArrayOption(POPTIONS, POPTIONS, int, uint32_t,
  void *);
static uint32_t HandleSelectionArrayOption(POPTIONS, POPTIONS, int, uint32_t,
  void *);
static uint32_t HandleCfgfileOption(POPTIONS, POPTIONS, int, uint32_t, void *);
static uint32_t HandleIndexedStrOption(POPTIONS, POPTIONS, int, uint32_t,
  void *);
static uint32_t HandleIndexedIntOption(POPTIONS, POPTIONS, int, uint32_t,
  void *);
static uint32_t HandleIndexedSelectionOption(POPTIONS, POPTIONS, int, uint32_t,
  void *);
static uint32_t HandleKeyValueListOption(POPTIONS, POPTIONS, int, uint32_t,
  void *);

// Error message strings.
static char *g_pError[] = {
"No Error", // OPT_ERR_NONE
"Memory allocation error", // OPT_ERR_MALLOC
"Can't open file '%s'", // OPT_ERR_FILENOTFOUND
"Unknown value '%s'", // OPT_ERR_UNKNOWNVALUE
"Invalid string '%s'", // OPT_ERR_INVALIDSTRING
"Option cannot be NULL", // OPT_ERR_NULLNOTVALID
"More than one main option file argument", // OPT_ERR_MULTIMAINCFG
"Help option found", // OPT_ERR_HELPFOUND
"Cannot expand filename, HOME environment variable not set", // OPT_ERR_NOHOME
"Path to file is too long", // OPT_ERR_PATHTOOLONG
"Error in specification of section", // OPT_ERR_SECTIONHEAD
"Unknown option '%s'", // OPT_ERR_UNKNOWNOPTION
"Main option file cannot be specified", // OPT_ERR_MAINCFGINFILE
"Option must have a value", // OPT_ERR_MUSTHAVEVALUE
"Erroneous integer value", // OPT_ERR_WRONGINTVALUE
"Erroneous integer value format", // OPT_ERR_WRONGINTFMT
"Erroneous boolean value format", // OPT_ERR_WRONGBOOLVALUE
"Help option cannot be specified", // OPT_ERR_HELPINFILE
"System error '%s' opening file '%s'", // OPT_ERR_OPENFILE
"System error '%s' reading file '%s'", // OPT_ERR_READFILE
"Illegal value format", // OPT_ERR_ILLEGALFORMAT
"Unexpected EOF", // OPT_ERR_UNEXPECTEDEOF
"Must be specified", // OPT_ERR_MUSTBESPECIFIED
"Invalid filename '%s", // OPT_ERR_INVALIDFILENAME
"Filename missing", // OPT_ERR_FILENAMEMISSING
"Unknown operation", // OPT_ERR_UNKNOWNOPER
"Main configuration file name must not be NULL", // OPT_ERR_CFGFILEMAINNOVALUE
"Can't open default configuration file", // OPT_ERR_DEFCFGFILENF
"Integer value too large", // OPT_ERR_INTTOOLARGE
"Unknown integer size", // OPT_ERR_UNKNOWNINTSIZE
"Unknown flag of combination of flags", // OPT_ERR_UNKNOWNFLAG
"Key must not contain any space characters", // OPT_ERR_KEYVALUESPACE
"Invalid format for key-value setting", // OPT_ERR_KEYVALUEFORMAT
"Key value pair cannot be NULL", // OPT_ERR_KEYVALUENULL
"Can't have option '%s' in config file", // OPT_ERR_OPTNOTINCFG
NULL
};

// Static function prototypes.
static POPTIONS FindLongOption(POPTIONS, char *, int *);
static POPTIONS FindShortOption(POPTIONS, char **, int, char **, int *);
static uint32_t ReadOptionFile(char *, char *, POPTIONS, uint32_t);
static char *ReadOptionFileLine(FILE *, char *, int *, int *);
static int64_t GetIntegerArrayValue(POPTIONS, int);
static BOOL RemoveIntegerFromArray(int, int **);
static BOOL AddIndexedOptToArray(PINDEXEDOPT, PINDEXEDOPT **);
static char *GetOptionValueString(char *);
static char *TranslateString(char *);
static char *UntranslateString(char *);
static uint32_t IntFromString(char *, int64_t *, uint32_t);
static uint32_t PrintOptionError(uint32_t, POPTIONS, int, char *, char *, int);

// Public functions.
/*
 * Function: ou_OptionArraySetNull()
 * Set values in option array to NULL.
 * Arguments:
 * POPTIONS pOptArray - Option array to set to NULL.
 * Returns:
 * uint32_t - Errorcode, 0 if there were no errors.
 */
uint32_t ou_OptionArraySetNull(POPTIONS pOptArray)
   {
   POPTIONS pOpt;

// Loop for all options in array.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      ou_ProcessOption(pOptArray, pOpt, -1, OPT_OPER_NULL, NULL);

   return OPT_ERR_NONE;
   } // End of ou_OptionArraySetNull().


/*
 * Function: ou_OptionArraySetDef()
 * Set values in option array to default values.
 * Arguments:
 * POPTIONS pOptArray - Option array to set to default.
 * Returns:
 * uint32_t - Errorcode, 0 if there were no errors.
 */
uint32_t ou_OptionArraySetDef(POPTIONS pOptArray)
   {
   POPTIONS pOpt;

// Loop for all non-DEFREF options in array.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      {
      if(!CHECK_OPT_FLAG(pOpt, DEFREF))
         ou_ProcessOption(pOptArray, pOpt, -1, OPT_OPER_DEF, NULL);
      }

// Then loop for all DEFREF options in array.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      {
      if(CHECK_OPT_FLAG(pOpt, DEFREF))
         ou_ProcessOption(pOptArray, pOpt, -1, OPT_OPER_DEF, NULL);
      }

   return OPT_ERR_NONE;
   } // End of ou_OptionArraySetDef().


/*
 * Function: ou_OptionArrayPrintHelp()
 * Print help for options in array.
 * Arguments:
 * POPTIONS pOptArray - Option array to set to default.
 * FILE *fp - File to print help to.
 * uint32_t nFlags - Processing flags.
 * Returns:
 * uint32_t - Errorcode, 0 if there were no errors.
 */
uint32_t ou_OptionArrayPrintHelp(POPTIONS pOptArray, FILE *fp, uint32_t nFlags)
   {
   POPTIONS pOpt;
   char *pLongName, *pShortName;
   char *pDef;
   char *pTmp;
   char szBuf[256];

// Loop for all options in array.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      {
      if(pOpt->pName == NULL)
         continue;

// Ignore hidden options.
      if(CHECK_OPT_FLAG(pOpt, HIDDEN))
         continue;

// If there is no helptext, then skip.
      if(pOpt->pHelp == NULL)
         continue;

// Figure out the long name.
      if(pOpt->pName[1] == '|' && pOpt->pName[2] == '\0')
         pLongName = NULL;
      else
         pLongName = ou_GetOptionName(pOpt, -1);
      pShortName = pOpt->pName[1] == '|' ? pOpt->pName : NULL;

// Get the default value.
      if(CHECK_OPT_FLAG(pOpt, NODEF) || CHECK_OPT_FLAG(pOpt, DEFREF)
        || CHECK_OPT_FLAG(pOpt, INDEXED) || CHECK_OPT_FLAG(pOpt, HELP))
         {
         pDef = NULL;
         }
      else if(CHECK_OPT_TYPE(pOpt, STR))
         pDef = pOpt->pDefault;
      else if(CHECK_OPT_TYPE(pOpt, INTBASE) && !CHECK_OPT_FLAG(pOpt, UNSIGNED))
         {
         if(CHECK_OPT_SIZE(pOpt, SIZE64)
           || (CHECK_OPT_SIZE(pOpt, SIZEINT) && sizeof(int) == 8)
           || (CHECK_OPT_SIZE(pOpt, SIZESHORT) && sizeof(int) == 8)
           || (CHECK_OPT_SIZE(pOpt, SIZELONG) && sizeof(long) == 8))
            sprintf(szBuf, "%" PRId64, (int64_t) (intptr_t) pOpt->pDefault);
         else if(CHECK_OPT_SIZE(pOpt, SIZE32)
           || (CHECK_OPT_SIZE(pOpt, SIZEINT) && sizeof(int) == 4)
           || (CHECK_OPT_SIZE(pOpt, SIZESHORT) && sizeof(int) == 4)
           || (CHECK_OPT_SIZE(pOpt, SIZELONG) && sizeof(long) == 4))
            sprintf(szBuf, "%" PRId32, (int32_t) (intptr_t) pOpt->pDefault);
         else if(CHECK_OPT_SIZE(pOpt, SIZE16)
           || (CHECK_OPT_SIZE(pOpt, SIZEINT) && sizeof(int) == 2)
           || (CHECK_OPT_SIZE(pOpt, SIZESHORT) && sizeof(int) == 2)
           || (CHECK_OPT_SIZE(pOpt, SIZELONG) && sizeof(long) == 2))
            sprintf(szBuf, "%" PRId16, (int16_t) (intptr_t) pOpt->pDefault);
         else if(CHECK_OPT_SIZE(pOpt, SIZE8))
            sprintf(szBuf, "%" PRId8, (int8_t) (intptr_t) pOpt->pDefault);
         else
            strcpy(szBuf, "####");

         pDef = szBuf;
         }
      else if(CHECK_OPT_TYPE(pOpt, INTBASE) && CHECK_OPT_FLAG(pOpt, UNSIGNED))
         {
         if(CHECK_OPT_SIZE(pOpt, SIZE64)
           || (CHECK_OPT_SIZE(pOpt, SIZEINT) && sizeof(int) == 8)
           || (CHECK_OPT_SIZE(pOpt, SIZESHORT) && sizeof(int) == 8)
           || (CHECK_OPT_SIZE(pOpt, SIZELONG) && sizeof(long) == 8))
            sprintf(szBuf, "%" PRIu64, (uint64_t) (uintptr_t) pOpt->pDefault);
         else if(CHECK_OPT_SIZE(pOpt, SIZE32)
           || (CHECK_OPT_SIZE(pOpt, SIZEINT) && sizeof(int) == 4)
           || (CHECK_OPT_SIZE(pOpt, SIZESHORT) && sizeof(int) == 4)
           || (CHECK_OPT_SIZE(pOpt, SIZELONG) && sizeof(long) == 4))
            sprintf(szBuf, "%" PRIu32, (uint32_t) (uintptr_t) pOpt->pDefault);
         else if(CHECK_OPT_SIZE(pOpt, SIZE16)
           || (CHECK_OPT_SIZE(pOpt, SIZEINT) && sizeof(int) == 2)
           || (CHECK_OPT_SIZE(pOpt, SIZESHORT) && sizeof(int) == 2)
           || (CHECK_OPT_SIZE(pOpt, SIZELONG) && sizeof(long) == 2))
            sprintf(szBuf, "%" PRIu16, (uint16_t) (uintptr_t) pOpt->pDefault);
         else if(CHECK_OPT_SIZE(pOpt, SIZE8))
            sprintf(szBuf, "%" PRIu8, (uint8_t) (uintptr_t) pOpt->pDefault);
         else
            strcpy(szBuf, "####");

         pDef = szBuf;
         }
      else if(CHECK_OPT_TYPE(pOpt, INT))
         {
         sprintf(szBuf, "%d", (int) (intptr_t) pOpt->pDefault);

         pDef = szBuf;
         }
      else if(CHECK_OPT_TYPE(pOpt, UINT))
         {
         sprintf(szBuf, "%u", (unsigned int) (intptr_t) pOpt->pDefault);

         pDef = szBuf;
         }
      else if(CHECK_OPT_TYPE(pOpt, BOOL))
         {
         if((BOOL) (uintptr_t) (pOpt->pDefault))
            pDef = "TRUE";
         else
            pDef = "FALSE";
         }
      else if(CHECK_OPT_TYPE(pOpt, SEL))
         {
         int i;
         int j;

// Find the string first.
         for(i = 0, pTmp = (char *) pOpt->pExtra; *pTmp != '\0'
           && i != (int) (intptr_t) (pOpt->pDefault); i++, pTmp++)
            {
// Move to next string.
            for(; *pTmp != ';' && *pTmp != '\0'; pTmp++)
              ;

// If this was the last string, break now.
            if(*pTmp == '\0')
               break;
            }

/* If we found the string, then process it. */
         if(*pTmp != '\0')
            {
// Now, find the length of the string.
            for(j = 0; pTmp[j] != ';' && pTmp[j] != '\0'; j++)
              ;

            sprintf(szBuf, "%.*s", j, pTmp);
            }
         else
            strcpy(szBuf, "####");
         pDef = szBuf;
         }
      else
         pDef = NULL;

// Print the help.
      fprintf(fp, "%s%s%s%s%.1s - %s%s%s%s\n",
        pLongName == NULL ? "" : "--", pLongName == NULL ? "" : pLongName,
        (pLongName == NULL || pShortName == NULL) ? "" : " | ",
        pShortName == NULL ? "" : "-",
        pShortName == NULL ? "" : pShortName,
        pOpt->pHelp == NULL ? "" : pOpt->pHelp,
        pDef == NULL ? "" : " (",
        pDef == NULL ? "" : pDef,
        pDef == NULL ? "" : ")");
      }

   return OPT_ERR_NONE;
   } // End of ou_OptionArrayPrintHelp().


/*
 * Function: ou_OptionArrayPrintValues()
 * Print option values to a specified file handle.
 * Arguments:
 * POPTIONS pOptArray - Array of values to print.
 * FILE *fp - File to print to.
 * uint32_t nFlags - Processing flags.
 * Returns:
 * uint32_t - An error code, 0 if there was no error.
 */
uint32_t ou_OptionArrayPrintValues(POPTIONS pOptArray, FILE *fp,
  uint32_t nFlags)
   {
   POPTIONS pOpt;

// Loop through the options.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      {
      if(pOpt->pName == NULL)
         continue;

// Print only values that are in a config file, that aren't config file
// options themselves, that does not have the NOPRT flag and that doesn't
// have a name.
      if(CHECK_OPT_FLAG(pOpt, HIDDEN)
        || CHECK_OPT_TYPE(pOpt, CFGFILE)
        || CHECK_OPT_TYPE(pOpt, CFGFILEMAIN)
        || CHECK_OPT_FLAG(pOpt, HELP)
        || CHECK_OPT_FLAG(pOpt, NOPRT)
        || CHECK_OPT_FLAG(pOpt, NOCFGFILE)
        || (CHECK_OPT_FLAG(pOpt, DEFAULT)
        && CHECK_OPT_OPER_FLAG(nFlags, PRTNONDEF)))
         continue;

// Only print values that have long options.
      if(pOpt->pName[1] == '|' && pOpt->pName[2] == '\0')
         continue;

// Print help, if we are to do this.
      if(CHECK_OPT_OPER_FLAG(nFlags, PRTHELP) && pOpt->pHelp != NULL)
         fprintf(fp, "# %s\n", pOpt->pHelp);
      ou_ProcessOption(pOptArray, pOpt, -1, OPT_OPER_PRT, fp);
      fprintf(fp, "\n");
      }

   return OPT_ERR_NONE;
   } // End of ou_OptionArrayPrintValues().


/*
 * Function: ou_OptionArraySetValues()
 * Set values in array from commandline.
 * First help options are process, secondly, config file options are
 * processed, and thirdly, any other options.
 * Arguments:
 * POPTIONS pOptArray - Option array to process.
 * int *pArgc - A pointer to the original argc.
 * char **pArgv - A pointer to the original argv.
 * uint32_t nFlags - Processing flags.
 * Returns:
 * uint32_t - Errorcode, 0 if there were no errors.
 */
uint32_t ou_OptionArraySetValues(POPTIONS pOptArray, int *pArgc, char **pArgv,
  uint32_t nFlags)
   {
   int i, j;
   char *pValue;
   int nIndex;
   int nRet;
   int nConsumed;
   POPTIONS pOpt;
   BOOL bHelp = FALSE;

// First process HELP type options on the commandline.
   for(i = 1; pArgc != NULL && i < *pArgc;)
      {
// Check for long option.
      if(pArgv[i][0] == '-' && pArgv[i][1] == '-'
        && (pOpt = FindLongOption(pOptArray, &pArgv[i][2], &nIndex)) != NULL
        && CHECK_OPT_FLAG(pOpt, HELP))
         {
         pValue = GetOptionValueString(&pArgv[i][2]);
         nConsumed = 1;
         }
// If this wasn't an option we are processing, ignore this now.
      else if(pArgv[i][0] != '-' || pArgv[i][1] == '-'
        || (pOpt = FindShortOption(pOptArray, &pArgv[i], *pArgc - i,
        &pValue, &nConsumed)) == NULL || !CHECK_OPT_FLAG(pOpt, HELP))
         {
         i++;
         continue;
         }
// Process the option.
      nRet = ou_ProcessOption(pOptArray, pOpt, -1, OPT_OPER_SET | nFlags,
        pValue);

// Remove the argument from the list.
      (*pArgc) -= nConsumed;
      for(j = i; j < *pArgc; j++)
         pArgv[j] = pArgv[j + nConsumed];

// Check if there was an error.
      if(nRet != OPT_ERR_NONE)
         return PrintOptionError(nRet, pOpt, nIndex, pValue, NULL, -1);

// Flag that this option was on the commandline.
      pOpt->nFlags |= OPT_FLAG_SETONCMDLINE;

// This round we are only processing HELP options. Flag that one was found.
      bHelp = TRUE;
      }

// If one or more HELP options were found, then return right now.
   if(bHelp)
      return OPT_ERR_HELPFOUND;

// Secondly, process main config file options.
   for(i = 1; i < *pArgc;)
      {
// Check for long option.
      if(pArgv[i][0] == '-' && pArgv[i][1] == '-'
        && (pOpt = FindLongOption(pOptArray, &pArgv[i][2], &nIndex)) != NULL
        && CHECK_OPT_TYPE(pOpt, CFGFILEMAIN))
         {
         pValue = GetOptionValueString(&pArgv[i][2]);
         nConsumed = 1;
         }
// If this wasn't an option we are processing, ignore this now.
      else if(pArgv[i][0] != '-' || pArgv[i][1] == '-'
        || (pOpt = FindShortOption(pOptArray, &pArgv[i], *pArgc - i,
        &pValue, &nConsumed)) == NULL || !CHECK_OPT_TYPE(pOpt, CFGFILEMAIN))
         {
         i++;
         continue;
         }
// Check if a main config file option has already been found, if so, we have an
// error.
      if(!CHECK_OPT_FLAG(pOpt, DEFAULT))
         return PrintOptionError(OPT_ERR_MULTIMAINCFG, pOpt, nIndex, pValue,
           NULL, -1);

// If the value was NULL, then we have an error.
      if(pValue == NULL)
         return PrintOptionError(OPT_ERR_CFGFILEMAINNOVALUE, pOpt, nIndex,
           pValue, NULL, -1);

// Process the file, and make sure not to treat it as an array here.
      nRet = ou_ProcessOption(pOptArray, pOpt, nIndex,
        (OPT_OPER_SET | nFlags) & ~OPT_FLAG_CFGFILEARRAY, pValue);

// Remove the argument from the list.
      (*pArgc) -= nConsumed;
      for(j = i; j < *pArgc; j++)
         pArgv[j] = pArgv[j + nConsumed];

// Check if there was an error.
      if(nRet != OPT_ERR_NONE)
         return PrintOptionError(nRet, pOpt, nIndex, pValue, NULL, -1);

// Flag that this option was on the commandline.
      pOpt->nFlags |= OPT_FLAG_SETONCMDLINE;
      }

// If a main config file commandline option wasn't found, use the a default.
// For all main config file options, check if they have been read, and if
// not, try the defaults.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      {
      if(CHECK_OPT_TYPE(pOpt, CFGFILEMAIN) && CHECK_OPT_FLAG(pOpt, DEFAULT)
        && pOpt->pDefault != 0)
         {
// If we have an error, try the next one.
         if((nRet = ou_ProcessOption(pOptArray, pOpt, nIndex,
           OPT_OPER_SET | nFlags | OPT_FLAG_CFGFILEARRAY,
           (void *) pOpt->pDefault)) != OPT_ERR_NONE)
            {
// If the error is that the file isn't found, then ignore this and try the next.
            if(nRet != OPT_ERR_FILENOTFOUND && nRet != OPT_ERR_DEFCFGFILENF)
               return PrintOptionError(nRet, pOpt, nIndex, pValue, NULL, -1);
            }
         }
      }

// Now, check for extra config file options. There can be many of these.
   for(i = 1; i < *pArgc;)
      {
// Check for long option.
      if(pArgv[i][0] == '-' && pArgv[i][1] == '-'
        && (pOpt = FindLongOption(pOptArray, &pArgv[i][2], &nIndex)) != NULL
        && CHECK_OPT_TYPE(pOpt, CFGFILE))
         {
         pValue = GetOptionValueString(&pArgv[i][2]);
         nConsumed = 1;
         }
// If this wasn't an option we are processing, ignore this now.
      else if(pArgv[i][0] != '-' || pArgv[i][1] == '-'
        || (pOpt = FindShortOption(pOptArray, &pArgv[i], *pArgc - i,
        &pValue, &nConsumed)) == NULL || !CHECK_OPT_TYPE(pOpt, CFGFILE))
         {
         i++;
         continue;
         }

// If the value was NULL, then use the default.
      if(pValue == NULL)
         {
         if(CHECK_OPT_FLAG(pOpt, DEFREF) && pOpt->pValue != NULL)
            pValue = (char *) &(pOpt->pDefault);
         else
            pValue = (char *) pOpt->pDefault;

// Process the file(s). The default may be an array.
         nRet = ou_ProcessOption(pOptArray, pOpt, nIndex,
           OPT_OPER_SET | OPT_FLAG_CMDLINE | OPT_FLAG_CFGFILEARRAY | nFlags,
           pValue);
         }
      else
// Process the given file, this is never an array.
         nRet = ou_ProcessOption(pOptArray, pOpt, nIndex,
           (OPT_OPER_SET | OPT_FLAG_CMDLINE | nFlags) & ~OPT_FLAG_CFGFILEARRAY,
           pValue);

// Remove the argument from the list.
      (*pArgc) -= nConsumed;
      for(j = i; j < *pArgc; j++)
         pArgv[j] = pArgv[j + nConsumed];

// Check if there was an error.
      if(nRet != OPT_ERR_NONE)
         return PrintOptionError(nRet, pOpt, nIndex, pValue, NULL, -1);

// Flag that this option was on the commandline.
      pOpt->nFlags |= OPT_FLAG_SETONCMDLINE;
      }

// Handle config file options with defaults.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      {
      if(CHECK_OPT_TYPE(pOpt, CFGFILE) && CHECK_OPT_FLAG(pOpt, DEFAULT)
        && pOpt->pDefault != 0)
         {
// If we have an error, try the next one.
         if((nRet = ou_ProcessOption(pOptArray, pOpt, nIndex,
           OPT_OPER_SET | nFlags | OPT_FLAG_CFGFILEARRAY,
           (void *) pOpt->pDefault)) != OPT_ERR_NONE)
            {
// If the error is that the file isn't found, then ignore this and try the next.
            if(nRet != OPT_ERR_FILENOTFOUND && nRet != OPT_ERR_DEFCFGFILENF)
               return PrintOptionError(nRet, pOpt, nIndex, pValue, NULL, -1);
            }
         }
      }

// Now, process other command line options.
   for(i = 1; i < *pArgc;)
      {
// Check for long option.
      if(pArgv[i][0] == '-' && pArgv[i][1] == '-'
        && (pOpt = FindLongOption(pOptArray, &pArgv[i][2], &nIndex)) != NULL)
         {
         pValue = GetOptionValueString(&pArgv[i][2]);
         nConsumed = 1;
         }
// If this wasn't an option we are processing, ignore this now.
      else if(pArgv[i][0] != '-' || pArgv[i][1] == '-'
        || (pOpt = FindShortOption(pOptArray, &pArgv[i], *pArgc - i,
        &pValue, &nConsumed)) == NULL)
         {
         i++;
         continue;
         }
// Process the option.
      nRet = ou_ProcessOption(pOptArray, pOpt, nIndex,
        OPT_OPER_SET | OPT_FLAG_CMDLINE | nFlags, pValue);

// Clear the argument value, in requested.
      if(CHECK_OPT_FLAG(pOpt, PWD))
         {
         for(j = 0; pValue[j] != '\0'; j++)
            pValue[j] = 'x';
         }

// Remove the argument from the list.
      (*pArgc) -= nConsumed;
      for(j = i; j < *pArgc; j++)
         pArgv[j] = pArgv[j + nConsumed];

// Check if there was an error.
      if(nRet != OPT_ERR_NONE)
         return PrintOptionError(nRet, pOpt, nIndex, pValue, NULL, -1);

// Flag that this option was on the commandline.
      pOpt->nFlags |= OPT_FLAG_SETONCMDLINE;
      }

// Now, check if there are any other options on the the commandline.
   if(!CHECK_OPT_OPER_FLAG(nFlags, IGNUNKNOWNOPT))
      {
      for(i = 1; i < *pArgc; i++)
         {
         if(pArgv[i][0] == '-' && pArgv[i][1] == '-')
            return PrintOptionError(OPT_ERR_UNKNOWNOPTION, NULL, -1, pArgv[i], NULL, -1);
         }
      }

// Finally, check default for values that hasn't been set.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      {
      if(CHECK_OPT_FLAG(pOpt, DEFAULT))
         {
         if(CHECK_OPT_FLAG(pOpt, NODEF))
            {
            nRet = OPT_ERR_MUSTBESPECIFIED;
            return PrintOptionError(nRet, pOpt, -1, NULL, NULL, 0);
            }
         else if(CHECK_OPT_FLAG(pOpt, DEFREF))
            ou_ProcessOption(pOptArray, pOpt, -1, OPT_OPER_DEF, NULL);
         }
      }

   return OPT_ERR_NONE;
   } // End of ou_OptionArraySetValues().


/*
 * Function: ou_OptionArrayFree()
 * Free values in option array.
 * Arguments:
 * POPTIONS pOptArray - Option array to set to default.
 * Returns:
 * uint32_t - Errorcode, 0 if there were no errors.
 */
uint32_t ou_OptionArrayFree(POPTIONS pOptArray)
   {
   POPTIONS pOpt;

// Loop for all options in array.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      ou_ProcessOption(pOptArray, pOpt, -1, OPT_OPER_FREE, NULL);

   return OPT_ERR_NONE;
   } // End of ou_OptionArrayFree().


/*
 * Function: ou_OptionArrayProcessFile()
 * Process options in an optionfile.
 * Arguments:
 * POPTIONS pOptArray - Options in file to process.
 * char *pFile - File to process.
 * char *pSection - Section in file to process.
 * uint32_t nFlags - Operation flags.
 * Returns:
 * uint32_t - An error code, 0 if there was no error.
 */
uint32_t ou_OptionArrayProcessFile(POPTIONS pOptArray, char *pFile,
  char *pSection, uint32_t nFlags)
   {
   int nRet;
   POPTIONS pOpt;

// Check arguments.
   if(pFile == NULL)
      return PrintOptionError(OPT_ERR_FILENAMEMISSING, NULL, -1, NULL, NULL, 0);

// Read the config file.
   if((nRet = ReadOptionFile(pFile, pSection, pOptArray, OPT_OPER_SET | nFlags))
     != OPT_ERR_NONE)
      return PrintOptionError(nRet, NULL, -1, pFile, NULL, 0);

// Set DEFREF values to their defaults again, if they already have defaults.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      {
      if(CHECK_OPT_FLAG(pOpt, DEFAULT) && CHECK_OPT_FLAG(pOpt, DEFREF))
         ou_ProcessOption(pOptArray, pOpt, -1, OPT_OPER_DEF, NULL);
      }

   return OPT_ERR_NONE;
   } // End of ou_OptionArrayProcessFile().


/*
 * Function: ou_OptionArrayProcessFiles()
 * Process options in a set of optionfiles.
 * Arguments:
 * POPTIONS pOptArray - Options in files to process.
 * char **pFile* - Files to process.
 * char *pSection - Section in files to process.
 * uint32_t nFlags - Operation flags.
 * Returns:
 * uint32_t - An error code, 0 if there was no error.
 */
uint32_t ou_OptionArrayProcessFiles(POPTIONS pOptArray, char **pFiles,
  char *pSection, uint32_t nFlags)
   {
   int nRet;
   POPTIONS pOpt;
   int i;

// Check if there are any files to process.
   if(pFiles == NULL)
      return OPT_ERR_NONE;

// Read the files, one by one.
   for(i = 0; pFiles[i] != NULL; i++)
      {
// Read the file, if there is an error, just return.
      if((nRet = ReadOptionFile(pFiles[i], pSection, pOptArray,
        OPT_OPER_SET | nFlags)) != OPT_ERR_NONE)
         return PrintOptionError(nRet, NULL, -1, pFiles[i], NULL, 0);
      }

// Set DEFREF values to their defaults again, if they already have defaults.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      {
      if(CHECK_OPT_FLAG(pOpt, DEFAULT) && CHECK_OPT_FLAG(pOpt, DEFREF))
         ou_ProcessOption(pOptArray, pOpt, -1, OPT_OPER_DEF, NULL);
      }

   return OPT_ERR_NONE;
   } // End of ou_OptionArrayProcessFiles().


/*
 * Function: ou_ProcessOption()
 * Process a step for a specified option.
 * Arguments:
 * POPTIONS pOptArray - The array of all options.
 * POPTIONS pOpt - The option to process.
 * int nIndex - Index for indexed options.
 * uint32_t nOper - The operation to perform.
 * void *pArg - Value in most cases.
 * Returns:
 * uint32_t - En error code. 0 if no error.
 */
uint32_t ou_ProcessOption(POPTIONS pOptArray, POPTIONS pOpt, int nIndex,
  uint32_t nOper, void *pArg)
   {
   int nRet = 0;
   uint32_t (*pOptionHandler)(POPTIONS, POPTIONS, int, uint32_t, void *) = NULL;

// Figure out the function to handle to type of option.
   if(CHECK_OPT_FLAG(pOpt, INDEXED) && CHECK_OPT_TYPE(pOpt, STR))
      pOptionHandler = HandleIndexedStrOption;
   else if(CHECK_OPT_FLAG(pOpt, INDEXED) && CHECK_OPT_TYPE(pOpt, SEL))
      pOptionHandler = HandleIndexedSelectionOption;
   else if(CHECK_OPT_FLAG(pOpt, INDEXED) && CHECK_OPT_TYPE(pOpt, BOOL))
      pOptionHandler = HandleIndexedIntOption;
   else if(CHECK_OPT_FLAG(pOpt, INDEXED) && CHECK_OPT_TYPE(pOpt, INTBASE))
      pOptionHandler = HandleIndexedIntOption;
   else if(CHECK_OPT_FLAG(pOpt, INDEXED) && CHECK_OPT_TYPE(pOpt, INT))
      pOptionHandler = HandleIndexedIntOption;
   else if(CHECK_OPT_FLAG(pOpt, INDEXED) && CHECK_OPT_TYPE(pOpt, UINT))
      pOptionHandler = HandleIndexedIntOption;
   else if(CHECK_OPT_TYPE(pOpt, STR))
      {
      if(CHECK_OPT_FLAG(pOpt, ARRAY))
         pOptionHandler = HandleStringArrayOption;
      else
         pOptionHandler = HandleStringOption;
      }
   else if(CHECK_OPT_TYPE(pOpt, INTBASE))
      pOptionHandler = HandleIntOption;
   else if(CHECK_OPT_TYPE(pOpt, INT))
      pOptionHandler = HandleIntOption;
   else if(CHECK_OPT_TYPE(pOpt, UINT))
      pOptionHandler = HandleIntOption;
   else if(CHECK_OPT_TYPE(pOpt, BOOL))
      pOptionHandler = HandleBoolOption;
   else if(CHECK_OPT_TYPE(pOpt, SEL))
      {
      if(CHECK_OPT_FLAG(pOpt, ARRAY))
         pOptionHandler = HandleSelectionArrayOption;
      else
         pOptionHandler = HandleSelectionOption;
      }
   else if(CHECK_OPT_TYPE(pOpt, KEYVALUELIST))
      pOptionHandler = HandleKeyValueListOption;
   else if(CHECK_OPT_TYPE(pOpt, CFGFILE) || CHECK_OPT_TYPE(pOpt, CFGFILEMAIN))
      pOptionHandler = HandleCfgfileOption;

// Note that this is 
   if(CHECK_OPT_TYPE(pOpt, FUNC))
      {
      if((nRet = ((BOOL (*)(POPTIONS, POPTIONS, int, uint32_t, void *))
        pOpt->pValue)(pOptArray, pOpt, nIndex, nOper, pArg)) == OPT_ERR_NONE)
         {
         if(CHECK_OPT_OPER(nOper, DEF))
            SET_OPT_FLAG(pOpt, DEFAULT);
         else
            CLR_OPT_FLAG(pOpt, DEFAULT);
         }
      return nRet;
      }
   else if(CHECK_OPT_OPER(nOper, NULL))
      {
// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue != NULL)
         *((int *) (pOpt->pValue)) = 0;
      return nRet;
      }
   else if(CHECK_OPT_OPER(nOper, DEF)
     || CHECK_OPT_OPER(nOper, SET))
      {
      if((nRet = pOptionHandler(pOptArray, pOpt, nIndex, nOper, pArg))
        == OPT_ERR_NONE)
         {
         if(CHECK_OPT_OPER(nOper, DEF))
            SET_OPT_FLAG(pOpt, DEFAULT);
         else
            CLR_OPT_FLAG(pOpt, DEFAULT);
         }
      return nRet;
      }
   else if(CHECK_OPT_OPER(nOper, FREE) || CHECK_OPT_OPER(nOper, PRT))
      {
// Set output to stderr if NULL for PRT operation.
      if(CHECK_OPT_OPER(nOper, PRT) && pArg == NULL)
         pArg = stderr;

      return pOptionHandler(pOptArray, pOpt, -1, nOper, pArg);
      }
   return OPT_ERR_UNKNOWNOPER;
   } // End of ou_ProcessOption()


/*
 * Function: ou_GetOptionDefault()
 * Get the default value for a named option from a list. The long name of the
 * option is used for comparison.
 * Arguments:
 * POPTIONS pOptArray - The option list.
 * char *pOptionName - The name of the option to look for the default of.
 * Returns:
 * void * - The default value. Needs to be cast appropriately by the caller.
 *   Note that NULL is returned for unknown options, which is a valid value
 *   in some cases.
 */
void *ou_GetOptionDefault(POPTIONS pOptArray, char *pOptionName)
   {
   char *pName;

// Check arguments.
   if(pOptArray == NULL || pOptionName == NULL)
      return NULL;

   for(; !CHECK_OPT_TYPE(pOptArray, NONE); pOptArray++)
      {
      if(pOptArray->pName == NULL)
         continue;

// Check if this is the one.
      pName = pOptArray->pName[1] == '|' ? &pOptArray->pName[2]
        : pOptArray->pName;

// Compare the strings.
      if(strcasecmp(pName, pOptionName) == 0)
         return pOptArray->pDefault;
      }
   return NULL;
   } // End of ou_GetOptionDefault().


/*
 * Function: ou_IsOptionSet()
 * Check if an option is explicitly set. The name is the long option name.
 * Arguments:
 * POPTIONS pOptArray - The option list.
 * char *pOptionName - The name of the option to check if it has been set.
 * Returns:
 * BOOL TRUE - If the option has been set.
 *      FALSE - If the option hasn't been set or doesn't exist.
 */
BOOL ou_IsOptionSet(POPTIONS pOptArray, char *pOptionName)
   {
   char *pName;

// Check arguments.
   if(pOptArray == NULL || pOptionName == NULL)
      return FALSE;

   for(; !CHECK_OPT_TYPE(pOptArray, NONE); pOptArray++)
      {
      if(pOptArray->pName == NULL)
         continue;

// Check if this is the one.
      pName = pOptArray->pName[1] == '|' ? &pOptArray->pName[2]
        : pOptArray->pName;

// Check if this was the option.
      if(strcasecmp(pName, pOptionName) == 0)
         return !CHECK_OPT_FLAG(pOptArray, DEFAULT);
      }
   return FALSE;
   } // End of ou_IsOptionSet().


/*
 * Function: ou_GetOptionByName()
 * Check if an option is explicitly set. The name is the long option name.
 * Arguments:
 * POPTIONS pOptArray - The option list.
 * char *pOptionName - The name of the option to check if it has been set.
 * Returns:
 * POPTIONS - The struct describing the named option, NULL is there is an error.
 */
POPTIONS ou_GetOptionByName(POPTIONS pOptArray, char *pOptionName)
   {
   char *pName;

// Check arguments.
   if(pOptArray == NULL || pOptionName == NULL)
      return NULL;

   for(; !CHECK_OPT_TYPE(pOptArray, NONE); pOptArray++)
      {
      if(pOptArray->pName == NULL)
         continue;

// Check if this is the one.
      pName = pOptArray->pName[1] == '|' ? &pOptArray->pName[2]
        : pOptArray->pName;

// Check if this was the option.
      if(strcasecmp(pName, pOptionName) == 0)
         return pOptArray;
      }
   return NULL;
   } // End of ou_GetOptionByName().


// Public function for dealing with indexed options.
/*
 * Function: ou_GetIndexedOptString()
 * Get a string from a set of numbered option strings.
 * Arguments:
 * PINDEXEDOPT *pArray - Array of pointers to structs with strings.
 * int32_t nId - Id of string in array.
 * Returns:
 * char * NULL - If the value is NULL of the string isn't found.
 * char * - The string.
 */
char *ou_GetIndexedOptString(PINDEXEDOPT *pArray, int32_t nId)
   {
   uint32_t i;

   if(pArray != NULL)
      {
      for(i = 0; pArray[i] != NULL; i++)
         {
         if(pArray[i]->nId == nId)
            return (char *) (intptr_t) pArray[i]->nValue;
         }
      }
   return NULL;
   } // End of ou_GetIndexedOptString().


/*
 * Function: ou_GetIndexedOptInt()
 * Get an int from a set of numbered option integers.
 * Arguments:
 * PINDEXEDOPT *pArray - Array of pointers to structs with ints.
 * int32_t nId - Id of string in array.
 * Returns:
 * int64_t - The integer value
 */
int64_t ou_GetIndexedOptInt(PINDEXEDOPT *pArray, int32_t nId)
   {
   uint32_t i;

   if(pArray != NULL && *pArray != NULL)
      {
      for(i = 0; pArray[i] != NULL; i++)
         {
         if(pArray[i]->nId == nId)
            return pArray[i]->nValue;
         }
      }
   return -1;
   } // End of ou_GetIndexedOptInt().


/*
 * Function: ou_GetIndexedOpt()
 * Get the indexed option strunct for an index in an array.
 * Arguments:
 * PINDEXEDOPT *pArray - Array of pointers to structs.
 * int32_t nId - Id of struct to check if set.
 * Returns:
 */
PINDEXEDOPT ou_GetIndexedOpt(PINDEXEDOPT *pArray, int32_t nId)
   {
   uint32_t i;

   if(pArray != NULL && *pArray != NULL)
      {
      for(i = 0; pArray[i] != NULL; i++)
         {
         if(pArray[i]->nId == (int) nId)
            return pArray[i];
         }
      }
   return NULL;
   } // End of ou_GetIndexedOpt().


/*
 * Function: ou_IsIndexedOptSet()
 * Check if a numbered option is set.
 * Arguments:
 * PINDEXEDOPT *pArray - Array of pointers to structs.
 * int32_t nId - Id of struct to check if set.
 * Returns:
 * BOOL TRUE - If the ID is set.
 *      FALSE - If it is not.
 */
BOOL ou_IsIndexedOptSet(PINDEXEDOPT *pArray, int32_t nId)
   {
   uint32_t i;

   if(pArray != NULL && *pArray != NULL)
      {
      for(i = 0; pArray[i] != NULL; i++)
         {
         if(pArray[i]->nId == (int) nId)
            return TRUE;
         }
      }
   return FALSE;
   } // End of ou_IsIndexedOptSet().


/*
 * Function: ou_GetIndexedOptCount()
 * Count the # of options in a numbered option list.
 * Arguments:
 * PINDEXEDOPT pArray - The array to count occurences in.
 * Returns:
 * uint32_t - The # of occurences.
 */
uint32_t ou_GetIndexedOptCount(PINDEXEDOPT *pArray)
   {
   uint32_t i;

// Check if array even exists first.
   if(pArray == NULL || *pArray == NULL)
      return 0;

   for(i = 0; pArray[i] != NULL; i++)
      ;
   return i;
   } // End of ou_GetIndexedOptCount().


/*
 * Function: ou_GetOptionName()
 * Get the name of an option.
 * Arguments:
 * POPTIONS pOpt - The option to get the name of.
 * int32_t nIndex - The index of an indexed option. If -1 use the string <n>
 *  instead of the index.
 * Returns:
 * char *pName - The name, NULL if not found or there is an error.
 */
char *ou_GetOptionName(POPTIONS pOpt, int32_t nIndex)
   {
   static char szShortName[2];
   static char *pLongNameBuf = NULL;
   static unsigned int nLongNameBuf = 0;
   int i;

// If a NULL option, then we're in bad luck.
   if(pOpt == NULL || pOpt->pName == NULL)
      return NULL;

// If this option has a short name only, then handle this now.
   if(pOpt->pName[1] == '|' && pOpt->pName[2] == '\0')
      {
      szShortName[0] = pOpt->pName[0];
      szShortName[1] = '\0';
      return szShortName;
      }
// If this is a non-indexed option, then this is easy, do it now.
   else if(!CHECK_OPT_FLAG(pOpt, INDEXED))
      {
      if(pOpt->pName[1] == '|')
         return &pOpt->pName[2];
      return pOpt->pName;
      }

// Now, handle indexed options. These cannot have a short name.
// Make sure there is place in the buffer for a 32-bit int, and if not,
// allocate more.
   if(strlen(pOpt->pName) + 11 > nLongNameBuf)
      {
      if((pLongNameBuf = (char *) realloc(pLongNameBuf,
        strlen(pOpt->pName) + 4)) == NULL)
         return NULL;
      nLongNameBuf = strlen(pOpt->pName) + 4;
      }

// Skip to index in an indexed option.
   for(i = 0; pOpt->pName[i] != ' ' && pOpt->pName[i] != '\t' && pOpt->pName[i] != '\0'; i++)
      ;

// Copy the leading part of the name.
   strncpy(pLongNameBuf, pOpt->pName, i);
   pLongNameBuf[i] = '\0';

// Add the index or the indicator.
   if(nIndex == -1)
      strcat(pLongNameBuf, "<n>");
   else
      sprintf(&pLongNameBuf[strlen(pLongNameBuf)], "%" PRId32, nIndex);

// Now, skip to the trailing part.
   for(; pOpt->pName[i] == ' ' || pOpt->pName[i] == '\t'; i++)
      ;

// And copy it to the name buffer.
   strcat(pLongNameBuf, &pOpt->pName[i]);

// And then return this thingy.
   return pLongNameBuf;
   } // End of ou_GetOptionName().


// Helper functions to handle different option types.
/*
 * HandleStringOption()
 * Handle options of the OPT_TYPE_STR type.
 * Arguments:
 * POPTIONS pOptArray - The list of all options.
 * POPTIONS pOpt - Option to process.
 * int nIndex - Index for indexed options.
 * uint32_t nOper - Operation to process.
 * void *pArg - Optional extra argument used for some options and operations.
 * Returns:
 * uint32_t - En error code. 0 if no error.
 */
uint32_t HandleStringOption(POPTIONS pOptArray, POPTIONS pOpt, int nIndex,
  uint32_t nOper, void *pArg)
   {
   if(CHECK_OPT_OPER(nOper, DEF))
      {
// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

// Free current value.
      if(*((char **) pOpt->pValue) != NULL)
         free(*((char **) pOpt->pValue));
      *((char **) pOpt->pValue) = NULL;

// If the default isn't 0, then set it.
      if(pOpt->pDefault != NULL)
         {
         if(CHECK_OPT_FLAG(pOpt, DEFREF))
            {
            if(*((char **) pOpt->pDefault) == NULL)
               *((char **) pOpt->pValue) = NULL;
            else
               *((char **) pOpt->pValue)
                 = (char *) strdup((char *) *((char **) pOpt->pDefault));
            }
         else
            *((char **) pOpt->pValue)
              = (char *) strdup((char *) pOpt->pDefault);

// Check that we could allocate this data.
         if(((char **) pOpt->pValue) == NULL)
            return OPT_ERR_MALLOC;
         }
      else
         *((char **) pOpt->pValue) = NULL;
      }
   else if(CHECK_OPT_OPER(nOper, SET))
      {
      char *pStr = (char *) pArg;

// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

      if(*((char **) pOpt->pValue) != NULL)
         {
         free(*((char **) pOpt->pValue));
         *((char **) pOpt->pValue) = NULL;
         }
      if(pStr == NULL)
         {
         if(CHECK_OPT_FLAG(pOpt, NULLEMPTY))
            {
            if((*((char **) pOpt->pValue) = strdup("")) == NULL)
               return OPT_ERR_MALLOC;
            }
         else if(CHECK_OPT_FLAG(pOpt, NONULL))
            return OPT_ERR_NULLNOTVALID;
         else
            *((char **) pOpt->pValue) = NULL;
         }
      else
         {
         if((*((char **) pOpt->pValue) = TranslateString(pStr)) == NULL)
            return OPT_ERR_INVALIDSTRING;
         }
      }
   else if(CHECK_OPT_OPER(nOper, PRT))
      {
      FILE *fp = (FILE *) pArg;
      char *pTmp;

      if(pOpt->pValue == NULL || *((char **) pOpt->pValue) == NULL)
         fprintf(fp, "%s = ", ou_GetOptionName(pOpt, -1));
      else
         {
         if((pTmp = UntranslateString(*((char **) pOpt->pValue))) == NULL)
            return OPT_ERR_MALLOC;

         fprintf(fp, "%s = %s", ou_GetOptionName(pOpt, -1), pTmp);
         free(pTmp);
         }
      }
   else if(CHECK_OPT_OPER(nOper, FREE) && *((char **) pOpt->pValue) != NULL)
      {
      free(*((char **) pOpt->pValue));
      *((char **) pOpt->pValue) = NULL;
      }
   return OPT_ERR_NONE;
   } // End of HandleStringOption().


/*
 * HandleIntOption()
 * Handle options of the OPT_TYPE_INT and OPT_TYPE_UINT type.
 * Arguments:
 * POPTIONS pOptArray - The list of all options.
 * POPTIONS pOpt - Option to process.
 * int nIndex - Index for indexed options.
 * uint32_t nOper - Operation to process.
 * void *pArg - Optional extra argument used for some options and operations.
 * Returns:
 * uint32_t - En error code. 0 if no error.
 */
uint32_t HandleIntOption(POPTIONS pOptArray, POPTIONS pOpt, int nIndex,
  uint32_t nOper, void *pArg)
   {
   int nRet;
   uint64_t nValue;

   if(CHECK_OPT_OPER(nOper, DEF))
      {
// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

// If this is an array and the array exists, then clear it up now.
      if(CHECK_OPT_FLAG(pOpt, ARRAY) && *((int64_t **) pOpt->pValue) != NULL)
         {
         free(*((int64_t **) pOpt->pValue));
         *((int64_t **) pOpt->pValue) = NULL;
         }

// Get the value.
// Note that on 32-bit systems, as pDefaults is a void * and hence a 32-bit
// value, we will have issues with a 64-bit initializer here in the case of
// non-DEFREF defaults.
      if(CHECK_OPT_FLAG(pOpt, DEFREF))
         nValue = *((uintptr_t *) pOpt->pDefault);
      else
         nValue = (uintptr_t) pOpt->pDefault;

// Now, based on the size on the int, do the right thing.
      if(CHECK_OPT_FLAG(pOpt, ARRAY))
         ou_AddIntegerToArray(nValue, GET_OPT_BYTES(pOpt), pOpt->pValue);
      else if(CHECK_OPT_8BYTE(pOpt))
         *((uint64_t *) pOpt->pValue) = (uint64_t) nValue;
      else if(CHECK_OPT_4BYTE(pOpt))
         *((uint32_t *) pOpt->pValue) = (uint32_t) nValue;
      else if(CHECK_OPT_2BYTE(pOpt))
         *((uint16_t *) pOpt->pValue) = (uint16_t) nValue;
      else if(CHECK_OPT_1BYTE(pOpt))
         *((uint8_t *) pOpt->pValue) = nValue;
      else
         return OPT_ERR_UNKNOWNINTSIZE;
      }
   else if(CHECK_OPT_OPER(nOper, SET))
      {
      char *pStr = (char *) pArg;
      int64_t nValue;

// If the value pointer is NULL, then we ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

// If the value is NULL, we are on the commandline and the CLNOARG1
// is active, then handle that.
      if(pStr == NULL && (nOper & OPT_FLAG_CMDLINE) == OPT_FLAG_CMDLINE
        && CHECK_OPT_FLAG(pOpt, CLNOARG1))
         pStr = "1";

// Set the value.
      if((nRet = IntFromString(pStr, &nValue, pOpt->nFlags))
        != OPT_ERR_NONE)
         return nRet;

      if(CHECK_OPT_FLAG(pOpt, ARRAY))
         {
// If default values are set, then remove that.
         if(CHECK_OPT_FLAG(pOpt, DEFAULT) && *((int64_t **) pOpt->pValue) != NULL)
            {
            free(*((int64_t **) pOpt->pValue));
            *((int64_t **) pOpt->pValue) = NULL;
            }

         ou_AddIntegerToArray((uint64_t) nValue, GET_OPT_BYTES(pOpt), pOpt->pValue);
         }
      else if(CHECK_OPT_8BYTE(pOpt))
        *((int64_t *) pOpt->pValue) = nValue;
      else if(CHECK_OPT_4BYTE(pOpt))
        *((int32_t *) pOpt->pValue) = nValue;
      else if(CHECK_OPT_2BYTE(pOpt))
        *((int16_t *) pOpt->pValue) = nValue;
      else if(CHECK_OPT_1BYTE(pOpt))
        *((int8_t *) pOpt->pValue) = nValue;
      }
   else if(CHECK_OPT_OPER(nOper, PRT))
      {
      if(CHECK_OPT_FLAG(pOpt, ARRAY))
         {
         int64_t nValue;
         int i;

         for(i = 0; (nValue = GetIntegerArrayValue(pOpt, i)) != -1; i++)
            {
            fprintf((FILE *) pArg, "%s%s = ", i > 0 ? "\n" : "", ou_GetOptionName(pOpt, -1));
            PrintIntegerValue((FILE *) pArg, GET_OPT_BYTES(pOpt), CHECK_OPT_FLAG(pOpt, UNSIGNED), nValue);
            }
         }
      else
         {
         fprintf((FILE *) pArg, "%s = ", ou_GetOptionName(pOpt, -1));
         PrintIntegerValue((FILE *) pArg, GET_OPT_BYTES(pOpt), CHECK_OPT_FLAG(pOpt, UNSIGNED), *((int64_t *) pOpt->pValue));
         }
      }
   else if(CHECK_OPT_OPER(nOper, FREE) && CHECK_OPT_FLAG(pOpt, ARRAY) && *((int64_t **) pOpt->pValue) != NULL)
      {
      free(*((int64_t **) pOpt->pValue));
      *((int64_t **) pOpt->pValue) = NULL;
      }

   return OPT_ERR_NONE;
   } // End if HandleIntOption().


/*
 * Function: PrintIntegerValue()
 * Print the value of an integer.
 * Arguments:
 * FILE *fp - File pointer to where to write.
 * int nBytes - # of bytes in value.
 * BOOL bUnsigned - Flag if unsigned.
 * int64_t nValue - The value to print.
 */
void PrintIntegerValue(FILE *fp, int nBytes, BOOL bUnsigned, int64_t nValue)
   {
   if(nBytes == 8 && bUnsigned)
      fprintf(fp, "%" PRIu64, (uint64_t) nValue);
   else if(nBytes == 8 && !bUnsigned)
      fprintf(fp, "%" PRId64, (int64_t) nValue);
   else if(nBytes == 4 && bUnsigned)
      fprintf(fp, "%" PRIu32, (uint32_t) nValue);
   else if(nBytes == 4 && !bUnsigned)
      fprintf(fp, "%" PRId32, (int32_t) nValue);
   else if(nBytes == 2 && bUnsigned)
      fprintf(fp, "%" PRIu16, (uint16_t) nValue);
   else if(nBytes == 2 && !bUnsigned)
      fprintf(fp, "%" PRId16, (int16_t) nValue);
   else if(nBytes == 1 && bUnsigned)
      fprintf(fp, "%" PRIu8, (uint8_t) nValue);
   else if(nBytes == 1 && !bUnsigned)
      fprintf(fp, "%" PRId8, (int8_t) nValue);

   return;
   } // End of PrintIntegerValue().


/*
 * HandleBoolOption()
 * Handle options of the OPT_TYPE_BOOL type.
 * Arguments:
 * POPTIONS pOptArray - The list of all options.
 * POPTIONS pOpt - Option to process.
 * int nIndex - Index for indexed options.
 * uint32_t nOper - Operation to process.
 * void *pArg - Optional extra argument used for some options and operations.
 * Returns:
 * uint32_t - En error code. 0 if no error.
 */
uint32_t HandleBoolOption(POPTIONS pOptArray, POPTIONS pOpt, int nIndex,
  uint32_t nOper, void *pArg)
   {
   int nRet;
   int64_t bValue;

   if(CHECK_OPT_OPER(nOper, DEF))
      {
// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

      if(CHECK_OPT_FLAG(pOpt, DEFREF))
         bValue = *((BOOL *) pOpt->pDefault);
      else
         bValue = (BOOL) (intptr_t) pOpt->pDefault;

      *((BOOL *) pOpt->pValue) = CHECK_OPT_FLAG(pOpt, REVERSE) ? !((int) bValue) : bValue;
      }
   else if(CHECK_OPT_OPER(nOper, SET))
      {
      char *pStr = (char *) pArg;

// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

// Set the value.
      if((nRet = IntFromString(pStr, (int64_t *) &bValue, pOpt->nFlags))
        != OPT_ERR_NONE)
         return nRet;

      *((BOOL *) pOpt->pValue) = CHECK_OPT_FLAG(pOpt, REVERSE) ? !((int) bValue) : bValue;
      }
   else if(CHECK_OPT_OPER(nOper, PRT))
      {
      FILE *fp = (FILE *) pArg;

      fprintf(fp, "%s = %s", ou_GetOptionName(pOpt, -1),
       (*((BOOL *) (pOpt->pValue))) ? "TRUE" : "FALSE");
      }
   return OPT_ERR_NONE;
   } // End if HandleBoolOption().


/*
 * HandleSelectionOption()
 * Handle options of the OPT_TYPE_SEL type.
 * Arguments:
 * POPTIONS pOptArray - The list of all options.
 * POPTIONS pOpt - Option to process.
 * int nIndex - Index for indexed options.
 * uint32_t nOper - Operation to process.
 * void *pArg - Optional extra argument used for some options and operations.
 * Returns:
 * uint32_t - En error code. 0 if no error.
 */
uint32_t HandleSelectionOption(POPTIONS pOptArray, POPTIONS pOpt, int nIndex,
  uint32_t nOper, void *pArg)
   {
   if(CHECK_OPT_OPER(nOper, DEF))
      {
// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

      if(CHECK_OPT_FLAG(pOpt, DEFREF))
         *((int *) pOpt->pValue) = *((int *) pOpt->pDefault);
      else
         *((int *) pOpt->pValue) = (int) (uintptr_t) pOpt->pDefault;
      }
   else if(CHECK_OPT_OPER(nOper, SET))
      {
      char *pStr = (char *) pArg;
      char *pTmp;
      int i;

// Check for null value.
      if(pStr == NULL)
         return OPT_ERR_NULLNOTVALID;

      for(i = 0, pTmp = (char *) pOpt->pExtra; *pTmp != '\0'; i++, pTmp++)
         {
// Ignore (and skip past) empty value names.
         if(*pTmp == ';')
            continue;

// Check if this was the option.
         if(strncasecmp(pStr, pTmp, strlen(pStr)) == 0)
            break;

// Skip to next string.
         for(; *pTmp != ';' && *pTmp != '\0'; pTmp++)
           ;
// If this was the last string, break now.
         if(*pTmp == '\0')
            return OPT_ERR_UNKNOWNVALUE;
         }

// If the value pointer is NULL, then don't do this.
      if(pOpt->pValue != NULL)
         *((unsigned int *) pOpt->pValue) = i;
      }
   else if(CHECK_OPT_OPER(nOper, PRT))
      {
      FILE *fp = (FILE *) pArg;
      char *pTmp;
      int i;
      int j;

// Find the string first.
      for(i = 0, pTmp = (char *) pOpt->pExtra; *pTmp != '\0'
        && i != *((int *) pOpt->pValue); i++, pTmp++)
         {
// Move to next string.
         for(; *pTmp != ';' && *pTmp != '\0'; pTmp++)
           ;

// If this was the last string, break now.
         if(*pTmp == '\0')
            return OPT_ERR_UNKNOWNVALUE;
         }

// Now, find the length of the string.
      for(j = 0; pTmp[j] != ';' && pTmp[j] != '\0'; j++)
        ;
      fprintf(fp, "%s = %.*s", ou_GetOptionName(pOpt, -1), j, pTmp);
      }
   return OPT_ERR_NONE;
   } // End of HandleSelectionOption().


/*
 * HandleStringArrayOption()
 * Handle options of the OPT_TYPE_STRARRAY type.
 * Arguments:
 * POPTIONS pOptArray - The list of all options.
 * POPTIONS pOpt - Option to process.
 * int nIndex - Index for indexed options.
 * uint32_t nOper - Operation to process.
 * void *pArg - Optional extra argument used for some options and operations.
 * Returns:
 * uint32_t - En error code. 0 if no error.
 */
uint32_t HandleStringArrayOption(POPTIONS pOptArray, POPTIONS pOpt, int nIndex,
  uint32_t nOper, void *pArg)
   {
// Set default value for array.
   if(CHECK_OPT_OPER(nOper, DEF))
      {
      char **ppTmp;
      int i;

// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

// Free current value, if there is one.
      if(*((char ***) pOpt->pValue) != NULL)
         {
// Free each string in the list.
         for(i = 0; (*((char ***) pOpt->pValue))[i] != NULL; i++)
            free((*((char ***) pOpt->pValue))[i]);

// Free the list itself.
         free(*((char ***) pOpt->pValue));
         *((char ***) pOpt->pValue) = NULL;
         }

// If default is a reference, then copy all values from that.
      if(CHECK_OPT_FLAG(pOpt, DEFARRAY))
         {
         if(((char **) pOpt->pDefault) != NULL
           && *((char **) pOpt->pDefault) != NULL)
            {
            for(ppTmp = ((char **) pOpt->pDefault); *ppTmp != NULL;
              ppTmp++)
               {
               if(ou_AddStringToArray(*ppTmp, (char ***) pOpt->pValue))
                  return OPT_ERR_MALLOC;
               }
            }
         }
// Else if this is a reference to a constant string.
      else if(CHECK_OPT_FLAG(pOpt, DEFREF)) 
         {
         if((char **) pOpt->pDefault != NULL
           && *((char **) pOpt->pDefault) != NULL)
            {
            if(ou_AddStringToArray(*((char **) pOpt->pDefault),
              (char ***) pOpt->pValue))
               return OPT_ERR_MALLOC;
            }
         }
// Else this is a constant string.
      else if(((char *) pOpt->pDefault) != NULL)
         {
         if(ou_AddStringToArray(((char *) pOpt->pDefault),
           (char ***) pOpt->pValue))
            return OPT_ERR_MALLOC;
         }
      }
// Set array element.
   else if(CHECK_OPT_OPER(nOper, SET))
      {
      char *pStr = (char *) pArg;
      char *pTmp;
      int i;

// Check the string.
      if(pStr == NULL && !CHECK_OPT_FLAG(pOpt, NULLEMPTY))
         return OPT_ERR_NULLNOTVALID;

// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

// If the string is NULL and we got this far, then we need to set it to the
// empty string instead.
      if(pStr == NULL)
         pStr = "";

// If default value is set, then remove those.
      if(CHECK_OPT_FLAG(pOpt, DEFAULT))
         {
         if(*((char ***) pOpt->pValue) != NULL)
            {
// Free each string in the list.
            for(i = 0; (*((char ***) pOpt->pValue))[i] != NULL; i++)
               free((*((char ***) pOpt->pValue))[i]);

// Free the list itself.
            free(*((char ***) pOpt->pValue));
            *((char ***) pOpt->pValue) = NULL;
            }
         }

// Note that empty strings are OK at this stage.
      if((pTmp = TranslateString(pStr)) == NULL)
         return OPT_ERR_INVALIDSTRING;
      if(ou_AddStringToArray(pTmp, (char ***) pOpt->pValue))
         {
         free(pTmp);
         return OPT_ERR_MALLOC;
         }
      free(pTmp);
      }
// Print array value.
   else if(CHECK_OPT_OPER(nOper, PRT))
      {
      FILE *fp = (FILE *) pArg;
      char *pTmp;
      int i;

      if(pOpt->pValue != NULL && *((char ***) (pOpt->pValue)) != NULL)
         {
         for(i = 0; (*((char ***) pOpt->pValue))[i] != NULL; i++)
            {
            if((pTmp = UntranslateString((*((char ***) pOpt->pValue))[i]))
              == NULL)
               return OPT_ERR_MALLOC;

            fprintf(fp, "%s%s = %s", i == 0 ? "" : "\n",
              ou_GetOptionName(pOpt, -1), pTmp);
            free(pTmp);
            }
         }
      else
         fprintf(fp, "# %s =", ou_GetOptionName(pOpt, -1));
      }
// Free string and list of strings.
   else if(CHECK_OPT_OPER(nOper, FREE) && pOpt->pValue != NULL
     && *((char ***) pOpt->pValue) != NULL)
      {
      int i;

// Free each string in the list.
      for(i = 0; (*((char ***) pOpt->pValue))[i] != NULL; i++)
         free((*((char ***) pOpt->pValue))[i]);

// Free the list itself.
      free(*((char ***) pOpt->pValue));
      *((char ***) pOpt->pValue) = NULL;
      }
   return OPT_ERR_NONE;
   } // End of HandleStringArrayOption().


/*
 * HandleSelectionArrayOption()
 * Handle options of the OPT_TYPE_SELARRAY type.
 * Arguments:
 * POPTIONS pOptArray - The list of all options.
 * POPTIONS pOpt - Option to process.
 * int nIndex - Index for indexed options.
 * uint32_t nOper - Operation to process.
 * void *pArg - Optional extra argument used for some options and operations.
 * Returns:
 * uint32_t - En error code. 0 if no error.
 */
uint32_t HandleSelectionArrayOption(POPTIONS pOptArray, POPTIONS pOpt,
  int nIndex, uint32_t nOper, void *pArg)
   {
   if(CHECK_OPT_OPER(nOper, DEF))
      {
// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

// Get the default value.
      if(CHECK_OPT_FLAG(pOpt, DEFREF) && CHECK_OPT_FLAG(pOpt, DEFARRAY))
         {
         int i;

         for(i = 0; pOpt->pDefault != NULL && ((int *) pOpt->pDefault)[i] != -1;
           i++)
            ou_AddIntegerToArray((int64_t) ((int *) pOpt->pDefault)[i], sizeof(int), pOpt->pValue);
         }
      else if(CHECK_OPT_FLAG(pOpt, DEFREF))
         {
         if(((int *) pOpt->pDefault) != NULL)
            ou_AddIntegerToArray((int64_t) *((int *) pOpt->pDefault), sizeof(int), pOpt->pValue);
         }
      else if(!CHECK_OPT_FLAG(pOpt, DEFARRAY))
         ou_AddIntegerToArray((int64_t) (uintptr_t) pOpt->pDefault, sizeof(int), pOpt->pValue);
      else
         return OPT_ERR_UNKNOWNFLAG;
      }
// Set array element.
   else if(CHECK_OPT_OPER(nOper, SET))
      {
      char *pStr = (char *) pArg;
      char *pTmp;
      BOOL bRemove;
      int i, j, k;

// If the default is set, then remove them.
      if(CHECK_OPT_FLAG(pOpt, DEFAULT) && *((int **) pOpt->pValue) != NULL)
         {
         free(*((int **) pOpt->pValue));
         *((int **) pOpt->pValue) = NULL;
         }

// Loop for all values in string.
      for(j = 0; pStr[j] != '\0'; j++)
         {
// Skip past leading blanks.
         for(; pStr[j] == ' ' || pStr[j] == '\t'; j++)
            ;

// Check if we are to remove a value.
         if(pStr[j] == '!')
            {
            bRemove = TRUE;

// Skip past blanks.
            for(++j; pStr[j] == ' ' || pStr[j] == '\t'; j++)
               ;
            }
         else
            bRemove = FALSE;

// Find length of value.
         for(k = j; pStr[k] != ' ' && pStr[k] != ';' && pStr[k] != ',' &&
           pStr[k] != '\0'; k++)
            ;

// Find the string among possible values.
         for(i = 0, pTmp = (char *) pOpt->pExtra; *pTmp != '\0'; i++, pTmp++)
            {
// Check that the length and the name matches.
            if((pTmp[k -j] == ';' || pTmp[k -j] == '\0')
              && strncasecmp(&pStr[j], pTmp, k - j) == 0)
               {
// If the value pointer is NULL, then don't do this.
               if(pOpt->pValue != NULL)
                  {
                  if(bRemove)
                     {
                     if(*((int **) pOpt->pValue) != NULL)
                        RemoveIntegerFromArray(i, (int **) pOpt->pValue);
                     }
                  else
                     ou_AddIntegerToArray((int64_t) i, sizeof(int), pOpt->pValue);
                  }
               break;
               }

// Skip to next string.
            for(; *pTmp != ';' && *pTmp != '\0'; pTmp++)
              ;

// If this was the last string, break now.
            if(*pTmp == '\0')
               return OPT_ERR_UNKNOWNVALUE;
            }
// Skip past trailing blanks.
         for(; pStr[k] != ',' && pStr[k] != ';' && pStr[k] != '\0'; k++)
            ;
 
// Break if we hit a trailing NULL.
         if(pStr[k] == '\0')
            break;
         j = k;
         }
      }
// Print array value.
   else if(CHECK_OPT_OPER(nOper, PRT))
      {
      char *pTmp;
      FILE *fp = (FILE *) pArg;
      int i, j, k;
      BOOL bFirst = TRUE;

      if(pOpt->pValue != NULL && *((int **) (pOpt->pValue)) != NULL)
         {
         for(i = 0; (*((int **) pOpt->pValue))[i] != -1; i++)
            {
// Find the string first.
            for(j = 0, pTmp = (char *) pOpt->pExtra; *pTmp != '\0'
              && j != (*((int **) pOpt->pValue))[i]; j++, pTmp++)
               {
// Move to next string.
               for(; *pTmp != ';' && *pTmp != '\0'; pTmp++)
                 ;

// If this was the last string, break now.
               if(*pTmp == '\0')
                  return OPT_ERR_UNKNOWNVALUE;
               }

// Now, find the length of the string.
            for(k = 0; pTmp[k] != ';' && pTmp[k] != '\0'; k++)
              ;
            if(bFirst)
               fprintf(fp, "%s = %.*s", ou_GetOptionName(pOpt, -1), k, pTmp);
            else
               fprintf(fp, ",%.*s", k, pTmp);
            bFirst = FALSE;
            }
         }
      else
         fprintf(fp, "# %s =", ou_GetOptionName(pOpt, -1));
      }
// Free string and list of strings.
   else if(CHECK_OPT_OPER(nOper, FREE) && *((int **) pOpt->pValue) != NULL)
      {
// Free the list.
      free(*((int **) pOpt->pValue));
      *((int **) pOpt->pValue) = NULL;
      }
   return OPT_ERR_NONE;
   } // End of HandleSelectionArrayOption().


/*
 * HandleKeyValueListOption()
 * Handle options of the OPT_TYPE_KEYVALUEARRAY type.
 * Arguments:
 * POPTIONS pOptArray - The list of all options.
 * POPTIONS pOpt - Option to process.
 * int nIndex - Index for indexed options.
 * uint32_t nOper - Operation to process.
 * void *pArg - Optional extra argument used for some options and operations.
 * Returns:
 * uint32_t - En error code. 0 if no error.
 */
uint32_t HandleKeyValueListOption(POPTIONS pOptArray, POPTIONS pOpt, int nIndex,
  uint32_t nOper, void *pArg)
   {
// Set default value for array.
   if(CHECK_OPT_OPER(nOper, DEF))
      {
      PKEYVALUE pKeyValue;
      PKEYVALUE pNext;

      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

      for(pKeyValue = *((PKEYVALUE *) pOpt->pValue); pKeyValue != NULL; pKeyValue = pNext)
         {
         pNext = pKeyValue->pNext;
         if(pKeyValue->pValue != NULL)
            free(pKeyValue->pValue);
         free(pKeyValue);
         }

      *((PKEYVALUE *) pOpt->pValue) = NULL;
      }
// Set key value pair.
   else if(CHECK_OPT_OPER(nOper, SET))
      {
      char *pStr = (char *) pArg;
      char *pKey;
      char *pValue;
      char *pTmp;
      PKEYVALUE pKeyValue;

// Check that the keyvalue pair i'n NULL.
      if(pStr == NULL)
         return OPT_ERR_KEYVALUENULL;

// Extract the key and value and check them.
      for(pKey = pStr; *pKey == ' ' || *pKey == '\t'; pKey++)
         ;

      if(*pKey == '\0' || *pKey == '=')
         return OPT_ERR_KEYVALUESPACE;

// Allocate a temp space for the key.
      if((pKey = strdup(pKey)) == NULL)
         return OPT_ERR_MALLOC;

      for(pValue = pKey; *pValue != '=' && *pValue != '\0' && *pValue != ' ' && *pValue != '\t'; pValue++)
         ;

      if(*pValue == ' ' || *pValue == '\t')
         {
         *pValue++ = '\0';

         for(; *pValue == ' ' || *pValue == '\t'; pValue++)
            ;
         }

      if(*pValue == '=')
         *pValue++ = '\0';
      else
         {
         free(pKey);
         return OPT_ERR_KEYVALUEFORMAT;
         }

// Skip past spaces after equal sign.
      for(; *pValue == ' ' || *pValue == '\t'; pValue++)
         ;

// Check the string.
      if(*pValue == '\0' && CHECK_OPT_FLAG(pOpt, NONULL))
         {
         free(pKey);
         return OPT_ERR_NULLNOTVALID;
         }

// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         {
         free(pKey);
         return OPT_ERR_NONE;
         }

// Note that empty strings are OK at this stage.
      if(*pValue == '\0' && !CHECK_OPT_FLAG(pOpt, NULLEMPTY))
         pTmp = NULL;
      else if((pTmp = TranslateString(pValue)) == NULL)
         {
         free(pKey);
         return OPT_ERR_INVALIDSTRING;
         }

// Check if the key already exists.
      for(pKeyValue = *((PKEYVALUE *) pOpt->pValue); pKeyValue != NULL; pKeyValue = pKeyValue->pNext)
         {
         if(strcmp(pKeyValue->pKey, pKey) == 0)
            {
            if(pKeyValue->pValue != NULL)
               free(pKeyValue->pValue);

            pKeyValue->pValue = pTmp;
            free(pKey);

            return OPT_ERR_NONE;
            }
         }

// Allocate space for the key value struct.
      if((pKeyValue = malloc(sizeof(KEYVALUE) + strlen(pKey) + 1)) == NULL)
         {
         free(pKey);
         return OPT_ERR_MALLOC;
         }

      pKeyValue->pKey = ((char *) pKeyValue) + sizeof(KEYVALUE);
      strcpy(pKeyValue->pKey, pKey);
      pKeyValue->pValue = pTmp;
      pKeyValue->pNext = *((PKEYVALUE *) pOpt->pValue);
      *((PKEYVALUE *) pOpt->pValue) = pKeyValue;
      free(pKey);
      }
// Print array value.
   else if(CHECK_OPT_OPER(nOper, PRT))
      {
      FILE *fp = (FILE *) pArg;
      char *pTmp;
      PKEYVALUE pKeyValue;
      int i;

      for(i = 0, pKeyValue = *((PKEYVALUE *) pOpt->pValue); pKeyValue != NULL; i++, pKeyValue = pKeyValue->pNext)
         {
         if((pTmp = UntranslateString(pKeyValue->pValue)) == NULL)
            return OPT_ERR_MALLOC;

         fprintf(fp, "%s%s = %s=%s", i == 0 ? "" : "\n",
           ou_GetOptionName(pOpt, -1), pKeyValue->pKey, pTmp);
         free(pTmp);
         }
      }
// Free string and list of strings.
   else if(CHECK_OPT_OPER(nOper, FREE))
      {
      PKEYVALUE pKeyValue;
      PKEYVALUE pNext;

      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

      for(pKeyValue = *((PKEYVALUE *) pOpt->pValue); pKeyValue != NULL; pKeyValue = pNext)
         {
         pNext = pKeyValue->pNext;
         if(pKeyValue->pValue != NULL)
            free(pKeyValue->pValue);
         free(pKeyValue);
         }

      *((PKEYVALUE *) pOpt->pValue) = NULL;
      }
   return OPT_ERR_NONE;
   } // End of HandleKeyValueListOption().


/*
 * HandleCfgfileOption()
 * Handle options of the OPT_TYPE_CFGFILE and CFGFILEMAIN types.
 * Arguments:
 * POPTIONS - The list of all options.
 * POPTIONS pOpt - Option to process.
 * int nIndex - Index for indexed options.
 * uint32_t nOper - Operation to process.
 * void *pArg - Pointer to filename or an array of filenames.
 * Returns:
 * uint32_t - En error code. 0 if no error.
 */
uint32_t HandleCfgfileOption(POPTIONS pOptArray, POPTIONS pOpt, int nIndex,
  uint32_t nOper, void *pArg)
   {
   uint32_t nRet;
   char *pFile;
   unsigned int i;
   BOOL bFound;

   if(CHECK_OPT_OPER(nOper, SET))
      {
// Check if we have an array of filenames.
      if(CHECK_OPT_FLAG(pOpt, CFGFILEARRAY) 
        && CHECK_OPT_OPER_FLAG(nOper, CFGFILEARRAY))
         {
// Check arguments.
         if(pArg == NULL || ((char **) pArg)[0] == NULL)
            return OPT_ERR_FILENAMEMISSING;

// Loop for all files in array.
         bFound = FALSE;
         for(i = 0; ((char **) pArg)[i] != NULL; i++)
            {
// Translate the string that contains the filename.
            if((pFile = TranslateString(((char **) pArg)[i])) == NULL)
               return OPT_ERR_INVALIDSTRING;

// Read the file now.
            if((nRet = ReadOptionFile(pFile, (char *) pOpt->pExtra, pOptArray,
              nOper | GET_OPT_FLAGS(pOpt))) == OPT_ERR_NONE
              && pOpt->pValue != NULL)
               {
// Add the name of this config file to the array of files.
               if(ou_AddStringToArray(pFile, (char ***) pOpt->pValue))
                  {
                  free(pFile);
                  return OPT_ERR_MALLOC;
                  }
// If we are to read only one file, then return now.
               if(!CHECK_OPT_FLAG(pOpt, CFGREADALLDEF))
                  {
                  free(pFile);
                  return nRet;
                  }
// Flag that at least one file was found.
               bFound = TRUE;
               }
/* If the error wasn't that the file couldn't be found, then we are done. */
            else if(!CHECK_ERR_CODE(nRet, FILENOTFOUND))
               {
               free(pFile);
               return nRet;
               }

            free(pFile);
            }
         return bFound ? OPT_ERR_NONE : OPT_ERR_DEFCFGFILENF;
         }
      else
         {
// Check arguments.
         if(pArg == NULL)
            return OPT_ERR_FILENAMEMISSING;

// Translate the string that contains the filename.
         if((pFile = TranslateString((char *) pArg)) == NULL)
            return OPT_ERR_INVALIDSTRING;

// Read the file now.
         if((nRet = ReadOptionFile(pFile, (char *) pOpt->pExtra, pOptArray,
           nOper | GET_OPT_FLAGS(pOpt))) == OPT_ERR_NONE && pOpt->pValue
           != NULL)
            {
// Add the name of this config file to the array of files.
            if(ou_AddStringToArray(pFile, (char ***) pOpt->pValue))
               {
               free(pFile);
               return OPT_ERR_MALLOC;
               }
            }
         free(pFile);
         return nRet;
         }
      }
// Free array of filenames.
   else if(CHECK_OPT_OPER(nOper, FREE) && pOpt->pValue != NULL
     && *((char ***) pOpt->pValue) != NULL)
      {
      int i;

// Free each filename.
      for(i = 0; (*((char ***) pOpt->pValue))[i] != NULL; i++)
         free((*((char ***) pOpt->pValue))[i]);

// Free the container.
      free(*((char ***) pOpt->pValue));
      *((char ***) pOpt->pValue) = NULL;
      }

   return OPT_ERR_NONE;
   } // End of HandleCfgfileOption().


/*
 * HandleIndexedStrOption()
 * Handle options of the OPT_TYPE_INDEXED_STR type.
 * Arguments:
 * POPTIONS pOptArray - The list of all options.
 * POPTIONS pOpt - Option to process.
 * int nIndex - Index for indexed options.
 * uint32_t nOper - Operation to process.
 * void *pArg - Optional extra argument used for some options and operations.
 * Returns:
 * uint32_t - En error code. 0 if no error.
 */
uint32_t HandleIndexedStrOption(POPTIONS pOptArray, POPTIONS pOpt, int nIndex,
  uint32_t nOper, void *pArg)
   {
// Set to default. A numbered value cannot have a default, just free existing
// values and set to NULL.
   if(CHECK_OPT_OPER(nOper, DEF))
      {
      int i;

// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

// Free current value, if there is one.
      if(*((PINDEXEDOPT **) pOpt->pValue) != NULL)
         {
// Free each struct in the list.
         for(i = 0; (*((PINDEXEDOPT **) pOpt->pValue))[i] != NULL; i++)
            {
// Free the string in the struct, if there is one.
            if((*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue != 0)
               free((void *) (intptr_t)
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue);
            free((*((PINDEXEDOPT **) pOpt->pValue))[i]);
            }

// Free the array itself.
         free(*((PINDEXEDOPT **) pOpt->pValue));
         *((PINDEXEDOPT **) pOpt->pValue) = NULL;
         }
      }
   else if(CHECK_OPT_OPER(nOper, SET))
      {
      PINDEXEDOPT pIndexedOpt = NULL;
      char *pStr = (char *) pArg;
      int i;

// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

// Check the value.
      if(pStr == NULL)
         {
         if(CHECK_OPT_FLAG(pOpt, NULLEMPTY))
            pStr = "";
         else if(CHECK_OPT_FLAG(pOpt, NONULL))
            return OPT_ERR_NULLNOTVALID;
         else
            pStr = NULL;
         }

// Find this value.
      if(*((PINDEXEDOPT **) pOpt->pValue) != NULL)
         {
         for(i = 0; (*((PINDEXEDOPT **) pOpt->pValue))[i] != NULL; i++)
            {
            if((*((PINDEXEDOPT *) pOpt->pValue))[i].nId == nIndex)
               {
               pIndexedOpt = (*((PINDEXEDOPT **) pOpt->pValue))[i];
               break;
               }
            }
         }

// Create an initialize an option struct, if it doesn't exist.
      if(pIndexedOpt == NULL)
         {
         if((pIndexedOpt = (PINDEXEDOPT) malloc(sizeof(INDEXEDOPT))) == NULL)
            return OPT_ERR_MALLOC;
         pIndexedOpt->nId = nIndex;

// Add the struct to the array now.
         if(AddIndexedOptToArray(pIndexedOpt, (PINDEXEDOPT **) pOpt->pValue))
            return OPT_ERR_MALLOC;
         }
// Else, clear current contents.
      else
         {
         if(pIndexedOpt->nValue != 0)
            free((void *) (intptr_t) pIndexedOpt->nValue);
         }

// Now, set the value.
      pIndexedOpt->nValue = 0;
      if(pStr != NULL
        && (pIndexedOpt->nValue = (intptr_t) TranslateString(pStr)) == 0)
         return OPT_ERR_MALLOC;
      }
   else if(CHECK_OPT_OPER(nOper, PRT))
      {
      FILE *fp = (FILE *) pArg;
      char *pTmp;
      int i;

      if(pOpt->pValue != NULL && *((PINDEXEDOPT **) (pOpt->pValue)) != NULL)
         {
         for(i = 0; (*((PINDEXEDOPT **) pOpt->pValue))[i] != NULL; i++)
            {
// Untranslate the string.
            if((*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue == 0)
               fprintf(fp, "%s%s = ", i == 0 ? "" : "\n",
                 ou_GetOptionName(pOpt,
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nId));
            else
               {
               if((pTmp = UntranslateString((char *) (intptr_t)
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue)) == NULL)
                  return OPT_ERR_MALLOC;

               fprintf(fp, "%s%s = %s", i == 0 ? "" : "\n",
                 ou_GetOptionName(pOpt,
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nId), pTmp);
               free(pTmp);
               }
            }
         }
      else
         fprintf(fp, "# %s =", ou_GetOptionName(pOpt, -1));
      }
// Free string and list of strings.
   else if(CHECK_OPT_OPER(nOper, FREE)
     && *((PINDEXEDOPT ***) pOpt->pValue) != NULL)
      {
      int i;

// Free each struct in the list.
      for(i = 0; (*((PINDEXEDOPT **) pOpt->pValue))[i] != NULL; i++)
         {
// Free the string in the struct, if there is one.
         if((*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue != 0)
            free((void *) (intptr_t)
              (*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue);
         free((*((PINDEXEDOPT **) pOpt->pValue))[i]);
         }

// Free the array itself.
      free(*((PINDEXEDOPT **) pOpt->pValue));
      *((PINDEXEDOPT **) pOpt->pValue) = NULL;
      }
   return OPT_ERR_NONE;
   } // End of HandleIndexedStrOption().


/*
 * HandleIndexedIntOption()
 * Handle options of the OPT_TYPE_INDEXED_INT, UINT and BOOL types.
 * Arguments:
 * POPTIONS pOptArray - The list of all options.
 * POPTIONS pOpt - Option to process.
 * int nIndex - Index for indexed options.
 * uint32_t nOper - Operation to process.
 * void *pArg - Optional extra argument used for some options and operations.
 * Returns:
 * uint32_t - En error code. 0 if no error.
 */
uint32_t HandleIndexedIntOption(POPTIONS pOptArray, POPTIONS pOpt, int nIndex,
  uint32_t nOper, void *pArg)
   {
// Set to default. A numbered value cannot have a default, just free existing
// values and set to NULL.
   if(CHECK_OPT_OPER(nOper, DEF))
      {
      int i;

// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

// Free current value, if there is one.
      if(*((PINDEXEDOPT **) pOpt->pValue) != NULL)
         {
// Free each struct in the list.
         for(i = 0; (*((PINDEXEDOPT **) pOpt->pValue))[i] != NULL; i++)
            free((*((PINDEXEDOPT **) pOpt->pValue))[i]);

// Free the array itself.
         free(*((PINDEXEDOPT **) pOpt->pValue));
         *((PINDEXEDOPT **) pOpt->pValue) = NULL;
         }
      }
   else if(CHECK_OPT_OPER(nOper, SET))
      {
      PINDEXEDOPT pIndexedOpt = NULL;
      char *pStr = (char *) pArg;
      int i;
      int nRet;

// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

// Find this value.
      if(*((PINDEXEDOPT **) pOpt->pValue) != NULL)
         {
         for(i = 0; (*((PINDEXEDOPT **) pOpt->pValue))[i] != NULL; i++)
            {
            if((*((PINDEXEDOPT *) pOpt->pValue))[i].nId == nIndex)
               {
               pIndexedOpt = (*((PINDEXEDOPT **) pOpt->pValue))[i];
               break;
               }
            }
         }

// Create and initialize an option struct, if it doesn't exist.
      if(pIndexedOpt == NULL)
         {
         if((pIndexedOpt = (PINDEXEDOPT) malloc(sizeof(INDEXEDOPT))) == NULL)
            return OPT_ERR_MALLOC;
         pIndexedOpt->nId = nIndex;

// Add the struct to the array now.
         if(AddIndexedOptToArray(pIndexedOpt, (PINDEXEDOPT **) pOpt->pValue))
            return OPT_ERR_MALLOC;
         }

// Now, set the the value.
      if(CHECK_OPT_TYPE(pOpt, INTBASE) || CHECK_OPT_TYPE(pOpt, INT)
        || CHECK_OPT_TYPE(pOpt, UINT))
         {
         if((nRet = IntFromString(pStr, &pIndexedOpt->nValue,
           GET_OPT_FLAG(pOpt, UNSIGNED) | GET_OPT_SIZE(pOpt))) != OPT_ERR_NONE)
            return nRet;
         }
      else if(CHECK_OPT_TYPE(pOpt, BOOL))
         {
         if((nRet = IntFromString(pStr, &pIndexedOpt->nValue,
           OPT_TYPE_BOOL)) != OPT_ERR_NONE)
            return nRet;
         }
      }
   else if(CHECK_OPT_OPER(nOper, PRT))
      {
      FILE *fp = (FILE *) pArg;
      int i;

// Loop for all values.
      if(pOpt->pValue != NULL && *((PINDEXEDOPT **) (pOpt->pValue)) != NULL)
         {
         for(i = 0; (*((PINDEXEDOPT **) pOpt->pValue))[i] != NULL; i++)
            {
// Print value depending on type.
            if(CHECK_OPT_TYPE(pOpt, BOOL))
               fprintf(fp, "%s%s = %s", i == 0 ? "" : "\n",
                 ou_GetOptionName(pOpt,
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nId),
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue
                 ? "TRUE" : "FALSE");
            else if(CHECK_OPT_FLAG(pOpt, UNSIGNED)
              && (CHECK_OPT_SIZE(pOpt, SIZE64)
              || (CHECK_OPT_SIZE(pOpt, SIZEINT) && sizeof(int) == 8)
              || (CHECK_OPT_SIZE(pOpt, SIZELONG) && sizeof(long) == 8)))
               fprintf(fp, "%s%s = %" PRIu64 , i == 0 ? "" : "\n",
                 ou_GetOptionName(pOpt,
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nId),
                 (uint64_t) (*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue);
            else if(CHECK_OPT_SIZE(pOpt, SIZE64)
              || (CHECK_OPT_SIZE(pOpt, SIZEINT) && sizeof(int) == 8)
              || (CHECK_OPT_SIZE(pOpt, SIZELONG) && sizeof(long) == 8))
               fprintf(fp, "%s%s = %" PRId64 , i == 0 ? "" : "\n",
                 ou_GetOptionName(pOpt,
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nId),
                 (int64_t) (*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue);
            else if(CHECK_OPT_FLAG(pOpt, UNSIGNED)
              && (CHECK_OPT_SIZE(pOpt, SIZE32)
              || (CHECK_OPT_SIZE(pOpt, SIZESHORT) && sizeof(short) == 4)
              || (CHECK_OPT_SIZE(pOpt, SIZEINT) && sizeof(int) == 4)
              || (CHECK_OPT_SIZE(pOpt, SIZELONG) && sizeof(long) == 4)))
               fprintf(fp, "%s%s = %" PRIu32 , i == 0 ? "" : "\n",
                 ou_GetOptionName(pOpt,
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nId),
                 (uint32_t) (*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue);
            else if(CHECK_OPT_SIZE(pOpt, SIZE32)
              || (CHECK_OPT_SIZE(pOpt, SIZESHORT) && sizeof(short) == 4)
              || (CHECK_OPT_SIZE(pOpt, SIZEINT) && sizeof(int) == 4)
              || (CHECK_OPT_SIZE(pOpt, SIZELONG) && sizeof(long) == 4))
               fprintf(fp, "%s%s = %" PRId32 , i == 0 ? "" : "\n",
                 ou_GetOptionName(pOpt,
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nId),
                 (int32_t) (*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue);
            else if(CHECK_OPT_FLAG(pOpt, UNSIGNED)
              && (CHECK_OPT_SIZE(pOpt, SIZE16)
              || (CHECK_OPT_SIZE(pOpt, SIZESHORT) && sizeof(int) == 2)
              || (CHECK_OPT_SIZE(pOpt, SIZEINT) && sizeof(long) == 2)))
               fprintf(fp, "%s%s = %" PRIu16 , i == 0 ? "" : "\n",
                 ou_GetOptionName(pOpt,
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nId),
                 (uint16_t) (*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue);
            else if(CHECK_OPT_SIZE(pOpt, SIZE16)
              || (CHECK_OPT_SIZE(pOpt, SIZESHORT) && sizeof(int) == 2)
              || (CHECK_OPT_SIZE(pOpt, SIZEINT) && sizeof(long) == 2))
               fprintf(fp, "%s%s = %" PRId16 , i == 0 ? "" : "\n",
                 ou_GetOptionName(pOpt,
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nId),
                 (int16_t) (*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue);
            else if(CHECK_OPT_FLAG(pOpt, UNSIGNED)
              && CHECK_OPT_SIZE(pOpt, SIZE8))
               fprintf(fp, "%s%s = %" PRIu8 , i == 0 ? "" : "\n",
                 ou_GetOptionName(pOpt,
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nId),
                 (uint8_t) (*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue);
            else if(CHECK_OPT_SIZE(pOpt, SIZE8))
               fprintf(fp, "%s%s = %" PRId8 , i == 0 ? "" : "\n",
                 ou_GetOptionName(pOpt,
                 (*((PINDEXEDOPT **) pOpt->pValue))[i]->nId),
                 (int8_t) (*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue);
            }
         }
      else
         fprintf(fp, "# %s =", ou_GetOptionName(pOpt, -1));
      }
// Free string and list of strings.
   else if(CHECK_OPT_OPER(nOper, FREE)
     && *((PINDEXEDOPT ***) pOpt->pValue) != NULL)
      {
      int i;

// Free each struct in the list.
      for(i = 0; (*((PINDEXEDOPT **) pOpt->pValue))[i] != NULL; i++)
         free((*((PINDEXEDOPT **) pOpt->pValue))[i]);

// Free the array itself.
      free(*((PINDEXEDOPT **) pOpt->pValue));
      *((PINDEXEDOPT **) pOpt->pValue) = NULL;
      }
   return OPT_ERR_NONE;
   } // End of HandleIndexedIntOption().


/*
 * Function: HandleIndexedSelectionOption()
 * Handle options of the OPT_TYPE_INDEXED_SEL type.
 * Arguments:
 * POPTIONS pOptArray - The list of all options.
 * POPTIONS pOpt - Option to process.
 * int nIndex - Index for indexed options.
 * uint32_t nOper - Operation to process.
 * void *pArg - Optional extra argument used for some options and operations.
 * Returns:
 * uint32_t - En error code. 0 if no error.
 */
uint32_t HandleIndexedSelectionOption(POPTIONS pOptArray, POPTIONS pOpt, int nIndex,
  uint32_t nOper, void *pArg)
   {
// Set to default. A numbered value cannot have a default, just free existing
// values and set to NULL.
   if(CHECK_OPT_OPER(nOper, DEF))
      {
      int i;

// If the value pointer is NULL, then ignore this.
      if(pOpt->pValue == NULL)
         return OPT_ERR_NONE;

// Free current value, if there is one.
      if(*((PINDEXEDOPT **) pOpt->pValue) != NULL)
         {
// Free each struct in the list.
         for(i = 0; (*((PINDEXEDOPT **) pOpt->pValue))[i] != NULL; i++)
            free((*((PINDEXEDOPT **) pOpt->pValue))[i]);

// Free the array itself.
         free(*((PINDEXEDOPT **) pOpt->pValue));
         *((PINDEXEDOPT **) pOpt->pValue) = NULL;
         }
      }
   else if(CHECK_OPT_OPER(nOper, SET))
      {
      PINDEXEDOPT pIndexedOpt = NULL;
      char *pStr = (char *) pArg;
      char *pTmp;
      int i;

// Find this value.
      if(*((PINDEXEDOPT **) pOpt->pValue) != NULL)
         {
         for(i = 0; (*((PINDEXEDOPT **) pOpt->pValue))[i] != NULL; i++)
            {
            if((*((PINDEXEDOPT *) pOpt->pValue))[i].nId == nIndex)
               {
               pIndexedOpt = (*((PINDEXEDOPT **) pOpt->pValue))[i];
               break;
               }
            }
         }

// Create an initialized option struct, if it doesn't exist.
      if(pIndexedOpt == NULL)
         {
         if((pIndexedOpt = (PINDEXEDOPT) malloc(sizeof(INDEXEDOPT))) == NULL)
            return OPT_ERR_MALLOC;
         pIndexedOpt->nId = nIndex;

// Add the struct to the array now.
         if(AddIndexedOptToArray(pIndexedOpt, (PINDEXEDOPT **) pOpt->pValue))
            return OPT_ERR_MALLOC;
         }

// Get the selection #.
      for(i = 0, pTmp = (char *) pOpt->pExtra; *pTmp != '\0'; i++, pTmp++)
         {
         if(strncasecmp(pStr, pTmp, strlen(pStr)) == 0)
            break;

// Skip to next string.
         for(; *pTmp != ';' && *pTmp != '\0'; pTmp++)
           ;

// If this was the last string, break now.
         if(*pTmp == '\0')
            return OPT_ERR_UNKNOWNVALUE;
         }

// Set the the value.
      pIndexedOpt->nValue = i;
      }
   else if(CHECK_OPT_OPER(nOper, PRT))
      {
      FILE *fp = (FILE *) pArg;
      char *pTmp;
      int j;
      int i;

      if(pOpt->pValue != NULL && *((PINDEXEDOPT **) (pOpt->pValue)) != NULL)
         {
         for(i = 0; (*((PINDEXEDOPT **) pOpt->pValue))[i] != NULL; i++)
            {
// Find the string first.
            for(j = 0, pTmp = (char *) pOpt->pExtra; *pTmp != '\0'
              && j != (intptr_t) ((*((PINDEXEDOPT **) pOpt->pValue))[i]->nValue); j++, pTmp++)
               {
// Move to next string.
               for(; *pTmp != ';' && *pTmp != '\0'; pTmp++)
                 ;

// If this was the last string, break now.
// This really should never be an error.
               if(*pTmp == '\0')
                  return OPT_ERR_UNKNOWNVALUE;
               }

// Now, find the length of the string.
            for(j = 0; pTmp[j] != ';' && pTmp[j] != '\0'; j++)
              ;
            fprintf(fp, "%s%s = %.*s", i == 0 ? "" : "\n",
              ou_GetOptionName(pOpt,
              (*((PINDEXEDOPT **) pOpt->pValue))[i]->nId), j, pTmp);
            }
         }
      else
         fprintf(fp, "# %s =", ou_GetOptionName(pOpt, -1));
      }
// Free string and list of strings.
   else if(CHECK_OPT_OPER(nOper, FREE)
     && *((PINDEXEDOPT ***) pOpt->pValue) != NULL)
      {
      int i;

// Free each struct in the list.
      for(i = 0; (*((PINDEXEDOPT **) pOpt->pValue))[i] != NULL; i++)
         free((*((PINDEXEDOPT **) pOpt->pValue))[i]);

// Free the array itself.
      free(*((PINDEXEDOPT **) pOpt->pValue));
      *((PINDEXEDOPT **) pOpt->pValue) = NULL;
      }
   return OPT_ERR_NONE;
   } // End of HandleIndexedSelectionOption().


// Helper functions.
/*
 * Function: FindLongOption()
 * Find a named option in an array of options. Only search the long name.
 * POPTIONS pOptArray - The array of options.
 * char *pOptName - A string with the name of the option.
 * int *pnIndex - Set to index for indexed options. -1 for non-indexed.
 * Returns:
 * POPTIONS - A pointer to the option, NULL if not found.
 */
POPTIONS FindLongOption(POPTIONS pOptArray, char *pOptName, int *pnIndex)
   {
   char *pTmp1, *pTmp2;
   char *pName, *pName2, *pIndex;
   POPTIONS pOpt;

// Find separator between option name and value.
   for(pTmp1 = pOptName; *pTmp1 != '\0' && *pTmp1 != '='
     && *pTmp1 != ' ' &&  *pTmp1 != '\t' ; pTmp1++)
      ;

// Loop through the array.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      {
      if(pOpt->pName == NULL)
         continue;

// Ignore hidden options.
      if(CHECK_OPT_TYPE(pOpt, CFGFILEMAIN) && CHECK_OPT_FLAG(pOpt, HIDDEN))
         continue;

// Get the name.
      if(pOpt->pName[1] == '|')
         {
// If this is only a short option, then ignore it.
         if(pOpt->pName[2] == '\0')
            continue;
         pName = &pOpt->pName[2];
         }
      else
         pName = pOpt->pName;

// Check for non-indexed option.
      if(strlen(pName) == ((unsigned int) (pTmp1 - pOptName))
        && strncasecmp(pOptName, pName, pTmp1 - pOptName) == 0
        && !CHECK_OPT_FLAG(pOpt, INDEXED))
         {
         *pnIndex = -1;
         return pOpt;
         }

// Check for indexed options.
      else if(CHECK_OPT_FLAG(pOpt, INDEXED))
         {
// Find the separator between index and name.
         for(pName2 = pName; *pName2 != ' ' && *pName2 != '\t' && *pName2 != '\0'; pName2++)
            ;

// Check if the leading part matches.
         if(strncasecmp(pOptName, pName, pName2 - pName) != 0)
            continue;

// Set pointer to first character in index.
         pIndex = pOptName + (pName2 - pName);

// Check that there is at least one numeric in the index.
         if(*pIndex < '0' || *pIndex > '9')
            continue;

// Skip past numbers in the option now.
         for(pTmp2 = pIndex; *pTmp2 >= '0' && *pTmp2 <= '9'; pTmp2++)
            ;

// Skip past multiple blanks in the name.
         for(; *pName2 == ' '  || *pName2 == '\t'; pName2++)
            ;

// Now, check if the trailing part compares.
         if(strncasecmp(pTmp2, pName2, strlen(pName2)) != 0)
            continue;

// Set the index.
         *pnIndex = atoi(pIndex);
         return pOpt;
         }
      }
   *pnIndex = -1;
   return NULL;
   } // End of FindLongOption().


/*
 * Function: FindShortOption()
 * Find a single-character option in the list of options.
 * Arguments:
 * POPTIONS pOptArray - The array of options.
 * char **pArgv - Pointer to the array of arguments.
 * int nArgc - Argument count including first element in pArgv.
 * char **pValue - Set to the value, if there was one.
 * int *pnArgsConsumed - Set to # of arguments consumed.
 * Returns:
 * POPTIONS - A pointer to the option, NULL if not found.
 */
POPTIONS FindShortOption(POPTIONS pOptArray, char **pArgv, int nArgc,
  char **pValue, int *pnArgsConsumed)
   {
   POPTIONS pOpt;

// Check that the arguemt is valid.
   if(pArgv[0] == NULL || pArgv[0][0] != '-' || pArgv[0][1] == '-')
      return NULL;

// Loop through the array.
   for(pOpt = pOptArray; !CHECK_OPT_TYPE(pOpt, NONE); pOpt++)
      {
// Ignore hidden options.
      if(CHECK_OPT_TYPE(pOpt, CFGFILEMAIN) && CHECK_OPT_FLAG(pOpt, HIDDEN))
         continue;

// Check if this is it. Name must be followed by pipe.
      if(pOpt->pName[0] == pArgv[0][1] && pOpt->pName[1] == '|')
         {
// A INT/BOOL type with a flag only setting only consumes one argument.
         if((CHECK_OPT_TYPE(pOpt, INTBASE)
           || CHECK_OPT_TYPE(pOpt, INT)
           || CHECK_OPT_TYPE(pOpt, UINT)
           || CHECK_OPT_TYPE(pOpt, BOOL))
           && CHECK_OPT_FLAG(pOpt, CLNOARG1))
            {
// If there are more characters, then don't consume anything, as this is likely
// an error.
            if(pArgv[0][2] != '\0')
               {
               *pnArgsConsumed = 0;
               return NULL;
               }
            *pValue = "1";
            *pnArgsConsumed = 1;
            return pOpt;
            }
         else
            {
// Check if option is immediately followed by the argument.
            if(pArgv[0][2] != '\0')
               {
               *pValue = &pArgv[0][2];
               *pnArgsConsumed = 1;
               return pOpt;
               }
// Else, check if there are more arguments to process, if so, pick that one,
// unless this is disallowed for this option.
            else if(nArgc > 1 && pArgv[1][0] != '-'
              && !CHECK_OPT_FLAG(pOpt, SHORT1ARG))
               {
               *pValue = pArgv[1];
               *pnArgsConsumed = 2;
               return pOpt;
               }
// If there was no more argument, set the value to an empty string.
            else
               {
               *pValue = "";
               *pnArgsConsumed = 1;
               return pOpt;
               }
            }
         }
      }
   return NULL;
   } // End of FindShortOption().


/*
 * Function: ReadOptionFile()
 * Read configuration file options.
 * Arguments:
 * char *pFile - Name of file to read.
 * char *pSection - Name of section to read.
 * POPTIONS pOptArray - Array of options to set.
 * uint32_t nOper - Operation to perform (assumed SET for now) and flags.
 * Returns:
 * uint32_t - An error code, 0 if there was no error.
 */
uint32_t ReadOptionFile(char *pFile, char *pSection, POPTIONS pOptArray,
  uint32_t nOper)
   {
   FILE *fp;
   BOOL bSect = FALSE;
   BOOL bCfgfile = FALSE;
   BOOL bIgnUnknown = FALSE;
   int nIndex;
   int nLine = 0;
   int nRet;
   int nBufSize = 0;
   unsigned int nLen;
   char *pTmp1, *pTmp2;
   char *pValue;
   char *pBuf = NULL;
   char *pSectTmp1, *pSectTmp2;
   char szFileName[PATH_MAX];
   POPTIONS pOpt;

// Check that the filename is valie.
   if(pFile == NULL || *pFile == '\0')
      return OPT_ERR_INVALIDFILENAME;

// If the filename begins with a ~ (tilde), then expand that first.
   if(*pFile == '~')
      {
      char *pHome = getenv("HOME");

      if(pHome == NULL)
         return OPT_ERR_NOHOME;
      if(strlen(pFile) + strlen(pHome) + 1 > PATH_MAX)
         return OPT_ERR_PATHTOOLONG;
      strcpy(szFileName, pHome);
      strcat(szFileName, &pFile[pFile[1] == '/' ? 1 : 2]);
      pFile = szFileName;
      }

// Open the file.
   if((fp = fopen(pFile, "r")) == NULL)
      {
// Check what error we got here.
      if(errno == ENOENT)
         return OPT_ERR_FILENOTFOUND;
      return OPT_ERR_OPENFILE;
      }

// Process the options in the file in two steps. First, all non-config file
// options, then all config file options.
   for(bCfgfile = FALSE; ; )
      {
// Read the file, line by line.
      while(!feof(fp))
         {
// Read the line.
         if((pBuf = ReadOptionFileLine(fp, pBuf, &nBufSize, &nLine)) == NULL)
            return PrintOptionError(OPT_ERR_MALLOC, NULL, 0, NULL, pFile,
              nLine);

// If the buffer isn't empty, EOF happened in the middle of reading an option.
         if(feof(fp) && *pBuf != '\0')
            {
            fclose(fp);
            free(pBuf);
            return PrintOptionError(OPT_ERR_UNEXPECTEDEOF, NULL, 0, NULL, pFile,
              nLine);
            }

// Check for EOF and file errors.
         if(feof(fp) || ferror(fp))
            break;

// Now, parse the line.
         for(pTmp1 = pBuf; *pTmp1 == ' ' || *pTmp1 == '\t'; pTmp1++)
            ;
// Skip blank lines.
         if(*pTmp1 == '\0')
            continue;

// Skip comments.
         if(*pTmp1 == '#' || *pTmp1 == ';')
            continue;

// Check section start / end.
         if(*pTmp1 == '[')
            {
            pTmp1++;
            for(pTmp2 = pTmp1; *pTmp2 != ']' && *pTmp2 != '\0'; pTmp2++)
               ;
            if(*pTmp2 == '\0')
               {
               fclose(fp);
               free(pBuf);
               return PrintOptionError(OPT_ERR_SECTIONHEAD, NULL, 0, NULL,
                 pFile, nLine);
               }
            *pTmp2 = '\0';
// Check all section names.
            for(pSectTmp1 = pSection; ; )
               {
// Skip past - that indicates to igore unknown options.
               if(*pSectTmp1 == '-')
                  {
                  pSectTmp1++;
                  bIgnUnknown = TRUE;
                  }
               else
                  bIgnUnknown = FALSE;

// Find the next section name.
               for(pSectTmp2 = pSectTmp1, nLen = 0; *pSectTmp2 != '\0' && *pSectTmp2 != ';'; pSectTmp2++, nLen++)
                  ;

// Check if we found a section.
               if(strlen(pTmp1) == nLen && strncasecmp(pTmp1, pSectTmp1, nLen) == 0)
                  {
                  bSect = TRUE;
                  break;
                  }
               else
                  bSect = FALSE;

// Move to next section to check.
               if(*pSectTmp2 == '\0')
                  break;
               pSectTmp1 = ++pSectTmp2;
               }
            continue;
            }

// If we are not in the proper section, then skip this option.
         if(!bSect)
            continue;

// Find option in array.
         if((pOpt = FindLongOption(pOptArray, pTmp1, &nIndex)) == NULL)
            {
// If we are to ignore unknown options, then just keep going.
            if(bIgnUnknown || CHECK_OPT_OPER_FLAG(nOper, IGNUNKNOWNOPT))
               continue;

// Else, close file, free buffer and return an error.
            fclose(fp);
            free(pBuf);

            return PrintOptionError(OPT_ERR_UNKNOWNOPTION, NULL, 0, pTmp1,
              pFile, nLine);
            }

// Check type of option.
         if(CHECK_OPT_TYPE(pOpt, CFGFILEMAIN))
            {
            fclose(fp);
            free(pBuf);
            return PrintOptionError(OPT_ERR_MAINCFGINFILE, pOpt, nIndex,
              NULL, pFile, nLine);
            }

// Check that this option is allowed in a config file.
         if(CHECK_OPT_FLAG(pOpt, NOCFGFILE))
            {
            fclose(fp);
            free(pBuf);
            return PrintOptionError(OPT_ERR_OPTNOTINCFG, pOpt, nIndex,
              pOpt->pName, pFile, nLine);
            }

// Check if this is a help option.
         if(CHECK_OPT_FLAG(pOpt, HELP))
            {
            fclose(fp);
            free(pBuf);
            return PrintOptionError(OPT_ERR_HELPINFILE, pOpt, nIndex,
              NULL, pFile, nLine);
            }

// Check if we are processing cfgfile options now.
         if(bCfgfile != CHECK_OPT_TYPE(pOpt, CFGFILE))
            continue;

// Find the value.
         pValue = GetOptionValueString(pTmp1);

// Process the option.
         if((nRet = ou_ProcessOption(pOptArray, pOpt, nIndex, nOper & ~OPT_FLAG_CFGFILEARRAY, pValue))
           != OPT_ERR_NONE)
            {
            fclose(fp);
            free(pBuf);
            return PrintOptionError(nRet, pOpt, nIndex, pValue, pFile,
              nLine);
            }
         }

// Check for file errors, if so, stop now.
      if(ferror(fp))
         break;

// Make sure we loop just once.
      if(bCfgfile)
         break;
      bCfgfile = TRUE;
      nLine = 0;

// Seek to start of file before we do the second round.
      fseek(fp, 0, SEEK_SET);
      }

// Check for file errors, and then close the file.
   if(ferror(fp))
      {
      fclose(fp);
      free(pBuf);
      return OPT_ERR_READFILE;
      }

// Close file, free buffer and return.
   fclose(fp);
   free(pBuf);

   return OPT_ERR_NONE;
   } // End of ReadOptionFile().


/*
 * Function: ReadOptionFileLine()
 * Read a line from an option file.
 * Arguments:
 * FILE *fp - File to read from.
 * char *pBuf - Buffer to read into.
 * int *pnBufSize - Size of buffer. 0 first time, set by function.
 * int *pnLine - Line counter.
 * Returns:
 * char *pBuf - The read line, NULL if there was a memory alloocation error.
 */
char *ReadOptionFileLine(FILE *fp, char *pBuf, int *pnBufSize, int *pnLine)
   {
   char szTmp[128];

// Allocate an initial buffer, if that isn't done already.
   if(pBuf == NULL || *pnBufSize == 0)
      {
      if((pBuf = (char *) malloc(512)) == NULL)
         {
         *pnBufSize = 0;
         return NULL;
         }
      *pnBufSize = 512;
      }

// Set buffer to the empty string.
   *pBuf = '\0';

// Get a line from the config file, or at least try to.
   if(fgets(pBuf, *pnBufSize, fp) == NULL)
      return pBuf;

// Check the the line would fit in the buffer.
   if(pBuf[strlen(pBuf) - 1] != '\n')
      {
// If the line didn't fit, then add space til it does.
      for(;;)
         {
// Get some more data.
         if(fgets(szTmp, sizeof(szTmp), fp) == NULL)
            break;

// Add space to the buffer.
         if((pBuf = (char *) realloc(pBuf, *pnBufSize + sizeof(szTmp))) == NULL)
            {
            *pnBufSize = 0;
            return NULL;
            }
         *pnBufSize += sizeof(szTmp);

// Add this string to the buffer.
         strcat(pBuf, szTmp);

// Check if we have reached EOL.
         if(pBuf[strlen(pBuf) - 1] == '\n')
            break;
         }
      if(feof(fp) || ferror(fp))
         return pBuf;
      }
   (*pnLine)++;

// Remove EOL.
   pBuf[strlen(pBuf) - 1] = '\0';

// Loop until there is no line with a line continuation character.
   while(pBuf[strlen(pBuf) - 1] == '\\')
      {
// Remove the line continuation character.
      pBuf[strlen(pBuf) - 1] = '\0';

// Read the following line into the end of this buffer.
      if(fgets(&pBuf[strlen(pBuf)], *pnBufSize - strlen(pBuf), fp) == NULL)
         break;

// Check if it did fit, and if not, tncrease the buffer til it does.
      if(pBuf[strlen(pBuf) - 1] != '\n')
         {
// If the line didn't fit, then add space til it does.
         for(;;)
            {
// Get some more data.
            if(fgets(szTmp, sizeof(szTmp), fp) == NULL)
               break;

// Add space to the buffer.
            if((pBuf = (char *) realloc(pBuf, *pnBufSize + sizeof(szTmp)))
              == NULL)
               {
               *pnBufSize = 0;
               return NULL;
               }
            *pnBufSize += sizeof(szTmp);

// Add this string to the buffer.
            strcat(pBuf, szTmp);

// Check if we have reached EOL.
            if(pBuf[strlen(pBuf) - 1] == '\n')
               break;
            }
         if(feof(fp) || ferror(fp))
            return pBuf;
         }

// Remove EOL.
      pBuf[strlen(pBuf) - 1] = '\0';
      (*pnLine)++;
      }
   return pBuf;
   } // End of ReadOptionFileLine().


/*
 * Function: ou_AddStringToArray()
 * Add a string to an option of type OPT_TYPE_STRARRAY.
 * Arguments:
 * char *pStr - The string to add.
 * char ***pArray - A pointer to the array to add the string to.
 * Returns:
 * BOOL TRUE - If there is an error.
 *      FALSE - If not.
 */
BOOL ou_AddStringToArray(char *pStr, char ***pArray)
   {
   int nStr;

// If pArray is not allocated at this point, then do it now.
   if(*pArray == NULL)
      {
      if((*pArray = (char **) malloc(OPT_ARRAY_INCR * sizeof(char *))) == NULL)
         return TRUE;
      (*pArray)[0] = NULL;
      }

// Find the next free position in the array.
   for(nStr = 0; (*pArray)[nStr] != NULL; nStr++)
      ;

// Copy the string into the array.
   if(((*pArray)[nStr] = strdup(pStr)) == NULL)
      return TRUE;

   ++nStr;
// If this was the last allocated free string, then add space for another NULL
// pointer.
   if(nStr % OPT_ARRAY_INCR == 0)
      {
      if((*pArray = (char **) realloc((void *) *pArray,
          (nStr + OPT_ARRAY_INCR) * sizeof(char *))) == NULL)
         return TRUE;
      }

// Set the next pointer to NULL.
   (*pArray)[nStr] = NULL;
   return FALSE;
   } // End of ou_AddStringToArray().


/*
 * Function: ou_StrExistsInArray()
 * Chck if a string exists in a string array.
 * Arguments:
 * char *pStr - The string to look for.
 * char **pArray - Array to check.
 * BOOL bCaseInsensitive - Flag if this is a case insensitive check.
 * Returns:
 * BOOL - TRUE if the string existed, else FALSE.
 */
BOOL ou_StrExistsInArray(char *pStr , char **pArray, BOOL bCaseInsensitive)
   {
   int i;

   if(pArray == NULL)
      return FALSE;
   if(bCaseInsensitive)
      {
      for(i = 0; pArray != NULL && pArray[i] != NULL && strcasecmp(pArray[i], pStr) != 0; i++)
         ;
      }
   else
      {
      for(i = 0; pArray != NULL && pArray[i] != NULL && strcmp(pArray[i], pStr) != 0; i++)
         ;
      }

   if(pArray != NULL && pArray[i] != NULL)
      return TRUE;

   return FALSE;
   } // End of ou_StrExistsInArray()


/*
 * Function: ou_KeyValueExists()
 * Checks if a specified key exists in the key value list.
 * Arguents:
 * PKEYVALUE pKeyValue - The key value list.
 * char *pKey - The key to check.
 * Returns:
 * BOOL - TRUE if the key exists in the list, elase FALSE.
 */
BOOL ou_KeyValueExists(PKEYVALUE pKeyValue, char *pKey)
   {
   for(; pKeyValue != NULL; pKeyValue = pKeyValue->pNext)
      {
      if(strcasecmp(pKeyValue->pKey, pKey) == 0)
         return TRUE;
      }

   return FALSE;
   } // End of ou_KeyValueExists()


/*
 * Function: ou_GetKeyValue()
 * Get the value associated with a key in a key value list.
 * PKEYVALUE pKeyValue - The key value list.
 * char *pKey - The key to look for.
 * Returns:
 * char * - The key value of NULL if the key wasn't found (note that the value may be NULL).
 */
char *ou_GetKeyValue(PKEYVALUE pKeyValue, char *pKey)
   {
   for(; pKeyValue != NULL; pKeyValue = pKeyValue->pNext)
      {
      if(strcasecmp(pKeyValue->pKey, pKey) == 0)
         return pKeyValue->pValue;
      }

   return NULL;
   } // End of ou_GetKeyValue()


/*
 * Function: ou_AddIntegerToArray()
 * Add an integer to an array of integers. The array ends with -1.
 * Arguments:
 * int64_t nValue - The integer to add.
 * int nBytes - # of bytes in the integer.
 * void **pArray - A pointer to the array to add the integer to.
 * Returns:
 * BOOL TRUE - If there is an error.
 *      FALSE - If not.
 */
BOOL ou_AddIntegerToArray(int64_t nValue, int nBytes, void **pArray)
   {
   int i;

// If pArray is not allocated at this point, then do it now.
   if(*pArray == NULL)
      {
      if((*pArray = (int *) malloc(OPT_ARRAY_INCR * nBytes)) == NULL)
         return TRUE;
      if(nBytes == 8)
         ((int64_t *)(*pArray))[0] = -1;
      else if(nBytes == 4)
         ((int32_t *)(*pArray))[0] = -1;
      else if(nBytes == 2)
         ((int16_t *)(*pArray))[0] = -1;
      else if(nBytes == 1)
         ((int8_t *)(*pArray))[0] = -1;
      else
         return TRUE;
      }

// Handle the different # of bytes.
   if(nBytes == 8)
      {
      int64_t *pArrTmp = (int64_t *) *pArray;

// Check if the value already exists.
      for(i = 0; pArrTmp[i] != -1 && pArrTmp[i] != (int64_t) nValue; i++)
         ;
// Return and ignore the rest of the value was already there.
      if(pArrTmp[i] != -1)
         return FALSE;

// Find the next free position in the array.
      for(i = 0; pArrTmp[i] != -1; i++)
         ;

      pArrTmp[i] = (int64_t) nValue;
      ++i;

// If this was the last allocated value, then add space for another one.
      if(i % OPT_ARRAY_INCR == 0)
         {
         if((*pArray = (int *) realloc((void *) *pArray,
             (i + OPT_ARRAY_INCR) * nBytes)) == NULL)
            return TRUE;
         }

// Set the next pointer to NULL.
      ((int64_t *) (*pArray))[i] = -1;
      }
   else if(nBytes == 4)
      {
      int32_t *pArrTmp = (int32_t *) *pArray;

// Check if the value already exists.
      for(i = 0; pArrTmp[i] != -1 && pArrTmp[i] != (int32_t) nValue; i++)
         ;
// Return and ignore the rest of the value was already there.
      if(pArrTmp[i] != -1)
         return FALSE;

// Find the next free position in the array.
      for(i = 0; pArrTmp[i] != -1; i++)
         ;

      pArrTmp[i] = (int32_t) nValue;
      ++i;

// If this was the last allocated value, then add space for another one.
      if(i % OPT_ARRAY_INCR == 0)
         {
         if((*pArray = (int *) realloc((void *) *pArray,
             (i + OPT_ARRAY_INCR) * nBytes)) == NULL)
            return TRUE;
         }

// Set the next pointer to NULL.
      ((int32_t *) (*pArray))[i] = -1;
      }
   else if(nBytes == 2)
      {
      int16_t *pArrTmp = (int16_t *) *pArray;

// Check if the value already exists.
      for(i = 0; pArrTmp[i] != -1 && pArrTmp[i] != (int16_t) nValue; i++)
         ;

// Return and ignore the rest of the value was already there.
      if(pArrTmp[i] != -1)
         return FALSE;

// Find the next free position in the array.
      for(i = 0; pArrTmp[i] != -1; i++)
         ;

      pArrTmp[i] = (int16_t) nValue;
      ++i;

// If this was the last allocated value, then add space for another one.
      if(i % OPT_ARRAY_INCR == 0)
         {
         if((*pArray = (int *) realloc((void *) *pArray,
             (i + OPT_ARRAY_INCR) * nBytes)) == NULL)
            return TRUE;
         }

// Set the next pointer to NULL.
      ((int16_t *) (*pArray))[i] = -1;
      }
   else if(nBytes == 1)
      {
      int8_t *pArrTmp = (int8_t *) *pArray;

// Check if the value already exists.
      for(i = 0; pArrTmp[i] != -1 && pArrTmp[i] != (int8_t) nValue; i++)
         ;
// Return and ignore the rest of the value was already there.
      if(pArrTmp[i] != -1)
         return FALSE;

// Find the next free position in the array.
      for(i = 0; pArrTmp[i] != -1; i++)
         ;

      pArrTmp[i] = (int8_t) nValue;
      ++i;

// If this was the last allocated value, then add space for another one.
      if(i % OPT_ARRAY_INCR == 0)
         {
         if((*pArray = (int *) realloc((void *) *pArray,
             (i + OPT_ARRAY_INCR) * nBytes)) == NULL)
            return TRUE;
         }

// Set the next pointer to NULL.
      ((int8_t *) (*pArray))[i] = -1;
      }
   else
      return TRUE;

   return FALSE;
   } // End of ou_AddIntegerToArray().


/*
 * Function: GetIntegerArrayValue()
 * Get a value from an integer array, converted to n int64_t
 * Arguments:
 * POPTIONS pOpt - The option to get the value from.
 * int nIndex - The index in the array to get.
 * Returns:
 * int64_t - The value, -1 if there is an error.
 */
int64_t GetIntegerArrayValue(POPTIONS pOpt, int nIndex)
   {
   int64_t nRet = -1;

   if(pOpt->pValue == NULL || ((int64_t *) pOpt->pValue) == NULL)
      return -1;

   if(CHECK_OPT_8BYTE(pOpt))
      nRet = (*(int64_t **) pOpt->pValue)[nIndex];
   else if(CHECK_OPT_4BYTE(pOpt))
      nRet = (int64_t) (*(int32_t **) pOpt->pValue)[nIndex];
   else if(CHECK_OPT_2BYTE(pOpt))
      nRet = (int64_t) (*(int16_t **) pOpt->pValue)[nIndex];
   else if(CHECK_OPT_1BYTE(pOpt))
      nRet = (int64_t) (*(int8_t **) pOpt->pValue)[nIndex];

   return nRet;
   } // End of GetIntegerArrayValue()


/*
 * Function: RemoveIntegerFromArray()
 * int nValue - The integer to remove.
 * int **pArray - A pointer to the array to remove the integer from.
 * Returns:
 * BOOL TRUE - If there is an error.
 *      FALSE - If not.
 */
BOOL  RemoveIntegerFromArray(int nValue, int **pArray)
   {
   int i, j;

   for(i = 0; (*pArray)[i] != -1; i++)
      {
      if((*pArray)[i] == nValue)
         {
         for(j = i; (*pArray)[j] != -1; j++)
            (*pArray)[j] = (*pArray)[j + 1];
         }
      }

// Check if there are no more values.
   if((*pArray)[0] == -1)
      {
      free(*pArray);
      *pArray = NULL;
      }
   return FALSE;
   } // End of RemoveIntegerFromArray().


/*
 * Function: AddIndexedOptToArray()
 * Add a INDEXEDOPT structure to an array of these.
 * Arguments:
 * PINDEXEDOPT pValue - The structure to add.
 * PINDEXEDOPT **pArray - A pointer to the array to add the struct to.
 * Returns:
 * BOOL TRUE - If there is an error.
 *      FALSE - If not.
 */
BOOL AddIndexedOptToArray(PINDEXEDOPT pValue, PINDEXEDOPT **pArray)
   {
   int nOpt;

// If pArray is not allocated at this point, then do it now.
   if(*pArray == NULL)
      {
      if((*pArray = (PINDEXEDOPT *) malloc(OPT_ARRAY_INCR * sizeof(PINDEXEDOPT)))
        == NULL)
         return TRUE;
      (*pArray)[0] = NULL;
      }

// Find the next free position in the array.
   for(nOpt = 0; (*pArray)[nOpt] != NULL; nOpt++)
      ;

// Assign value.
   (*pArray)[nOpt] = pValue;

   ++nOpt;
// If this was the last allocated free struct, then add space for another NULL
// pointer.
   if(nOpt % OPT_ARRAY_INCR == 0)
      {
      if((*pArray = (PINDEXEDOPT *) realloc((void *) *pArray,
          (nOpt + OPT_ARRAY_INCR) * sizeof(PINDEXEDOPT))) == NULL)
         return TRUE;
      }

// Set the next pointer to NULL.
   (*pArray)[nOpt] = NULL;
   return FALSE;
   } // End of AddIndexedOptToArray().


/*
 * Function: GetOptionValueString()
 * Set the value of an option string. The return value represents the value
 * string, and the argument string is null terminated.
 * Arguments:
 * char *pStr - The option string, with the format <option>=<value>
 * Returns:
 * char * - The value of the value string. NULL if the value is NULL.
 */
char *GetOptionValueString(char *pStr)
   {
   char *pTmp1, *pTmp2;

// Find the separator between option and value.
   for(pTmp1 = pTmp2 = pStr; *pTmp1 != '\0' && *pTmp1 != '=' && *pTmp1 != ' ' && *pTmp1 != '\t';
     pTmp1++)
      {
      if(*pTmp1 > '9' || *pTmp1 < '0')
         pTmp2 = pTmp1;
      }

// If the value ended with a NULL, then the value is NULL also.
   if(*pTmp1 == '\0')
      return NULL;
// If the separator wasn't a NULL, then check what it was.
// skip past blanks before the value.
   else
      {
// If terminated by a blank, then terminate the option and skip past the blanks.
      if(*pTmp1 == ' ' || *pTmp1 == '\t')
         {
         *pTmp1++ = '\0';
         for(; *pTmp1 == ' ' || *pTmp1 == '\t'; pTmp1++)
            ;
         }

// Now, there should be a = sign or a NULL, else the value is NULL.
      if(*pTmp1 == '=')
         {
         pTmp1++;

// Skip past leading blanks.
         for(; *pTmp1 == ' ' || *pTmp1 == '\t'; pTmp1++)
            ;

// If we hit a NULL now, then return a NULL string.
         if(*pTmp1 == '\0')
            return NULL;
         }
      else
         return NULL;
      }

   return pTmp1;
   } // End of GetOptionValueString().


/*
 * Function: TranslateString()
 * Translate escape sequences in strings.
 * Arguments:
 * char *pStr - The string to translate.
 * Returns:
 * char * - A pointer to a translated string. It is the callers responsibility
 *  to free this.
 */
char *TranslateString(char *pStr)
   {
   char *pRet, *pIn, *pOut;
   char cQuote;
   int nTmp;

// Find first non-blank and see what it is. If a single quote, then this is a quoted string.
   for(pIn = pStr; *pIn == ' ' || *pIn == '\t'; pIn++)
      ;

// If this was a quote, then this is a quoted string.
   if(*pIn == '\'' || *pIn == '\"')
      {
      cQuote = *pIn;
      pIn++;
// Check for malloc errors. Treat erros as invalid strings, which is OK.
      if((pRet = pIn = strdup(pIn)) == NULL)
         return NULL;
      }
   else
      {
// Check for malloc errors. Treat erros as invalid strings, which is OK.
      if((pIn = pRet = strdup(pStr)) == NULL)
         return NULL;
      cQuote = '\0';
      }

// Now, loop for all characters until we find an end to it.
   for(pOut = pIn; *pIn != '\0' && *pIn != cQuote; pIn++, pOut++)
      {
      if(*pIn == '\\')
         {
         pIn++;
// Handle cr/lf.
         if(*pIn == 'n')
            *pOut = '\n';
// Handle cr.
         else if(*pIn == 'M')
            *pOut = '\015';
// Handle backslash.
         else if(*pIn == '\\')
            *pOut = '\\';
// Handle escape.
         else if(*pIn == '[')
            *pOut = '\033';
// Handle character specified as an octal number.
         else if(*pIn >= '0' && *pIn <= '9'
           && *(pIn + 1) >= '0' && *(pIn + 1) <= '9'
           && *(pIn + 2) >= '0' && *(pIn + 2) <= '9')
            {
            nTmp = (*pIn - '0') * 64 + (*(pIn + 1) - '0') * 8
              + (*(pIn + 2) - '0');
            if(nTmp > 255)
               return NULL;
            *pOut = (char) nTmp;
            pIn += 2;
            }
// Check if this is an escaped quote of some kind.
         else if(*pIn == '\'' || *pIn == '"')
            *pOut = *pIn;
// All other are just anded to become a control sequence.
         else
            *pOut = *pIn & 0x1F;
         }
      else
         *pOut = *pIn;
      }

// Check for a proper termination.
   if(cQuote != *pIn)
      {
      free(pRet);
      return NULL;
      }

   *pOut = '\0';
   return pRet;
   } // End of TranslateString().


/*
 * Function: UntranslateString()
 * Untranslate escape sequences in strings.
 * Arguments:
 * char *pStr - The string to untranslate.
 * Returns:
 * char * - A pointer to a untranslated string. It is the callers responsibility
 *  to free this.
 */
char *UntranslateString(char *pStr)
   {
   char *pRet, *pIn, *pOut;

// If the input is NULL, then this is what we return.
   if(pStr == NULL)
      return NULL;

// Check if translation is needed at all.
   for(pIn = pStr; *pIn != '\0'; pIn++)
      {
      if(*pIn == '\n' || *pIn == '\\' || *pIn == '\'' || *pIn == '"'
        || *pIn == 0x1B || *pIn < 0x020 || *pIn == ' ' || *pIn == '\t')
         break;
      }

// If translation is not necessary, then just return a copy of what we have.
// Empty string must be translated also!
   if(*pStr != '\0' && *pIn == '\0')
      return strdup(pStr);

// The returned string will be at the very most 4 times the length of the
// input string + 2.
   pOut = pRet = (char *) malloc(strlen(pStr) * 4 + 3);
   *pOut++ = '\'';
   for(pIn = pStr; *pIn != '\0'; pIn++, pOut++)
      {
      if(*pIn == '\n' || *pIn == '\\' || *pIn == '\'' || *pIn == '"')
         {
         *pOut++ = '\\';
         *pOut = *pIn;
         }
      else if(*pIn == 0x1B)
         {
         *pOut++ = '\\';
         *pOut = '[';
         }
      else if(*pIn < 0x20)
         {
         *pOut++ = '\\';
         *pOut++ = '0';
         *pOut++ = '0' + (*pIn / 8);
         *pOut = '0' + (*pIn % 8);
         }
      else
         *pOut = *pIn;
      }
   *pOut++ = '\'';
   *pOut = '\0';
   return pRet;
   } // End of UntranslateString().


/*
 * Function: IntFromString()
 * Convert a string to an integer. Check string an values.
 * Arguments:
 * char *pStr - The string to convert.
 * int64_t *pnValue - A pointer to the resulting value. Might point to an uint.
 * uint32_t nType - Type, size and signed / unsigned flag of type.
 * Returns:
 * uint32_t - An error code, 0 if no error.
 */
uint32_t IntFromString(char *pStr, int64_t *pnValue, uint32_t nType)
   {
   BOOL bNegative = FALSE;
   int i;
   uint64_t nValue;
   uint64_t nValuePrev;
   uint64_t nBase = 10;
   uint64_t nMult;

// Check if these is no value.
   if(pStr == NULL || *pStr == '\0')
      {
// For non-boolean values, a value must be entered.
      if((nType & OPT_MASK_TYPE) != OPT_TYPE_BOOL)
         {
         printf("pStr = %s\n", pStr);
         return OPT_ERR_MUSTHAVEVALUE;
         }

// If there is a valid value pointer, assign a value to it.
      if(pnValue != NULL)
         *((BOOL *) pnValue) = TRUE;
      return OPT_ERR_NONE;
      }
   else if((nType & OPT_MASK_TYPE) == OPT_TYPE_BOOL
     && strcasecmp(pStr, "false") == 0)
      {
      if(pnValue != NULL)
         *((BOOL *) pnValue) = FALSE;
      return OPT_ERR_NONE;
      }
   else if((nType & OPT_MASK_TYPE) == OPT_TYPE_BOOL
     && strcasecmp(pStr, "true") == 0)
      {
      if(pnValue != NULL)
         *((BOOL *) pnValue) = TRUE;
      return OPT_ERR_NONE;
      }
// Check leading part of string.
   if(pStr[0] == '0' && pStr[1] == 'x')
      {
      nBase = 16;
      pStr += 2;
      }
   else if(pStr[0] == '-')
      {
// Only signed INT can have negative values.
      if((nType & OPT_FLAG_UNSIGNED) == OPT_FLAG_UNSIGNED)
         return OPT_ERR_WRONGINTVALUE;
      bNegative = TRUE;
      pStr++;

// Check if hex again.
      if(pStr[0] == '0' && pStr[1] == 'x')
         {
         nBase = 16;
         pStr += 2;
         }
      }

// Loop through the string and compute the value and check the format.
   nValue = nValuePrev = 0;
   nMult = 1;
   for(i = strlen(pStr) - 1; i >= 0; i--)
      {
// Check character.
      if((nType & OPT_MASK_TYPE) == OPT_TYPE_BOOL
        && (pStr[i] < '0' || pStr[i] > '1'))
         return OPT_ERR_WRONGBOOLVALUE;
      else if((nBase == 16 && !((pStr[i] >= '0' && pStr[i] <= '9')
        || (pStr[i] >= 'A' && pStr[i] <= 'F') || (pStr[i] >= 'a'
        && pStr[i] <= 'f')))
        || (nBase != 16 && (pStr[i] < '0' || pStr[i] > '9')))
         return OPT_ERR_WRONGINTFMT;

// Check for overflow, but allow leading overflow zeros.
      if(pStr[i] != '0' && nMult == 0)
         return OPT_ERR_INTTOOLARGE;

// Compute the value.
      nValue += nMult * (pStr[i] - ((pStr[i] >= '0' && pStr[i] <= '9') ? '0' :
        ((pStr[i] >= 'a' && pStr[i] <= 'f') ? 'a' : 'A') - 10));

      nMult *= nBase;
      }

// Check the value range.
   if((bNegative && nValue > 0x7FFFFFFFFFFFFFFFLL)
     || ((nType & OPT_FLAG_UNSIGNED) == OPT_FLAG_NONE
       && ((((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZE64
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZELONG && sizeof(long) == 8)
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZEINT && sizeof(int) == 8))
       && nValue > 0x7FFFFFFFFFFFFFFFLL)
       || (((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZE32
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZELONG && sizeof(long) == 4)
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZEINT && sizeof(int) == 4)
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZESHORT && sizeof(short) == 4))
       && nValue > 0x7FFFFFFFLL)
       || (((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZE16
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZEINT && sizeof(int) == 2)
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZESHORT && sizeof(short) == 2))
       && nValue > 0x7FFFLL)
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZE8 && nValue > 0x7F)))
     || ((nType & OPT_FLAG_UNSIGNED) == OPT_FLAG_UNSIGNED
       && ((((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZE32
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZELONG && sizeof(long) == 4)
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZEINT && sizeof(int) == 4)
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZESHORT && sizeof(short) == 4))
       && nValue > 0xFFFFFFFFLL)
       || (((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZE16
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZEINT && sizeof(int) == 2)
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZESHORT && sizeof(short) == 2))
       && nValue > 0xFFFFLL)
       || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZE8 && nValue > 0xFF))))
      return OPT_ERR_INTTOOLARGE;

// Assign the value.
   if((nType & OPT_MASK_TYPE) == OPT_TYPE_BOOL)
      *((BOOL *) pnValue) = (BOOL) nValue == 0 ? 0 : 1;
   else if((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZE64
     || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZEINT && sizeof(int) == 8)
     || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZELONG && sizeof(long) == 8))
      *pnValue = bNegative ? -nValue : nValue;
   else if((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZE32
     || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZESHORT && sizeof(short) == 4)
     || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZEINT && sizeof(int) == 4)
     || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZELONG && sizeof(long) == 4))
      *((int32_t *) pnValue) = (int32_t) (bNegative ? -nValue : nValue);
   else if((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZE16
     || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZESHORT && sizeof(short) == 2)
     || ((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZEINT && sizeof(int) == 2))
      *((int16_t *) pnValue) = (int16_t) (bNegative ? -nValue : nValue);
   else if((nType & OPT_MASK_SIZE) == OPT_FLAG_SIZE8)
      *((int8_t *) pnValue) = (int8_t) (bNegative ? -nValue : nValue);
   else
      return OPT_ERR_UNKNOWNINTSIZE;

   return OPT_ERR_NONE;
   } // End of IntFromString().


/*
 * Function: PrintOptionError()
 * Print an errormessage.
 * Arguments:
 * uint32_t nErr - Error id.
 * POPTIONS pOpt - Option.
 * int nIndex - Index for indexed options.
 * char *pValue - Value string.
 * char *pFile - Filename - If NULL use commandline.
 * int nLine - Line# in file, ignored if pFile is NULL.
 * Returns:
 * uint32_t - The error code passed in nErr
 */
uint32_t PrintOptionError(uint32_t nErr, POPTIONS pOpt, int nIndex,
  char *pValue, char *pFile, int nLine)
   {
   char *pErr;

// Check if message has already been printed.
   if((nErr & OPT_ERR_PRINTED) == OPT_ERR_PRINTED)
      return nErr;

// Print option and index name.
   if(pOpt != NULL && pOpt->pName != NULL)
      fprintf(stderr, "Option %s: ", ou_GetOptionName(pOpt, nIndex));

// If this wasn't really an error, then just return.
   if(nErr == OPT_ERR_NONE)
      return nErr;
// Check for user-defined errors.
   else if((nErr & OPT_ERR_USER) == OPT_ERR_USER)
      fprintf(stderr, "User defined error: %d", nErr & ~OPT_ERR_USER);
// Check for special treatment of some errors.
   else if(nErr == OPT_ERR_OPENFILE || nErr == OPT_ERR_READFILE)
      {
      pErr = strerror(errno);
      fprintf(stderr, g_pError[nErr], pErr, pValue);
      }
   else
      fprintf(stderr, g_pError[nErr], pValue);

// Print file and line, if specified.
   if(pFile == NULL && nLine == -1)
      fprintf(stderr, " on commandline.\n");
   else if(pFile != NULL)
      fprintf(stderr, " on line %d in file '%s'.\n", nLine, pFile);
   else
      fprintf(stderr, ".\n");

// Return error and flag it as having been printed.
   return nErr | OPT_ERR_PRINTED;
   } // End of PrintOptionError().

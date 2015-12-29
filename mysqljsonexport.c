/* Copyright (C) 2012 Anders Karlsson
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */
/*
 * Program: mysqljsonexport.c
 * Unload data from MySQL in JSON format.
 *
 * Change log
 * Who        Date       Comments
 * ========== ========== ==================================================
 * Karlsson   2012-06-05 Program created.
 * Karlsson   2012-06-09 New fixes and options.
 * Karlsson   2012-06-10 Added batching.
 * Karlsson   2012-06-12 Cleanup, commenting and structuring.
 *                       Added SIGINT handling.
 * Karlsson   2012-06-14 More cleaning.
 * Karlsson   2012-06-15 Added multi table export facility.
 * Karlsson   2012-06-19 Added parallel option.
 *                       Fixed SQL formatting.
 * Karlsson   2012-06-28 Implemented multithreading.
 *                       Added statistics printing.
 *                       Added skip-options for negative boolean options.
 *                       Added SQL_NO_CACHE option.
 * Karlsson   2012-06-29 Fixed a few bugs and made numeric types work as
 *                       expected.
 * Karlsson   2012-07-24 Fixed a few bugs and made some options work as they
 *                       do with mysqljsonimport.
 * Karlsson   2013-01-04 Added the ability to export as an array.
 */
#include "config.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <mysql.h>
#include <signal.h>
#include <limits.h>
#include <optionutil.h>
#ifdef HAVE_SYS_UTSNAME_H
#include <sys/utsname.h>
#endif

// Settings.
BOOL g_bAutoBatch;
BOOL g_bArrayFile;
BOOL g_bDryRun;
BOOL g_bSkipEmpty;
BOOL g_bSkipNull;
BOOL g_bParallel;
BOOL g_bSQLNoCache;
BOOL g_bStopOnError;
BOOL g_bStopOnInitError;
BOOL g_bUseResult;
BOOL g_bTiming;
BOOL g_bTiny1AsBool;
BOOL g_bUTF8;
BOOL g_bVersion;
unsigned int g_nLoglevel;
unsigned int g_nPort;
unsigned int g_nStats;
unsigned long g_lBatchSize;
unsigned long g_lLimit;
char **g_pConfigFile;
char **g_pSkipCol;
char **g_pColQuoted;
char **g_pColUnquoted;
char **g_pSQLInit;
char **g_pSQLThreadInit;
char **g_pSQLFinish;
char **g_pTables;
char *g_pBatchCol;
char *g_pDirectory;
char *g_pExtension;
char *g_pFile;
char *g_pIncludeFile;
char *g_pLogFile;
char *g_pHost;
char *g_pUser;
char *g_pPassword;
char *g_pSocket;
char *g_pDatabase;
char *g_pSQL;
char *g_pSQLWhereSuffix;
char *g_pDefCfgFiles[] = {
"/etc/my.cnf",
"/etc/mysql/my.cnf",
"~/.my.cnf",
"mysqljson.cnf",
NULL
};
char *g_pDefCfgFiles2[] = {
"~/.my.cnf",
"mysqljson.cnf",
NULL
};
PKEYVALUE g_pColValue;
PKEYVALUE g_pColIncr;
PKEYVALUE g_pColJSONName;

// Globals.
FILE *g_fdLog = NULL;
volatile BOOL g_bStop;

// Log levels
#define LOG_NONE 0x0000
#define LOG_STATUS 0x0001
#define LOG_ERROR 0x0002
#define LOG_INFO 0x0003
#define LOG_VERBOSE 0x0004
#define LOG_DEBUG 0x0005
#define LOG_MASK_LEVEL 0x0000000F
#define LOG_FLAG_NODATE 0x00000010
#define LOG_FLAG_STDOUT 0x00000020
#define LOG_LEVEL(X) ((X) & 0x0000000F)
#define LOG_FLAG_CHECK(X,Y) (((X) & LOG_FLAG_ ## Y) == LOG_FLAG_ ## Y)

// Statistics levels.
#define STATS_NONE 0x0000
#define STATS_NORMAL 0x0001
#define STATS_FULL 0x0002

// Column flags.
#define JSONCOL_FLAG_NONE 0x00000000
#define JSONCOL_FLAG_QUOTED 0x00000001
#define JSONCOL_FLAG_UNQUOTED 0x00000002
#define JSONCOL_FLAG_EMPTYIGN 0x00000004
#define JSONCOL_FLAG_FIXED 0x00000008
#define JSONCOL_FLAG_NULL 0x00000010
#define JSONCOL_FLAG_MYSQL 0x00000020
#define JSONCOL_FLAG_NUMERIC 0x00000040
#define JSONCOL_FLAG_INTEGER (0x00000080 | JSONCOL_FLAG_NUMERIC)
#define JSONCOL_FLAG_BOOL (0x00000100 | JSONCOL_FLAG_NUMERIC)
#define JSONCOL_FLAG_BATCH 0x00000200
#define JSONCOL_FLAG_SKIP 0x00000400
#define JSONCOL_FLAG_PK 0x00000800
#define JSONCOL_FLAG_LAST 0x10000000
#define JSONCOL_FLAG_FIXEDNULL (JSONCOL_FLAG_FIXED | JSONCOL_FLAG_NULL)
#define JSONCOL_FLAG_FIXEDNUMERIC (JSONCOL_FLAG_FIXED | JSONCOL_FLAG_NUMERIC)
#define JSONCOL_FLAG_CHECK(X,Y) (((X)->nFlags & JSONCOL_FLAG_ ## Y) == JSONCOL_FLAG_ ## Y)

typedef struct tagJSONCOL {
  char *pName;
  char *pJSONName;
  char *pValue;
  char *pPrevValue;
  unsigned long lValue;
  unsigned long lIncr;
  unsigned int nFlags;
  int nMySQLCol;
  } JSONCOL, *PJSONCOL;

typedef struct tagJSONTABLE {
  FILE *fd;
  time_t tStop;
  time_t tStart;
  char *pName;
  char *pJSONName;
  unsigned int nCols;
  PJSONCOL pCols;
  PJSONCOL pBatchCol;
  char *pSQLFormat;
  char *pSQL;
  unsigned int nSQLBufLen;
  unsigned long lBatchSize;
  unsigned long lBatch;
  unsigned long lRows;
  } JSONTABLE, *PJSONTABLE;

typedef struct tagTHREADDATA {
  pthread_t thr;
  MYSQL *pMySQL;
  PJSONTABLE pTable;
  unsigned int nRet;
  } THREADDATA, *PTHREADDATA;

// Configuration options.
OPTIONS Options[] = {
{ NULL, OPT_TYPE_CFGFILEMAIN | OPT_FLAG_CFGFILEARRAY
  | OPT_FLAG_HIDDEN | OPT_FLAG_IGNUNKNOWNOPT | OPT_FLAG_CFGREADALLDEF,
  &g_pConfigFile, (void *) g_pDefCfgFiles, NULL, (void *) "client" },
{ "array-file", OPT_TYPE_BOOL, (void *) &g_bArrayFile, (void *) FALSE,
  "Export rows in a top-level array", NULL },
{ "auto-batch", OPT_TYPE_BOOL | OPT_FLAG_HIDDEN, (void *) &g_bAutoBatch,
  (void *) TRUE, "Automatically figure out the batching options", NULL },
{ "skip-auto-batch", OPT_TYPE_BOOLREVERSE, (void *) &g_bAutoBatch,
  (void *) FALSE, "Do not automatically check for batch columns", NULL },
{ "batch-col", OPT_TYPE_STR, (void *) &g_pBatchCol, (void *) NULL,
  "Column to batch on", NULL },
{ "batch-size", OPT_TYPE_ULONG, (void *) &g_lBatchSize, (void *) 0,
  "Number of fetched rows per batch", NULL },
{ "skip-col", OPT_TYPE_STRARRAY, (void *) &g_pSkipCol, NULL,
  "Do not export the specified column", NULL },
{ "col-incr", OPT_TYPE_KEYVALUELIST, &g_pColIncr, (void *) NULL,
  "Column increment values in columnname=increment format", NULL },
{ "col-json-name", OPT_TYPE_KEYVALUELIST | OPT_FLAG_NONULL,
  (void *) &g_pColJSONName, (void *) NULL,
  "Column name map in <name>=<JSON name> format", NULL },
{ "col-value", OPT_TYPE_KEYVALUELIST, &g_pColValue, (void *) NULL,
  "Column set values in columnname=value format", NULL },
{ "col-quoted", OPT_TYPE_STRARRAY, (void *) &g_pColQuoted, (void *) FALSE,
  "Always quote this column in the output", NULL },
{ "col-unquoted", OPT_TYPE_STRARRAY, (void *) &g_pColUnquoted, (void *) FALSE,
  "Never quote this column in the output", NULL },
{ "d|database", OPT_TYPE_STR | OPT_FLAG_NODEF, &g_pDatabase, (void *) NULL,
  "Database to load data into", NULL },
{ "defaults-file", OPT_TYPE_CFGFILEMAIN | OPT_FLAG_CFGFILEARRAY
  | OPT_FLAG_CFGREADALLDEF, &g_pConfigFile, (void *) g_pDefCfgFiles2,
  "Configuration file", (void *) "jsonexport;-client" },
{ "directory", OPT_TYPE_STR | OPT_FLAG_DEFREF, (void *) &g_pDirectory,
  (void *) &g_pDatabase,
  "Directory to dump into. Default is the name of the database", NULL },
{ "dryrun", OPT_TYPE_BOOL, (void *) &g_bDryRun, (void *) FALSE,
  "Do not process any table data, just show tables / column info", NULL },
{ "skip-empty", OPT_TYPE_BOOL, &g_bSkipEmpty, (void *) FALSE,
  "Treat empty strings as non existing values", NULL },
{ "extension", OPT_TYPE_STR, (void *) &g_pExtension, (void *) ".json",
  "File extension of output file", NULL },
{ "file", OPT_TYPE_STR, (void *) &g_pFile, (void *) NULL, "Name of output file", NULL },
{ "help", OPT_TYPE_BOOL | OPT_FLAG_HELP, NULL, (void *) FALSE, "Show help",
  NULL },
{ "h|host", OPT_TYPE_STR, (void *) &g_pHost, (void *) NULL,
  "MySQL server host", NULL },
{ "include", OPT_TYPE_CFGFILE, (void *) &g_pIncludeFile, (void *) NULL,
  "Include this config file. Use to include config files for other config files",
  (void *) "jsonexport;-client" },
{ "limit", OPT_TYPE_ULONG, (void *) &g_lLimit, (void *) 0,
  "Max # of rows to fetch", NULL },
{ "logfile", OPT_TYPE_STR | OPT_FLAG_NONULL, &g_pLogFile, (void *) NULL,
  "File where log messages go", NULL },
{ "loglevel", OPT_TYPE_SEL, &g_nLoglevel, (void *) LOG_INFO,
  "Log level (status, error, info, verbose, debug)",
  (void *) ";status;error;info;verbose;debug" },
{ "skip-null", OPT_TYPE_BOOL, (void *) &g_bSkipNull, (void *) FALSE,
  "Skip NULL columns instead of exporting them as null", NULL },
{ "parallel", OPT_TYPE_BOOL | OPT_FLAG_HIDDEN, (void *) &g_bParallel,
  (void *) TRUE, "Enable parallel processing", NULL },
{ "skip-parallel", OPT_TYPE_BOOLREVERSE, (void *) &g_bParallel, (void *) FALSE,
  "Disable parallel processing", NULL },
{ "p|password", OPT_TYPE_STR, (void *) &g_pPassword, (void *) NULL,
  "MySQL Password for user", NULL },
{ "P|port", OPT_TYPE_UINT, (void *) &g_nPort, (void *) 3306, "MySQL Port",
  NULL },
{ "S|socket", OPT_TYPE_STR, (void *) &g_pSocket, (void *) NULL, "MySQL Socket",
  NULL },
{ "sql-no-cache", OPT_TYPE_BOOL | OPT_FLAG_HIDDEN, (void *) &g_bSQLNoCache,
  (void *) TRUE, "Add SQL_NO_CACHE to the SELECT.", NULL },
{ "skip-sql-no-cache", OPT_TYPE_BOOLREVERSE, (void *) &g_bSQLNoCache,
  (void *) FALSE, "Do not add SQL_NO_CACHE to the SELECT.", NULL },
{ "sql-init", OPT_TYPE_STRARRAY, (void *) &g_pSQLInit, (void *) NULL,
  "SQL Statements to run in main thread before starting dump", NULL },
{ "sql-thread-init", OPT_TYPE_STRARRAY, (void *) &g_pSQLThreadInit,
  (void *) NULL, "SQL Statements to run in each thread before starting export",
  NULL },
{ "sql", OPT_TYPE_STR, (void *) &g_pSQL, (void *) NULL,
  "SQL statement to run as export", NULL },
{ "sql-where-suffix", OPT_TYPE_STR, (void *) &g_pSQLWhereSuffix, (void *) NULL,
  "Suffix to generated SQL WHERE statement", NULL },
{ "sql-finish", OPT_TYPE_STRARRAY, (void *) &g_pSQLFinish, (void *) NULL,
  "SQL Statements to run after processing.", NULL },
{ "stats", OPT_TYPE_SEL, (void *) &g_nStats, (void *) STATS_NORMAL,
  "Statistics to show after processing (none, normal, full)",
  (void *) "none;normal;full" },
{ "stop-on-error", OPT_TYPE_BOOL | OPT_FLAG_HIDDEN, (void *) &g_bStopOnError,
  (void *) TRUE, "Stop if there is an error", NULL },
{ "skip-stop-on-error", OPT_TYPE_BOOLREVERSE, (void *) &g_bStopOnError,
  (void *) FALSE, "Continue even if there is an error", NULL },
{ "stop-on-init-error", OPT_TYPE_BOOL | OPT_FLAG_HIDDEN,
  (void *) &g_bStopOnInitError, (void *) TRUE,
  "Stop if there is an error in a SQL Init statement.", NULL },
{ "skip-stop-on-init-error", OPT_TYPE_BOOLREVERSE, (void *) &g_bStopOnInitError,
  (void *) FALSE,
  "Continue even if there is an error in a SQL Init statement.", NULL },
{ "t|table", OPT_TYPE_STRARRAY, (void *) &g_pTables, (void *) NULL,
  "Table to export. More than 1 may be specified.", NULL },
{ "skip-timing", OPT_TYPE_BOOLREVERSE | OPT_FLAG_HIDDEN, (void *) &g_bTiming,
  (void *) FALSE, "Show timing (for debugging and testing)", NULL },
{ "tiny1-as-bool", OPT_TYPE_BOOL, (void *) &g_bTiny1AsBool, (void *) FALSE,
  "Treat a tiny(1) as a bool column, exporting the strings TRUE and FALSE",
  NULL },
{ "u|user", OPT_TYPE_STR, &g_pUser, (void *) NULL, "MySQL Username", NULL },
{ "skip-utf8", OPT_TYPE_BOOLREVERSE, &g_bUTF8, (void *) TRUE,
  "Enable MySQL in UTF-8 mode", NULL },
{ "use-result", OPT_TYPE_BOOL, (void *) &g_bUseResult, (void *) FALSE,
  "Use mysql_use_result() instead of mysql_store_result().", NULL },
{ "v|version", OPT_TYPE_BOOL | OPT_FLAG_HELP, (void *) &g_bVersion,
  (void *) FALSE, "Show version", NULL },
{NULL, OPT_TYPE_NONE, NULL, NULL, NULL, NULL}};

// Function prototypes.
void PrintMsg(unsigned int nLogLevel, char *pFmt, ...);
void PrintStats(int nData);
void *RunThread(void *pData);
unsigned int ExportTable(MYSQL *pMySQL, PJSONTABLE pTable);
BOOL StringIsNumeric(char *pStr, BOOL bInt);
char *json_escape(char *pStr, char *pRet, unsigned int *pnLen);
unsigned int json_len_escaped(char *pStr);
char *BuildSQL(MYSQL *pMySQL, char *pRes, char *pPrefix, unsigned long lLimit, BOOL bQuotes, char *pBatchCol , char *pLast);
char *FormatSQL(PJSONTABLE pTable, unsigned long lLimit);
PJSONCOL FindColByName(PJSONCOL pCols, unsigned int nCols, char *pName);
BOOL SetBatchingColumn(PJSONTABLE pTable);
PJSONCOL SetColsFromResult(PJSONCOL pCols, unsigned int *pnCols, MYSQL_RES *pRes);
void PrintTableCols(FILE *fd, PJSONTABLE pTable);

int main(int argc, char *argv[])
   {
   char szTmp[256];
   char szFile[PATH_MAX + 1];
   int nRet = -1;
   int i, j;
   unsigned int nCols;
   unsigned int nMySQLCol;
   unsigned int nLen = 0;
   unsigned int nTables;
   time_t tStart, tStop;
   MYSQL *pMySQL;
   MYSQL_RES *pRes = NULL;
   MYSQL_ROW pRow;
   MYSQL_ROW_OFFSET rowOffset;
   PJSONCOL pCols;
   PJSONCOL pCol;
   PJSONTABLE pTables;
   struct sigaction sa;
   struct stat statBuf;
#ifdef HAVE_SYS_UTSNAME_H
   struct utsname utsName;
#endif
   PKEYVALUE pKeyValue;
   PTHREADDATA pThreads = NULL;

// Set NULL and default of values.
   ou_OptionArraySetNull(Options);
   ou_OptionArraySetDef(Options);

// Check commandline.
   if((nRet = ou_OptionArraySetValues(Options, &argc, argv, OPT_FLAG_NONE))
     != OPT_ERR_NONE)
      {
      if((nRet & OPT_ERR_ERRMASK) == OPT_ERR_HELPFOUND)
         {
// Check for version argument.
         if(g_bVersion)
            {
            fprintf(stderr, "Version: %s\n", PACKAGE_STRING);
#ifdef HAVE_SYS_UTSNAME_H
            uname(&utsName);
            fprintf(stderr, "Built on: %s %s %s %s\n", utsName.sysname, utsName.machine,
              utsName.release, utsName.version);
#endif
            fprintf(stderr, "Build time: " __DATE__ " " __TIME__ "\n");
            fprintf(stderr, "Built on MySQL client version: %s\n", MYSQL_VERSION);
            }
         else
            ou_OptionArrayPrintHelp(Options, stdout, OPT_FLAG_NONE);

         goto Exit;
         }

      nRet &= OPT_ERR_ERRMASK;
      fprintf(stderr, "Command line error: %d\n", nRet);
      goto ErrExit;
      }

// Reset error code to the default.
   nRet = -1;

// Add tables on commandline to list of tables.
   for(i = 1; i < argc; i++)
      ou_AddStringToArray(argv[i], &g_pTables);

// Check arguments.
   if(g_pTables != NULL && g_pSQL != NULL)
      {
      fprintf(stderr, "Either one or more tables or an SQL statement or none must be provided, but not both.\n");
      goto ShowUsage;
      }
   if(g_pSQL == NULL && g_pTables != NULL && g_pTables[1] != NULL && g_pFile != NULL)
      {
      fprintf(stderr, "When you specify an output file, you may only specify one table.\n");
      goto ShowUsage;
      }
   if(g_pSQL != NULL && g_pFile == NULL)
      {
      fprintf(stderr, "When you specify an SQL statement, you must also specify an output file.\n");
      goto ShowUsage;
      }
   if(g_pSQL != NULL && g_pSQLWhereSuffix != NULL)
      {
      fprintf(stderr, "You can't specify a SQL WHERE Suffix clause when using an SQL statement.");
      goto ShowUsage;
      }

// Check batching options.
   if(!g_bAutoBatch && g_pBatchCol == NULL && g_lBatchSize > 0)
      {
      fprintf(stderr, "You must specify a batch column or use automatic detection.\n");
      goto ErrExit;
      }
   if(g_pBatchCol != NULL && g_lBatchSize == 0)
      {
      fprintf(stderr, "You have to specify a batch size > 0\n");
      goto ErrExit;
      }

// Check output directory.
   if(stat(g_pDirectory, &statBuf) != 0)
      {
      if(errno == ENOENT)
         {
         if(mkdir(g_pDirectory, 0755) != 0)
            {
            fprintf(stderr, "Error creating directory %s\n", g_pDirectory);
            perror("Error");
            goto ErrExit;
            }
         }
      else
         {
         fprintf(stderr, "Error with directory %s\n", g_pDirectory);
         perror("Error");
         goto ErrExit;
         }
      }
   else if(!S_ISDIR(statBuf.st_mode))
      {
      fprintf(stderr, "Path %s is not a directory\n", g_pDirectory);
      goto ErrExit;
      }
   else if(access(g_pDirectory, W_OK) != 0)
      {
      fprintf(stderr, "Directory %s cannot be accessed.\n", g_pDirectory);
      perror("Error");
      goto ErrExit;
      }
 
// Open a logfile handle.
   if(g_pLogFile != NULL)
      {
      if((g_fdLog = fopen(g_pLogFile, "a")) == NULL)
         {
         fprintf(stderr, "Error opening logfile: %s\n", g_pLogFile);
         goto ErrExit;
         }
      }
   else
      g_fdLog = stderr;

// Connect to MySQL.
   pMySQL = mysql_init(NULL);
   if(mysql_real_connect(pMySQL, g_pHost, g_pUser, g_pPassword,
     NULL, g_nPort, g_pSocket, CLIENT_COMPRESS) == NULL)
      {
      fprintf(stderr, "MySQL Connection failed:\n%s\n", mysql_error(pMySQL));
      goto ErrExit;
      }

// Run init statements.
   for(i = 0; g_pSQLInit != NULL && g_pSQLInit[i] != NULL; i++)
      {
      PrintMsg(LOG_VERBOSE, "Running SQL Init:\n%s\n", g_pSQLInit[i]);
      if(mysql_query(pMySQL, g_pSQLInit[i]) != 0)
         {
         PrintMsg(LOG_ERROR, "SQL Error %d\n%s\nin SQL Init statement:\n%s\n",
           mysql_errno(pMySQL), mysql_error(pMySQL), g_pSQLInit[i]);
         if(g_bStopOnInitError)
            goto ErrExit;
         }
      }

// Select the specified database.
   if(mysql_select_db(pMySQL, g_pDatabase) != 0)
      {
      PrintMsg(LOG_ERROR, "SQL Error %d when selecting database %s\n%s\n",
        mysql_errno(pMySQL), g_pDatabase, mysql_error(pMySQL));
         goto ErrExit;
      }

// Run thread init statements.
   for(i = 0; g_pSQLThreadInit != NULL && g_pSQLThreadInit[i] != NULL; i++)
      {
      PrintMsg(LOG_VERBOSE, "Running SQL Init:\n%s\n", g_pSQLThreadInit[i]);
      if(mysql_query(pMySQL, g_pSQLThreadInit[i]) != 0)
         {
         PrintMsg(LOG_ERROR, "SQL Error %d\n%s\nin SQL Init statement:\n%s\n",
           mysql_errno(pMySQL), mysql_error(pMySQL), g_pSQLThreadInit[i]);
         if(g_bStopOnInitError)
            goto ErrExit;
         }
      }

// Set up UTF8.
   if(g_bUTF8)
      mysql_query(pMySQL, "SET NAMES utf8");

   if(g_pSQL == NULL)
      {
// If no tables were specified, get all tables.
      if(g_pTables == NULL)
         {
         PrintMsg(LOG_VERBOSE, "Getting all table in database %s.\n", g_pDatabase);
         if(mysql_query(pMySQL, "SHOW FULL TABLES WHERE table_type = 'BASE TABLE'") != 0)
            {
            fprintf(stderr, "MySQL SHOW TABLES failed:\n%s\n", mysql_error(pMySQL));
            goto ErrExit;
            }

         if((pRes = mysql_store_result(pMySQL)) == NULL)
            {
            fprintf(stderr, "MySQL store results failed:\n%s\n", mysql_error(pMySQL));
            goto ErrExit;
            }
         if(mysql_num_rows(pRes) == 0)
            {
            fprintf(stderr, "No tables to export in database: %s\n", g_pDatabase);
            goto Exit;
            }

// Set the list of tables.
         for(i = 0; (pRow = mysql_fetch_row(pRes)) != NULL; i++)
            ou_AddStringToArray(pRow[0], &g_pTables);

         mysql_free_result(pRes);
         }

// Count tbe tables and allocate space for them.
      for(nTables = 0; g_pTables != NULL && g_pTables[nTables] != NULL; nTables++)
         ;
      }
   else
      nTables = 1;

   if((pTables = calloc(nTables, sizeof(JSONTABLE))) == NULL)
      {
      fprintf(stderr, "Memory allocation error.\n");
      goto ErrExit;
      }

// Set up array of fixed columns.
   nCols = 0;

// Count number of non-MySQL columns.
   for(pKeyValue = g_pColValue; pKeyValue != NULL; pKeyValue = pKeyValue->pNext)
      nCols++;

// Check column increment.
   for(pKeyValue = g_pColIncr, j = 0; pKeyValue != NULL; pKeyValue = pKeyValue->pNext, j++)
      {
// Check that this increment is applied to a fixed value column.
      if(!ou_KeyValueExists(g_pColValue, pKeyValue->pKey))
         {
         fprintf(stderr, "Column increment for column %s must be applied to a fixed value column.\n",
           pKeyValue->pKey);
         goto ErrExit;
         }

// Check that all this is applied to integers only.
      if(!StringIsNumeric(pKeyValue->pValue, TRUE) || !StringIsNumeric(ou_GetKeyValue(g_pColValue, pKeyValue->pKey), TRUE))
         {
         fprintf(stderr, "Column increment for column %s must be an integer and be applied to an integer value\n",
           pKeyValue->pKey);
         goto ErrExit;
         }
      }

// Add columns with mapped names.
   for(pKeyValue = g_pColJSONName; pKeyValue != NULL; pKeyValue = pKeyValue->pNext)
      {
      if(!ou_KeyValueExists(g_pColValue, pKeyValue->pKey))
         nCols++;
      }

// Add columns to skip.
   for(j = 0; g_pSkipCol != NULL && g_pSkipCol[j] != NULL; j++)
      {
      if(!ou_KeyValueExists(g_pColValue, g_pSkipCol[j])
        && !ou_KeyValueExists(g_pColJSONName, g_pSkipCol[j]))
         nCols++;
      }

// Add column quotes.
   for(j = 0; g_pColQuoted != NULL && g_pColQuoted[j] != NULL; j++)
      {
      if(!ou_KeyValueExists(g_pColValue, g_pColQuoted[j])
        && !ou_KeyValueExists(g_pColJSONName, g_pColQuoted[j])
        && !ou_StrExistsInArray(g_pColQuoted[j], g_pSkipCol, TRUE))
         nCols++;
      }

// Add unquoted columns.
   for(j = 0; g_pColUnquoted != NULL && g_pColUnquoted[j] != NULL; j++)
      {
      if(!ou_KeyValueExists(g_pColValue, g_pColUnquoted[j])
        && !ou_KeyValueExists(g_pColJSONName, g_pColUnquoted[j])
        && !ou_StrExistsInArray(g_pColUnquoted[j], g_pSkipCol, TRUE)
        && !ou_StrExistsInArray(g_pColUnquoted[j], g_pColQuoted, TRUE))
         nCols++;
      }

// Add batching column.
   if(g_pBatchCol != NULL && !ou_KeyValueExists(g_pColValue, g_pBatchCol)
     && !ou_KeyValueExists(g_pColJSONName, g_pBatchCol)
     && !ou_StrExistsInArray(g_pBatchCol, g_pSkipCol, TRUE)
     && !ou_StrExistsInArray(g_pBatchCol, g_pColQuoted, TRUE)
     && !ou_StrExistsInArray(g_pBatchCol, g_pColUnquoted, TRUE))
      nCols++;

// Allocate columns.
   if((pCols = calloc(nCols, sizeof(JSONCOL))) == NULL)
      {
      fprintf(stderr, "Memory allocation error.\n");
      goto ErrExit;
      }

// Clear columns.
   for(j = 0; j < nCols; j++)
      {
      pCols[j].pName = NULL;
      pCols[j].pJSONName = NULL;
      pCols[j].pValue = NULL;
      pCols[j].pPrevValue = NULL;
      pCols[j].lValue = 0;
      pCols[j].lIncr = 0;
      pCols[j].nFlags = JSONCOL_FLAG_NONE;
      pCols[j].nMySQLCol = -1;
      }

// Set fixed columns.
   for(pKeyValue = g_pColValue, j = 0; pKeyValue != NULL;
     pKeyValue = pKeyValue->pNext, j++)
      {
      pCols[j].nFlags |= JSONCOL_FLAG_FIXED;
      pCols[j].pName = pKeyValue->pKey;

      if((pCols[j].pJSONName = json_escape(pKeyValue->pKey, NULL, NULL))
        == NULL)
         {
         fprintf(stderr, "Memory allocation error.\n");
         goto ErrExit;
         }
      if(StringIsNumeric(pKeyValue->pValue, TRUE))
         {
         pCols[j].lValue = atol(pKeyValue->pValue);
         pCols[j].nFlags |= JSONCOL_FLAG_INTEGER;
         }
      else if(StringIsNumeric(pKeyValue->pValue, FALSE))
         {
         pCols[j].pValue = pKeyValue->pValue;
         pCols[j].nFlags |= JSONCOL_FLAG_NUMERIC;
         }
      else if((pCols[j].pValue = json_escape(pKeyValue->pValue, NULL, NULL))
        == NULL)
         {
         fprintf(stderr, "Memory allocation error.\n");
         goto ErrExit;
         }

// Add column increment.
      if(ou_KeyValueExists(g_pColIncr, pKeyValue->pKey))
        pCols[j].lIncr = atol(ou_GetKeyValue(g_pColIncr, pKeyValue->pKey));
      }

// Set JSON names of columns.
   for(pKeyValue = g_pColJSONName; pKeyValue != NULL;
     pKeyValue = pKeyValue->pNext)
      {
      pCol = FindColByName(pCols, nCols, pKeyValue->pKey);
      if(pCol->pName == NULL)
         pCol->pName = pKeyValue->pKey;
      if(pCol->pJSONName != NULL)
         free(pCol->pJSONName);
      if((pCol->pJSONName = json_escape(pKeyValue->pValue, NULL, NULL)) == NULL)
         {
         fprintf(stderr, "Memory allocation error.\n");
         goto ErrExit;
         }
      }

// Set column names to skip.
   for(j = 0; g_pSkipCol != NULL && g_pSkipCol[j] != NULL; j++)
      {
      pCol = FindColByName(pCols, nCols, g_pSkipCol[j]);
      if(pCol->pName == NULL)
         {
         pCol->pName = g_pSkipCol[j];
         if((pCol->pJSONName = json_escape(g_pSkipCol[j], NULL, NULL)) == NULL)
            {
            fprintf(stderr, "Memory allocation error.\n");
            goto ErrExit;
            }
         }

      pCol->nFlags |= JSONCOL_FLAG_SKIP;
      }

// Add column quote / unquote attribute.
   for(j = 0; g_pColQuoted != NULL && g_pColQuoted[j] != NULL; j++)
      {
      pCol = FindColByName(pCols, nCols, g_pColQuoted[j]);
      if(pCol->pName == NULL)
         {
         pCol->pName = g_pColQuoted[j];
         if((pCol->pJSONName = json_escape(g_pColQuoted[j], NULL, NULL))
           == NULL)
            {
            fprintf(stderr, "Memory allocation error.\n");
            goto ErrExit;
            }
         }

      pCol->nFlags |= JSONCOL_FLAG_QUOTED;
      }

   for(j = 0; g_pColUnquoted != NULL && g_pColUnquoted[j] != NULL; j++)
      {
      pCol = FindColByName(pCols, nCols, g_pColUnquoted[j]);
      if(pCol->pName == NULL)
         {
         pCol->pName = g_pColUnquoted[j];
         if((pCol->pJSONName = json_escape(g_pColUnquoted[j], NULL, NULL))
           == NULL)
            {
            fprintf(stderr, "Memory allocation error.\n");
            goto ErrExit;
            }
         }

      pCol->nFlags |= JSONCOL_FLAG_UNQUOTED;
      }

// Add batching column.
   if(g_pBatchCol != NULL)
      {
      pCol = FindColByName(pCols, nCols, g_pBatchCol);
      if(pCol->pName == NULL)
         {
         pCol->pName = g_pBatchCol;
         if((pCol->pJSONName = json_escape(g_pBatchCol, NULL, NULL)) == NULL)
            {
            fprintf(stderr, "Memory allocation error.\n");
            goto ErrExit;
            }
         }

      pCol->nFlags |= JSONCOL_FLAG_BATCH;
      }

// Initialize tables array.
   PrintMsg(LOG_DEBUG, "Setting up array for all %d tables.\n", nTables);
   for(i = 0; i < nTables; i++)
      {
// Set up basic table data,
      pTables[i].pSQLFormat = NULL;
      pTables[i].pSQL = NULL;
      pTables[i].pBatchCol = NULL;
      pTables[i].nSQLBufLen = 0;
      pTables[i].lBatchSize = 0;
      pTables[i].lRows = 0;
      pTables[i].nCols = nCols;

      if(g_pSQL != NULL)
         {
         pTables[i].pSQLFormat = g_pSQL;
         pTables[i].pName = pTables[i].pJSONName = NULL;

// Allocate space for columns.
         if((pTables[i].pCols = calloc(pTables[i].nCols, sizeof(JSONCOL)))
           == NULL)
            {
            fprintf(stderr, "Memory allocation error.\n");
            goto ErrExit;
            }

// Copy fixed columns.
         for(j = 0; j < nCols; j++)
            memcpy(&pTables[i].pCols[j], &pCols[j], sizeof(JSONCOL));

// If we are to use batchning, then we need to figure out the batching column.
         if(g_lBatchSize > 0)
            {
// Format the SQL statement with a single row limit.
            FormatSQL(&pTables[i], 1);
            PrintMsg(LOG_DEBUG, "Formated SQL: %s\n", pTables[i].pSQL);

// Execute the query.
            if(mysql_query(pMySQL, pTables[i].pSQL) != 0)
               {
               fprintf(stderr, "MySQL %s failed:\n%s\n", pTables[i].pSQL,
                 mysql_error(pMySQL));
               nRet = mysql_errno(pMySQL);
               goto ErrExit;
               }

            if((pRes = mysql_store_result(pMySQL)) == NULL)
               {
               fprintf(stderr, "MySQL SHOW COLUMNS store results failed:\n%s\n",
                 mysql_error(pMySQL));
               nRet = mysql_errno(pMySQL);
               goto ErrExit;
               }

// Get the columns.
            if((pTables[i].pCols = SetColsFromResult(pTables[i].pCols,
              &pTables[i].nCols, pRes)) == NULL)
               goto ErrExit;
            }
         }
      else
         {
         pTables[i].pSQLFormat = NULL;
         pTables[i].pName = g_pTables[i];
         if((pTables[i].pJSONName = json_escape(pTables[i].pName, NULL, NULL))
           == NULL)
            {
            fprintf(stderr, "Memory allocation error.\n");
            goto ErrExit;
            }

// If all we have is a table name, we have to build an SQL statement.
// First, compute size of SQL statement.
         sprintf(szTmp, "SHOW COLUMNS FROM %s", pTables[i].pName);
         if(mysql_query(pMySQL, szTmp) != 0)
            {
            fprintf(stderr, "MySQL %s failed:\n%s\n", szTmp,
              mysql_error(pMySQL));
            nRet = mysql_errno(pMySQL);
            goto ErrExit;
            }

         if((pRes = mysql_store_result(pMySQL)) == NULL)
            {
            fprintf(stderr, "MySQL SHOW COLUMNS store results failed:\n%s\n",
              mysql_error(pMySQL));
            nRet = mysql_errno(pMySQL);
            goto ErrExit;
            }

// Count SQL columns.
         rowOffset = mysql_row_tell(pRes);
         while((pRow = mysql_fetch_row(pRes)) != NULL)
            {
            if((pCol = FindColByName(pCols, nCols, pRow[0])) == NULL
              || pCol->pName == NULL)
               pTables[i].nCols++;
            }

// Allocate space for all columns.
         if((pTables[i].pCols = calloc(pTables[i].nCols, sizeof(JSONCOL)))
           == NULL)
            {
            fprintf(stderr, "Memory allocation error.\n");
            goto ErrExit;
            }

// Copy fixed columns.
         for(j = 0; j < nCols; j++)
            memcpy(&pTables[i].pCols[j], &pCols[j], sizeof(JSONCOL));

// Clean the MySQL Columns.
         for(; j < pTables[i].nCols; j++)
            {
            pTables[i].pCols[j].pName = NULL;
            pTables[i].pCols[j].pJSONName = NULL;
            pTables[i].pCols[j].pValue = NULL;
            pTables[i].pCols[j].pPrevValue = NULL;
            pTables[i].pCols[j].lValue = 0;
            pTables[i].pCols[j].lIncr = 0;
            pTables[i].pCols[j].nFlags = JSONCOL_FLAG_NONE;
            pTables[i].pCols[j].nMySQLCol = -1;
            }

// Seek to the start of the result and then add the columns to the SQL
// statement.
         mysql_row_seek(pRes, rowOffset);
         while((pRow = mysql_fetch_row(pRes)) != NULL)
            {
// This shouldn't happen, but just to be sure..
            if((pCol = FindColByName(pTables[i].pCols, pTables[i].nCols,
              pRow[0])) == NULL)
               continue;

// Set column name of this column is unknown.
            if(pCol->pName == NULL)
               pCol->pName = strdup(pRow[0]);

            if(pCol->pJSONName == NULL && (pCol->pJSONName
              = json_escape(pRow[0], NULL, NULL)) == NULL)
               {
               fprintf(stderr, "Memory allocation error.\n");
               goto ErrExit;
               }
            pCol->nFlags |= JSONCOL_FLAG_MYSQL;

// Check if this is part of a primary key.
            if(strcmp(pRow[3], "PRI") == 0)
               pCol->nFlags |= JSONCOL_FLAG_PK;
            }

// Now get the batching column.
         if(g_lBatchSize > 0 && SetBatchingColumn(&pTables[i]))
            goto ErrExit;

// Free the SHOW COLUMNS result.
         mysql_free_result(pRes);

// SELECT<space>/*!40001 SQL_NO_CACHE */<space>
         nLen = 32;

// Add size of all columns, unless they shoudl be skipped and are not batching
// columns.
         for(j = 0; j < pTables[i].nCols; j++)
            {
            if(JSONCOL_FLAG_CHECK(&pTables[i].pCols[j], MYSQL)
              && (JSONCOL_FLAG_CHECK(&pTables[i].pCols[j], BATCH)
              || !JSONCOL_FLAG_CHECK(&pTables[i].pCols[j], SKIP)))
               nLen += strlen(pTables[i].pCols[j].pName) + 4;
            }

// FROM<space>`<table name>`%<W|w>%O
         nLen += strlen(pTables[i].pName) + 11;

// Allocate space for the SQL statement.
         if((pTables[i].pSQLFormat = malloc(nLen + 1)) == NULL)
            {
            fprintf(stderr, "Memory allocation error.\n");
            goto ErrExit;
            }

// Create the SQL statement.
         strcpy(pTables[i].pSQLFormat,
           g_bSQLNoCache ? "SELECT /*!40001 SQL_NO_CACHE */ " : "SELECT ");

// Add the columns now.
         nMySQLCol = 0;
         for(j = 0; j < pTables[i].nCols; j++)
            {
            if(JSONCOL_FLAG_CHECK(&pTables[i].pCols[j], MYSQL)
              && (JSONCOL_FLAG_CHECK(&pTables[i].pCols[j], BATCH)
              || !JSONCOL_FLAG_CHECK(&pTables[i].pCols[j], SKIP)))
               {
               sprintf(&pTables[i].pSQLFormat[strlen(pTables[i].pSQLFormat)],
                 "%s`%s`", nMySQLCol == 0 ? "" : ", ",
                 pTables[i].pCols[j].pName);

// Set the column # in the result set.
               pTables[i].pCols[j].nMySQLCol = nMySQLCol++;
               }
            }

// Check that there are columns to select.
         if(nMySQLCol == 0)
            {
            fprintf(stderr, "Table %s has no columns to select.\n",
              pTables[i].pName);
            goto ErrExit;
            }

         strcat(pTables[i].pSQLFormat, " FROM `");
         strcat(pTables[i].pSQLFormat, pTables[i].pName);
         strcat(pTables[i].pSQLFormat,
           g_pSQLWhereSuffix == NULL ? "`%w%O" : "`%W%O");
         }

      if(g_lBatchSize > 0 && SetBatchingColumn(&pTables[i]))
         goto ErrExit;
      }

// If we are just checking columns and SQL statements, then do that now and then exit.
   if(g_bDryRun)
      {
      for(i = 0; i < nTables; i++)
         {
// Format the SQL statement, but limit to 1 row only.
         FormatSQL(&pTables[i], 1);

         if(mysql_query(pMySQL, pTables[i].pSQL) != 0)
            {
            fprintf(stderr, "MySQL Error:%s\nin:%s\n", mysql_error(pMySQL),
              pTables[i].pSQL);
            nRet = mysql_errno(pMySQL);
            goto ErrExit;
            }
         if((pRes = mysql_store_result(pMySQL)) == NULL)
            {
            fprintf(stderr, "MySQL store results failed:\n%s\nin:%s\n",
              mysql_error(pMySQL), pTables[i].pSQL);
            nRet = mysql_errno(pMySQL);
            goto ErrExit;
            }
         if((pTables[i].pCols = SetColsFromResult(pTables[i].pCols,
           &pTables[i].nCols, pRes)) == NULL)
            goto ErrExit;
         if(i > 0)
            fprintf(stdout, "\n");
         PrintTableCols(stdout, &pTables[i]);
         mysql_free_result(pRes);
         }
      goto Exit;
      }

   if(g_bParallel)
      {
// Set up threads array.
      if((pThreads = calloc(nTables, sizeof(THREADDATA))) == NULL)
         {
         fprintf(stderr, "Memory allocation error.\n");
         goto ErrExit;
         }

// Set up the individual threads.
      for(i = 0; i < nTables; i++)
         {
         pThreads[i].pTable = &pTables[i];
         pThreads[i].nRet = 0;

// Connect to MYSQL.
         pThreads[i].pMySQL = mysql_init(NULL);
         if(mysql_real_connect(pThreads[i].pMySQL, g_pHost, g_pUser,
           g_pPassword, g_pDatabase, g_nPort, g_pSocket, CLIENT_COMPRESS)
           == NULL)
            {
            fprintf(stderr, "MySQL Connection failed:\n%s\n",
              mysql_error(pThreads[i].pMySQL));
            goto ErrExit;
            }

// Run init statements.
         for(j = 0; g_pSQLThreadInit != NULL && g_pSQLThreadInit[j] != NULL;
           i++)
            {
            PrintMsg(LOG_VERBOSE, "Running SQL Init:\n%s\n",
              g_pSQLThreadInit[j]);
            if(mysql_query(pThreads[i].pMySQL, g_pSQLThreadInit[j]) != 0)
               {
               PrintMsg(LOG_ERROR,
                 "SQL Error %d\n%s\nin SQL Init statement:\n%s\n",
                 mysql_errno(pThreads[i].pMySQL),
                 mysql_error(pThreads[i].pMySQL), g_pSQLThreadInit[j]);
               if(g_bStopOnInitError)
                  goto ErrExit;
               }
            }

// Set up UTF8.
         if(g_bUTF8)
            mysql_query(pThreads[i].pMySQL, "SET NAMES utf8");
         }
      }

/* Set up signal handlers. */
   sa.sa_handler = PrintStats;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = SA_RESTART;
   if(sigaction(SIGUSR1, &sa, NULL) != 0)
      {
      fprintf(stderr, "Error when setting up signal handler\n");
      goto ErrExit;
      }
   if(sigaction(SIGINT, &sa, NULL) != 0)
      {
      fprintf(stderr, "Error when setting up signal handler\n");
      goto ErrExit;
      }

// Now, do the actual export.
   tStart = g_bTiming ? time(NULL) : 0;
   for(i = 0; i < nTables; i++)
      {
// Create and open export file.
      if(g_pFile != NULL)
         {
         if((pTables[i].fd = fopen(g_pFile, "w")) == NULL)
            {
            fprintf(stderr, "Error opening file %s.\n", g_pFile);
            perror("File open error");
            goto ErrExit;
            }
         }
      else
         {
         strcpy(szFile, g_pDirectory);
         strcat(szFile, "/");
         strcat(szFile, pTables[i].pName);
         strcat(szFile, g_pExtension);

         if((pTables[i].fd = fopen(szFile, "w")) == NULL)
            {
            fprintf(stderr, "Error opening file %s.\n", szFile);
            perror("File open error");
            goto ErrExit;
            }
         }

// Are we running in parallel?
      if(g_bParallel)
         {
         if((nRet = pthread_create(&pThreads[i].thr, NULL, RunThread,
           (void *) &pThreads[i])) != 0)
            {
            PrintMsg(LOG_ERROR, "pthread_create() error: %d\n", nRet);
            goto ErrExit;
            }
         }
      else
         if((nRet = ExportTable(pMySQL, &pTables[i])) != 0)
            goto ErrExit;
      }

// Wait for threads if we are running in parallel.
   nRet = 0;
   if(g_bParallel)
      {
      for(i = 0; i < nTables; i++)
         {
         pthread_join(pThreads[i].thr, NULL);
         if(pThreads[i].nRet != 0)
            nRet = pThreads[i].nRet;
         }
      }

// Run SQL statenents to finish up.
   for(i = 0; g_pSQLFinish != NULL && g_pSQLFinish[i] != NULL; i++)
      {
      PrintMsg(LOG_VERBOSE, "Running SQL Finish:\n%s\n", g_pSQLFinish[i]);
      if(mysql_query(pMySQL, g_pSQLFinish[i]) != 0)
         {
         PrintMsg(LOG_ERROR, "SQL Error %d\n%s\nin SQL Finish statement:\n%s\n",
           mysql_errno(pMySQL), mysql_error(pMySQL), g_pSQLFinish[i]);
         }
      }

// Register end of time for processing.
   tStop = g_bTiming ? time(NULL) : 0;

// Show statistics.
   if(g_nStats > STATS_NONE)
      {
      unsigned long lRows = 0;
      unsigned long lBatches = 0;

      for(i = 0; i < nTables; i++)
         {
         if(g_nStats == STATS_FULL)
            fprintf(stderr, "Table: %s Rows: %ld Batches: %ld in %ld seconds\n",
              pTables[i].pName, pTables[i].lRows, pTables[i].lBatch + 1,
              pTables[i].tStop - pTables[i].tStart);
         lBatches += pTables[i].lBatch + 1;
         lRows += pTables[i].lRows;
         }

      fprintf(stderr,
        "Tables exported: %d, Rows: %ld, Batches: %ld in %ld seconds\n",
        nTables, lRows, lBatches, tStop - tStart);
      }

// Now check the return value.
   if(nRet != 0)
      goto ErrExit;

Exit:
   if(g_fdLog != NULL && g_fdLog != stderr)
      fclose(g_fdLog);
   ou_OptionArrayFree(Options);
   return 0;

ShowUsage:
// Show usage and options.
   fprintf(stderr, "Usage: %s [<options>] [<json file>]\n", argv[0]);

ErrExit:
   if(g_fdLog != NULL && g_fdLog != stderr)
      fclose(g_fdLog);
   ou_OptionArrayFree(Options);

   return nRet;
   } // End of main()


/*
 * Function: RunThread()
 * This function will do the actual running of the thread,
 * handling sybchronization and stuff.
 * Arguments:
 * void *pData - A pointer to the PTHREADDATA that defines this thread.
 * Returns:
 * void * - NULL
 */
void *RunThread(void *pData)
   {
   PTHREADDATA pThr = (PTHREADDATA) pData;

// Export the table.
   pThr->nRet = ExportTable(pThr->pMySQL, pThr->pTable);

// Close the MySQL connection.
   mysql_close(pThr->pMySQL);

   return NULL;
   } // End of RunThread()


/*
 * Function: ExportTable()
 * Export a MySQL table to a specified file.
 * Arguments:
 * MYSQL *pMySQL - The MySQL Connection to use.
 * PJSONTABLE pTable - The table to export.
 * Returns:
 * unsigned int - An error code, 0 if there was no error.
 */
unsigned int ExportTable(MYSQL *pMySQL, PJSONTABLE pTable)
   {
   BOOL bFirstCol;
   unsigned int nRet = -1;
   unsigned int i;
   unsigned int nJSONBufSize = 0;
   unsigned long lBatchRows;
   unsigned long lBatchLimit;
   char *pJSONBuf = NULL;
   MYSQL_RES *pRes;
   MYSQL_ROW pRow;

// Format the first SQL statement.
   lBatchLimit = (g_lLimit > 0 && (g_lLimit < pTable->lBatchSize
     || pTable->lBatchSize == 0)) ? g_lLimit : pTable->lBatchSize;
   FormatSQL(pTable, lBatchLimit);

   pTable->tStart = g_bTiming ? time(NULL) : 0;
// If we are exporting as an array, the write the array leader now.
   if(g_bArrayFile)
      fprintf(pTable->fd, "[\n");

// Loop for all batches.
   for(pTable->lBatch = 0; !g_bStop; pTable->lBatch++)
      {
      PrintMsg(LOG_DEBUG, "Stmt: Batch %ld (limit: %ld)\n  SQL: %s\n",
        pTable->lBatch, lBatchLimit, pTable->pSQL);
      if(mysql_query(pMySQL, pTable->pSQL) != 0)
         {
         fprintf(stderr, "MySQL Error:%s\nin:%s\n", mysql_error(pMySQL),
           pTable->pSQL);
         nRet = mysql_errno(pMySQL);
         goto ErrExit;
         }

// Get the SQL result.
      if(g_bUseResult)
         {
         if((pRes = mysql_use_result(pMySQL)) == NULL)
            {
            fprintf(stderr, "MySQL store results failed:\n%s\nin:%s\n",
              mysql_error(pMySQL), pTable->pSQL);
            nRet = mysql_errno(pMySQL);
            goto ErrExit;
            }
         }
      else
         {
         if((pRes = mysql_store_result(pMySQL)) == NULL)
            {
            fprintf(stderr, "MySQL use results failed:\n%s\nin:%s\n",
              mysql_error(pMySQL), pTable->pSQL);
            nRet = mysql_errno(pMySQL);
            goto ErrExit;
            }
         }

// Set types of columns.
      if(pTable->lBatch == 0)
         {
         if((pTable->pCols = SetColsFromResult(pTable->pCols, &pTable->nCols,
           pRes)) == NULL)
            {
            fprintf(stderr, "Memory allocation error.\n");
            goto ErrExit;
            }
         }

// Now, get the rows.
      lBatchRows = 0;
      while((pRow = mysql_fetch_row(pRes)) != NULL && !g_bStop)
         {
         bFirstCol = TRUE;

// Set column values.
         for(i = 0; i < pTable->nCols; i++)
            {
            if(!JSONCOL_FLAG_CHECK(&pTable->pCols[i], MYSQL))
               continue;

// Save as next value to use for batching.
            if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], BATCH)
              && pTable->lBatchSize > 0
              && (lBatchRows + 1) == mysql_num_rows(pRes))
               {
               if(pTable->pCols[i].pPrevValue != NULL)
                  free(pTable->pCols[i].pPrevValue);
               if(pRow[pTable->pCols[i].nMySQLCol] == NULL)
                  {
                  fprintf(stderr,"Record %ld in table %s has batching column as NULL. Stopping.\n",
                    pTable->lRows, pTable->pName);
                  goto ErrExit;
                  }
               pTable->pCols[i].pPrevValue
                 = strdup(pRow[pTable->pCols[i].nMySQLCol]);
               }

// Ignore skipped columns.
            if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], SKIP))
               continue;

// Set value of column.
            if(pRow[pTable->pCols[i].nMySQLCol] == NULL)
               pTable->pCols[i].nFlags |= JSONCOL_FLAG_NULL;
            else
               {
               pTable->pCols[i].nFlags &= ~JSONCOL_FLAG_NULL;
               pJSONBuf = json_escape(pRow[pTable->pCols[i].nMySQLCol], pJSONBuf, &nJSONBufSize);
               if(pTable->pCols[i].pValue != NULL)
                  free(pTable->pCols[i].pValue);
               pTable->pCols[i].pValue = strdup(pJSONBuf);
               }
            }

// Print the trailing CRLF and also a coma if exporting as an array.
         if(pTable->lRows > 0)
            fprintf(pTable->fd, "%s\n", g_bArrayFile ? "," : "");
         fprintf(pTable->fd, "{");

// Now, print columns.
         for(i = 0; i < pTable->nCols; i++)
            {
// Skip NULL and empty values.
            if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], SKIP)
              || (JSONCOL_FLAG_CHECK(&pTable->pCols[i], MYSQL) 
                && ((JSONCOL_FLAG_CHECK(&pTable->pCols[i], NULL) && g_bSkipNull)
                || (!JSONCOL_FLAG_CHECK(&pTable->pCols[i], NULL) && pTable->pCols[i].pValue[0] == '\0' && g_bSkipEmpty))))
               continue;

// Print column name.
            fprintf(pTable->fd, "%s\"%s\":", bFirstCol ? "" : ",", pTable->pCols[i].pJSONName);

// Print column value.
            if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], NULL))
               fprintf(pTable->fd, "null");
            else if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], FIXEDNUMERIC))
               {
               if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], QUOTED))
                  fprintf(pTable->fd, "\"%ld\"", pTable->pCols[i].lValue);
               else
                  fprintf(pTable->fd, "%ld", pTable->pCols[i].lValue);
               pTable->pCols[i].lValue += pTable->pCols[i].lIncr;
               }
            else if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], BOOL))
               fprintf(pTable->fd, *(pTable->pCols[i].pValue) == '0' ? "false" : "true");
            else if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], QUOTED) || !JSONCOL_FLAG_CHECK(&pTable->pCols[i], NUMERIC))
               {
               if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], UNQUOTED))
                  fprintf(pTable->fd, "%s", pTable->pCols[i].pValue);
               else
                  fprintf(pTable->fd, "\"%s\"", pTable->pCols[i].pValue);
               }
            else if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], NUMERIC))
               fprintf(pTable->fd, "%s", pTable->pCols[i].pValue);

            bFirstCol = FALSE;
            }
         fprintf(pTable->fd, "}");
         pTable->lRows++;
         lBatchRows++;
         }
      mysql_free_result(pRes);

      if(lBatchRows < lBatchLimit || pTable->lBatchSize == 0 || (g_lLimit > 0 && pTable->lRows >= g_lLimit))
         break;

      lBatchLimit = (g_lLimit > 0 && (g_lLimit - pTable->lRows < pTable->lBatchSize || pTable->lBatchSize == 0)) ? g_lLimit - pTable->lRows : pTable->lBatchSize;
      FormatSQL(pTable, lBatchLimit);
      }

   pTable->tStop = g_bTiming ? time(NULL) : 0;

// Write the trailing cr/lf and array indicator now.
   fprintf(pTable->fd, "%s%s", pTable->lRows == 0 ? "" : "\n",
     g_bArrayFile ? "]\n" : "");
   return 0;

ErrExit:
   pTable->tStop = g_bTiming ? time(NULL) : 0;
   g_bStop = TRUE;
   return nRet;
   } // End of ExportTable()


/*
 * Function: PrintMsg()
 * Print a message to the current log.
 * Arguments:
 * unsigned int nLoglevel - Log level of message.
 * char *pFmt - Message format as defined by vprintf().
 * ... Any arguments as per pFmt.
 */
void PrintMsg(unsigned int nLoglevel, char *pFmt, ...)
   {
   struct tm tm;
   time_t now;
   va_list va;

   time(&now);
   localtime_r(&now, &tm);

// Print to logfile, if one is specified, or stderr if not.
   if(LOG_LEVEL(nLoglevel) <= g_nLoglevel)
      {
      va_start(va, pFmt);
      if(LOG_FLAG_CHECK(nLoglevel, STDOUT) && g_fdLog != stdout)
         vprintf(pFmt, va);

// Print date and time, unless requested not to.
      va_start(va, pFmt);
      if(!LOG_FLAG_CHECK(nLoglevel, NODATE))
         fprintf(g_fdLog == NULL ? stderr : g_fdLog,
           "%04d-%02d-%02d %02d:%02d:%02d ", 1900 + tm.tm_year,
           tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

      vfprintf(g_fdLog == NULL ? stderr : g_fdLog, pFmt, va);
      va_end(va);
      if(g_fdLog != NULL && g_fdLog != stderr)
         fflush(g_fdLog);
      }

   return;
   } // End of PrintMsg().


/*
 * Function: PrintStats()
 * Signal handler that shows current status.
 * Arguments:
 * int nData - Signal to process.
 */
void PrintStats(int nData)
   {
// If this is an interupt, stop gracefully.
   if(nData == SIGINT)
      g_bStop = TRUE;

   return;
   } // End of PrintStats()


/*
 * Function: StringIsNumeric()
 * Check if a string is numeric.
 * Arguments:
 * char *pStr - The string to check.
 * BOOL bInt - Check for integer only.
 * Returns:
 * BOOL - TRUE if the string is a valid number, else FALSE.
 */
BOOL StringIsNumeric(char *pStr, BOOL bInt)
   {
   BOOL bComa;

// Skip leading sign.
   if(*pStr != '-' && *pStr != '+' && *pStr < '0' && *pStr > '9')
      return FALSE;

// Skip past leading digits.
   while(*pStr >= '0' && *pStr <= '9')
      pStr++;
   if(*pStr == '\0')
      return TRUE;
   if(bInt)
      return FALSE;

// Check decimals.
   if(*pStr == ',')
      {
      bComa = TRUE;
      pStr++;
      while(*pStr >= '0' && *pStr <= '9')
         pStr++;
      if(*pStr == '\0')
         return TRUE;
      }

// Check exponent.
   if(*pStr != 'e' || *pStr != 'E')
      return FALSE;

   pStr++;
   if(*pStr != '-' && *pStr != '+')
      pStr++;
   while(*pStr >= '0' && *pStr <= '9')
      pStr++;
   if(*pStr == '\0')
      return TRUE;

   return FALSE;
   } // End of StringIsNumeric()


/*
 * Function: json_escape()
 * Escape a JSON string properly.
 * Arguments:
 * char *pStr - The JSON string to quote.
 * char *pRet - A buffer of sufficient size to hold the JSON string.
 * unsigned int *pnLen - Length of the string. Is set to the allocated len, unless if NULL when this is ignored.
 * Returns:
 * char * - A pointer to the converted string, NULL if there is an error.
 */
char *json_escape(char *pStr, char *pRet, unsigned int *pnLen)
   {
   char *pTmp;

   if(pnLen == NULL || json_len_escaped(pStr) + 1 >= *pnLen)
      {
      if((pRet = realloc(pRet, json_len_escaped(pStr) + 1025)) == NULL)
         return NULL;
      if(pnLen != NULL)
         *pnLen = json_len_escaped(pStr) + 1024;
      }

   for(pTmp = pRet; *pStr != '\0'; pStr++)
      {
      if(*pStr == '\\' || *pStr == '"' || *pStr == '/' || *pStr == '\b'
        || *pStr == '\f' || *pStr == '\n' || *pStr == '\r' || *pStr == '\t')
         {
         *pTmp++ = '\\';
         if(*pStr == '\\')
            *pTmp++ = '\\';
         else if(*pStr == '"')
            *pTmp++ = '"';
         else if(*pStr == '/')
            *pTmp++ = '/';
         else if(*pStr == '\b')
            *pTmp++ = 'b';
         else if(*pStr == '\f')
            *pTmp++ = 'f';
         else if(*pStr == '\n')
            *pTmp++ = 'n';
         else if(*pStr == '\r')
            *pTmp++ = 'r';
         else if(*pStr == '\t')
            *pTmp++ = 't';
         }
// Any other control or 8-bit non-ASCII character, use the \uNNNN notation.
      else if(*pStr <= 0x1F || *pStr >= 0x7F)
         {
         *pTmp++ = '\\';
         *pTmp++ = 'u';
         sprintf(pTmp, "%04X", (unsigned char) *pStr);
         pTmp += 4;
         }
      else
         *pTmp++ = *pStr;
      }
   *pTmp = '\0';

   return pRet;
   } // End of json_escape()


/*
 * Function: json_len_escaped()
 * Get the length of a quoted JSON string from an unquoted one. Excluding a terminiting null.
 * Arguments:
 * char *pStr - The string to compute the length of.
 * Returns:
 * unsigned int - The öth of pStr as a quoted JSON string.
 */
unsigned int json_len_escaped(char *pStr)
   {
   unsigned int nLen;

   for(nLen = 0; *pStr != '\0'; pStr++, nLen++)
      {
      if(*pStr == '\\' || *pStr == '"' || *pStr == '/' || *pStr == '\b'
        || *pStr == '\f' || *pStr == '\n' || *pStr == '\r' || *pStr == '\t')
         nLen++;
      else if(*pStr <= 0x1F || *pStr >= 0x7F)
         nLen += 5;
      }

   return nLen;
   } // End of json_len_escaped()


/*
 * Function: FormatSQL()
 * Build a complete SQL from a SQL Format. This is a SQL string
 * with the following included placeholder:
 * %O - Replaced by "ORDER BY <batch col>"
 * %W - Replaced by "WHERE <batch col> > <prev value> AND"
 * %w - Replaced by "WHERE <batch col> > <prev value>"
 * Arguments:
 * PJSONTABLE pTable - The table with the data to be formatted.
 * unsigned long lLimit - LIMIT clause.
 * Returns:
 * char * - The allocated SQL buffer, NULL if there is an error.
 */
char *FormatSQL(PJSONTABLE pTable, unsigned long lLimit)
   {
   BOOL bWhere1 = FALSE;
   BOOL bWhere2 = FALSE;
   BOOL bOrderBy = FALSE;
   char *pTmp1;
   char *pTmp2;
   unsigned int nLen;

// Check which formats we have.
   for(pTmp1 = pTable->pSQLFormat; *pTmp1 != '\0'; pTmp1++)
      {
      if(pTmp1[0] == '%' && pTmp1[1] == 'w')
         bWhere1 = TRUE;
      if(pTmp1[0] == '%' && pTmp1[1] == 'W')
         bWhere2 = TRUE;
      if(pTmp1[0] == '%' && pTmp1[1] == 'O')
         bOrderBy = TRUE;
      }

// Calculate required space.
   nLen = strlen(pTable->pSQLFormat);
   if(bWhere1 || bWhere2)
      {
      if(pTable->pBatchCol == NULL
        || (pTable->pBatchCol != NULL && pTable->pBatchCol->pPrevValue == NULL))
// <space>WHERE<space>
         nLen += 7;
      else if(pTable->pBatchCol != NULL)
// <space>WHERE<space>`<column name>`<space>><space>`<column value>`<space>AND
         nLen += 8 + strlen(pTable->pBatchCol->pJSONName) + 5 + strlen(pTable->pBatchCol->pPrevValue) + 5;

// Make space for suffix.
      if(g_pSQLWhereSuffix != NULL)
// <space><suffix><space>
         nLen += 2 + strlen(g_pSQLWhereSuffix);
      }

   if(bOrderBy && pTable->pBatchCol != NULL)
// <space>ORDER<space>BY<space>`<column name>`
      nLen += 12 + strlen(pTable->pBatchCol->pJSONName);

// Add space for a limit clause.
   if(lLimit > 0)
      nLen += 27;

// Allocate space, if needed.
   if(nLen + 1 > pTable->nSQLBufLen) 
      {
      if((pTable->pSQL = realloc(pTable->pSQL, nLen + 1)) == NULL)
         {
         fprintf(stderr, "Memory allocation error.\n");
         return NULL;
         }
      pTable->nSQLBufLen = nLen + 1;
      }

// Now, do the formatting, char by char.
   for(pTmp1 = pTable->pSQLFormat, pTmp2 = pTable->pSQL; *pTmp1 != '\0'; pTmp1++)
      {
      if(pTmp1[0] == '%' && (pTmp1[1] == 'w' || pTmp1[1] == 'W'))
         {
// If this is the first batch, do this.
         if(pTable->pBatchCol == NULL
           || (pTable->pBatchCol != NULL && pTable->pBatchCol->pPrevValue == NULL))
            {
            if(pTmp1[1] == 'W')
               strcat(pTable->pSQL, " WHERE ");
            }
// Do this for any following rounds.
         else if(pTable->pBatchCol != NULL)
            {
            strcat(pTable->pSQL, " WHERE `");
            strcat(pTable->pSQL, pTable->pBatchCol->pJSONName);
            strcat(pTable->pSQL, "` > ");
            if(JSONCOL_FLAG_CHECK(pTable->pBatchCol, QUOTED) || !JSONCOL_FLAG_CHECK(pTable->pBatchCol, NUMERIC))
               strcat(pTable->pSQL, "'");
            strcat(pTable->pSQL, pTable->pBatchCol->pPrevValue);
            if(JSONCOL_FLAG_CHECK(pTable->pBatchCol, QUOTED) || !JSONCOL_FLAG_CHECK(pTable->pBatchCol, NUMERIC))
               strcat(pTable->pSQL, "'");
            if(pTmp1[1] == 'W')
               strcat(pTable->pSQL, " AND ");
            }

// Add WHERE suffix.
         if(g_pSQLWhereSuffix != NULL)
            {
            strcat(pTable->pSQL, g_pSQLWhereSuffix);
            strcat(pTable->pSQL, " ");
            }
         pTmp1++;
         pTmp2 = &pTable->pSQL[strlen(pTable->pSQL)];
         }
      else if(pTmp1[0] == '%' && (pTmp1[1] == 'o' || pTmp1[1] == 'O'))
         {
         if(pTable->pBatchCol != NULL)
            {
            strcat(pTable->pSQL, pTmp1[1] == 'O' ? " ORDER BY `" : ", `");
            strcat(pTable->pSQL, pTable->pBatchCol->pJSONName);
            strcat(pTable->pSQL, "`");
            }

         pTmp1++;
         pTmp2 = &pTable->pSQL[strlen(pTable->pSQL)];
         }
      else
         {
         *pTmp2++ = *pTmp1;
         *pTmp2 = '\0';
         }
      }

   if(lLimit > 0)
      sprintf(&pTable->pSQL[strlen(pTable->pSQL)], " LIMIT %ld", lLimit);

   return pTable->pSQL;
   } // End of FormatSQL()


/*
 * Function: FindColByName()
 * Find a PJSONCOL based on a name.
 * Arguments:
 * PJSONCOL pCols - Table with columns to find.
 * unsigned int nCols - Number of columns in pCols.
 * char *pName - Column name to find.
 * Returns:
 * PJSONCOL - The found column, NULL if it is not found.
 */
PJSONCOL FindColByName(PJSONCOL pCols, unsigned int nCols, char *pName)
   {
   int i;

   for(i = 0; i < nCols && pCols[i].pName != NULL && strcasecmp(pCols[i].pName, pName) != 0; i++)
      ;

   if(i >= nCols)
      return NULL;

   return &pCols[i];
   } // End of FindColByName()


/*
 * Function: SetBatchingColumn()
 * Set up the batching column in the specified table.
 * Arguments:
 * PJSONTABLE pTable - The table to set batching options for.
 * Returns:
 * BOOL - TRUE if there is an error, else FALSE.
 */
BOOL SetBatchingColumn(PJSONTABLE pTable)
   {
   unsigned int nPk = 0;
   int i;

   for(i = 0; i < pTable->nCols; i++) 
      {
      if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], MYSQL)
        && JSONCOL_FLAG_CHECK(&pTable->pCols[i], BATCH))
         {
         pTable->pBatchCol = &pTable->pCols[i];
         pTable->lBatchSize = g_lBatchSize;
         break;
         }
      }

// If we are not using auto batch col detection and we didn't find the specified one,
// this is an error.
   if(!g_bAutoBatch && i >= pTable->nCols)
      {
      fprintf(stderr, "Batch column %s not found in table %s\n", g_pBatchCol,
        pTable->pName);
      return TRUE;
      }
// Check if we didn't find the batching column, then look for a primary key.
   else if(i >= pTable->nCols)
      {
      for(i = 0; i < pTable->nCols; i++)
         {
         if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], PK))
            {
            nPk++;
            pTable->pBatchCol = &pTable->pCols[i];
            pTable->pCols[i].nFlags |= JSONCOL_FLAG_BATCH;
            }
         }
      if(nPk == 0)
         {
         if(g_bAutoBatch)
            pTable->lBatchSize = 0;
         else
            {
            fprintf(stderr, "No batching column found in table %s\n",  pTable->pName);
            return TRUE;
            }
         }
      else if(nPk > 1)
         {
         if(g_bAutoBatch)
            {
            pTable->pBatchCol = NULL;
            pTable->lBatchSize = 0;
            }
         else
            {
            fprintf(stderr, "More than 1 primary key in table %s. You must specify a batching column.\n",  pTable->pName);
            return TRUE;
            }
         }
      else
         pTable->lBatchSize = g_lBatchSize;
      }

   return FALSE;
   } // End of SetBatchingColumn()


/*
 * Function: SetColsFromResult()
 * Set the column types and definitions based on a result set.
 * Arguments:
 * PJSONCOL pCols - The columns.
 * unsigned int *pnCols - Number of columns in pCols.
 * MYSQL_RES *pRes - The result to use.
 * Returns:
 * PJSONCOL - The set of columns, NULL if there is an error.
 */
PJSONCOL SetColsFromResult(PJSONCOL pCols, unsigned int *pnCols, MYSQL_RES *pRes)
   {
   unsigned int i;
   unsigned int nCols;
   PJSONCOL pRet;
   PJSONCOL pCol;
   MYSQL_FIELD *pFields;
   pFields = mysql_fetch_fields(pRes);
   nCols = *pnCols;

   for(i = 0; i < mysql_num_fields(pRes); i++)
      {
      if((pCol = FindColByName(pCols, *pnCols, pFields[i].name)) == NULL || pCol->pName == NULL)
         nCols++;
      }

// Now we know how many columns to deal with, then allocate space for them and then set them up, if needed.
   if(nCols > *pnCols)
      {
      if((pRet = calloc(nCols, sizeof(JSONCOL))) == NULL)
         {
         fprintf(stderr, "Memory allocation error.\n");
         return NULL;
         }

// Copy existing column definitions.
      for(i = 0; i < *pnCols; i++)
         memcpy(&pRet[i], &pCols[i], sizeof(JSONCOL));

// Clean up new columns.
      for(i = *pnCols; i < nCols; i++)
         {
         pRet[i].pName = NULL;
         pRet[i].pJSONName = NULL;
         pRet[i].pValue = NULL;
         pRet[i].pPrevValue = NULL;
         pRet[i].lValue = 0;
         pRet[i].lIncr = 0;
         pRet[i].nFlags = JSONCOL_FLAG_NONE;
         pRet[i].nMySQLCol = -1;
         }

// Free and alter column definitions.
      if(pCols != NULL)
         free(pCols);

      *pnCols = nCols;
      }
   else
      pRet = pCols;

// Initialize column definitions.
   for(i = 0; i < mysql_num_fields(pRes); i++)
      {
      pCol = FindColByName(pRet, nCols, pFields[i].name);
      if(pCol->pName == NULL)
         {
         pCol->pName = strdup(pFields[i].name);
         pCol->pJSONName = json_escape(pFields[i].name, NULL, NULL);
         }
      pCol->nFlags |= JSONCOL_FLAG_MYSQL;
      pCol->nMySQLCol = i;
      PrintMsg(LOG_DEBUG, "Checking column definition for column: %s (MySQL Flags: 0x%016x)\n", pCol->pName);

// Set datatype flags.
      if(g_bTiny1AsBool && pFields[i].type == MYSQL_TYPE_TINY && pFields[i].max_length == 1)
         pCol->nFlags |= JSONCOL_FLAG_BOOL;
      else if(pFields[i].type == MYSQL_TYPE_TINY || pFields[i].type == MYSQL_TYPE_SHORT
        || pFields[i].type == MYSQL_TYPE_LONG || pFields[i].type == MYSQL_TYPE_INT24
        || pFields[i].type == MYSQL_TYPE_LONGLONG)
         pCol->nFlags |= JSONCOL_FLAG_INTEGER;
      else if(pFields[i].type == MYSQL_TYPE_DECIMAL || pFields[i].type == MYSQL_TYPE_NEWDECIMAL
        || pFields[i].type == MYSQL_TYPE_FLOAT || pFields[i].type == MYSQL_TYPE_DOUBLE)
         pCol->nFlags |= JSONCOL_FLAG_NUMERIC;

// Check if this is a primary key column.
      if((pFields[i].flags & PRI_KEY_FLAG) == PRI_KEY_FLAG)
         pCol->nFlags |= JSONCOL_FLAG_PK;
      }

   return pRet;
   } // End of SetColsFromResult()


/*
 * Function: PrintTableCols()
 * Print out the column definition for a SQL statement or table.
 * Arguments:
 * FILE *fd - File to print to.
 * PJSONTABLE pTable - Tabke to print columns for.
 */
void PrintTableCols(FILE *fd, PJSONTABLE pTable)
   {
   int i;


// Print table info.
   if(pTable->pName == NULL)
      fprintf(fd, "SQL: %s\n", pTable->pSQL);
   else
      fprintf(fd, "Table: %s\n", pTable->pName);
   fprintf(fd, "Batch size: %ld\n", pTable->lBatchSize);
   if(pTable->pBatchCol != NULL)
      fprintf(fd, "Batch col: %s\n", pTable->pBatchCol->pName);
   fprintf(fd, "Columns:\n");

// Print columns.
   for(i = 0; i < pTable->nCols; i++)
      {
      if(!JSONCOL_FLAG_CHECK(&pTable->pCols[i], MYSQL)
        && !JSONCOL_FLAG_CHECK(&pTable->pCols[i], FIXED))
         continue;

      fprintf(fd, "  Name: %s\n", pTable->pCols[i].pName);
      if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], MYSQL))
         fprintf(fd, "    MySQL column\n");
      fprintf(fd, "    Flags: 0x%08x\n", pTable->pCols[i].nFlags);
      fprintf(fd, "    Type: ");
      if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], BOOL))
         fprintf(fd, "Boolean\n");
      else if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], NUMERIC))
         {
         if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], QUOTED))
            fprintf(fd, "Numeric Quoted\n");
         else
            fprintf(fd, "Numeric Unquoted\n");
         }
      else
         {
         if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], QUOTED)
           || !JSONCOL_FLAG_CHECK(&pTable->pCols[i], UNQUOTED))
            fprintf(fd, "String Quoted\n");
         else
            fprintf(fd, "String Unquoted\n");
         }

      if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], FIXEDNUMERIC))
         {
         fprintf(fd, "    Value: %ld\n", pTable->pCols[i].lValue);
         if(pTable->pCols[i].lIncr > 0)
            fprintf(fd, "      Increment: %ld\n", pTable->pCols[i].lIncr);
         }
      else if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], FIXED))
         fprintf(fd, "    Value: %s\n", pTable->pCols[i].pValue);

      if(JSONCOL_FLAG_CHECK(&pTable->pCols[i], PK))
         fprintf(fd, "    Primary key\n");
      }

   return;
   } // End of PrintTableCols()

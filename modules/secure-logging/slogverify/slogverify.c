/*
 * Copyright (c) 2019-2026 Airbus Commercial Aircraft
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <locale.h>
#include "messages.h"
#include "slog.h"

// Arguments and options
static gboolean iterative = FALSE;
static char *hostKeyPath = NULL;
static char *prevHostKeyPath = NULL;
static char *curMacFilePath = NULL;
static char *prevMacFilePath = NULL;
static char *inputLogPath = NULL;
static char *outputLogPath = NULL;
static int bufSize = DEF_BUF_SIZE;

// Return TRUE on success, FALSE on error
gboolean normalMode(char *path_hostkey, char *path_MACfile, char *path_inputlog, char *path_outputlog, int bufsize)
{
  guchar key[KEY_LENGTH];
  guint64 counter;

  msg_info(SLOG_INFO_PREFIX, evt_tag_str("Reason", "Reading key file"), evt_tag_str("name", path_hostkey));
  gboolean success = readKey(key, &counter, path_hostkey);
  if (!success)
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Unable to read hostkey"), evt_tag_str("file", path_hostkey));
      return FALSE; //-- ERROR
    }

  if (counter!=0UL)
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason",
                                               "Initial key k0 is required for verification and decryption but the supplied key read has a counter > 0."),
                evt_tag_long("Counter", counter));
      return FALSE; //-- ERROR
    }

  msg_info(SLOG_INFO_PREFIX, evt_tag_str("Reason", "Reading MAC file"), evt_tag_str("name", path_MACfile));
  FILE *fp_bigMAC = fopen(path_MACfile, "r");
  if (fp_bigMAC == NULL)
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Unable to read MAC"), evt_tag_str("file", path_MACfile));
      return FALSE; //-- ERROR
    }
  else
    {
      fclose(fp_bigMAC);
      fp_bigMAC = NULL;
    }

  guchar MAC[CMAC_LENGTH]; //-- aggregated MAC
  if (!readAggregatedMAC(path_MACfile, MAC))
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Unable to read MAC"), evt_tag_str("file", path_MACfile));
      return FALSE; //-- ERROR
    }

  //-- initial MAC0 ---
  char pathMac0[PATH_MAX]; //-- full path of MAC0 file mac0.dat
  guchar MAC0[CMAC_LENGTH]; //-- initial MAC
  memset(MAC0, 0, CMAC_LENGTH);
  if (TRUE == get_path_mac0(path_MACfile, pathMac0, PATH_MAX))
    {
      if (!readAggregatedMAC(pathMac0, MAC0))
        {
          msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Unable to read MAC0"), evt_tag_str("file", pathMac0));
          return FALSE; //-- ERROR
        }
      else
        {
          msg_info(SLOG_INFO_PREFIX, evt_tag_str("Reason", "Read MAC0"), evt_tag_str("file", pathMac0));
        }
    }
  else
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Invalid pathMac0")); //-- fileVerify will fail later
      return FALSE; //-- ERROR
    }


  FILE *fp_input = fopen(path_inputlog, "r");
  if (fp_input == NULL)
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Unable to open inputlog"), evt_tag_str("file", path_inputlog));
      return FALSE; //-- ERROR
    }

  // Scan through file ones
  guint64 entries = 0;
  while (!feof(fp_input))
    {
      char c = fgetc(fp_input);
      if(c == '\n')
        {
          entries++;
        }
    }
  fclose(fp_input);
  fp_input = NULL;

  msg_info(SLOG_INFO_PREFIX, evt_tag_str("Reason", "Number of lines in file"), evt_tag_long("number", entries));
  msg_info(SLOG_INFO_PREFIX, evt_tag_str("Reason", "Restoring and verifying log entries"), evt_tag_int("buffer size",
           bufsize));
  gboolean result = fileVerify(key, path_inputlog, path_outputlog, MAC, entries, bufsize, MAC0);
  if (!result)
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason",
                                               "There is a problem with log verification. Please check log manually"));
      return FALSE; //-- ERROR

    }
  return TRUE; //-- SUCCESS
}


// Return TRUE on success, FALSE on error
gboolean iterativeMode(char *path_prevKey, char *path_prevMAC, char *path_curMAC, char *path_inputlog,
                       char *path_outputlog, int bufsize)
{
  guchar previousKey[KEY_LENGTH];
  guint64 previousKeyCounter = 0;

  msg_info(SLOG_INFO_PREFIX, evt_tag_str("Reason", "Reading previous keyfile"), evt_tag_str("name", path_prevKey));
  gboolean success = readKey(previousKey, &previousKeyCounter, path_prevKey);
  if (!success)
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Previous key could not be loaded."), evt_tag_str("file",
                path_prevKey));
      return FALSE; //-- ERROR
    }

  msg_info(SLOG_INFO_PREFIX, evt_tag_str("Reason", "Reading previous MACfile"), evt_tag_str("name", path_prevMAC));
  FILE *fp_previousBigMAC = fopen(path_prevMAC, "r");
  if (fp_previousBigMAC == NULL)
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Unable to readprevious MAC"), evt_tag_str("file", path_prevMAC));
      return FALSE; //-- ERROR
    }
  else
    {
      fclose(fp_previousBigMAC);
      fp_previousBigMAC = NULL;
    }

  guchar previousMAC[CMAC_LENGTH];
  if (!readAggregatedMAC(path_prevMAC, previousMAC))
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Unable to read previous MAC"), evt_tag_str("file", path_prevMAC));
      return FALSE; //-- ERROR
    }

  msg_info(SLOG_INFO_PREFIX, evt_tag_str("Reason", "Reading current MAC file"), evt_tag_str("name", path_curMAC));
  FILE *fp_currentBigMAC = fopen(path_curMAC, "r");
  if (fp_currentBigMAC == NULL)
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Unable to read current MAC"), evt_tag_str("file", path_curMAC));
      return FALSE; //-- ERROR
    }
  else
    {
      fclose(fp_currentBigMAC);
      fp_currentBigMAC = NULL;
    }

  guchar currentMAC[CMAC_LENGTH];
  if (!readAggregatedMAC(path_curMAC, currentMAC))
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Unable to read current MAC"), evt_tag_str("file", path_curMAC));
      return FALSE; //-- ERROR
    }

  FILE *fp_input = fopen(path_inputlog, "r");
  if (fp_input == NULL)
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Unable to open inputlog"), evt_tag_str("file", path_inputlog));
      return FALSE; //-- ERROR
    }

  // Scan through file ones
  guint64 entries = 0;
  while (!feof(fp_input))
    {
      char c = fgetc(fp_input);
      if(c == '\n')
        {
          entries++;
        }
    }
  fclose(fp_input);

  msg_info(SLOG_INFO_PREFIX, evt_tag_str("Reason", "Number of lines in file"), evt_tag_long("number", entries));
  msg_info(SLOG_INFO_PREFIX, evt_tag_str("Reason", "Restoring and verifying log entries"), evt_tag_int("buffer size",
           bufsize));
  gboolean result = iterativeFileVerify(previousMAC, previousKey, path_inputlog, currentMAC, path_outputlog,
                                        entries,
                            bufsize, previousKeyCounter);

  if (!result)
    {
      msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason",
                                               "There is a problem with log verification. Please check log manually"));
      return FALSE; //-- ERROR
    }

  return TRUE; //-- SUCCESS
}


//
// main logic: 0 usually indicates success, and non-zero values indicate an error
//
int main(int argc, char *argv[])
{
  setlocale(LC_ALL, "");

  SLogOptions options[] =
  {
    { "iterative", 'i', "Iterative verification", NULL, NULL },
    { "key-file", 'k', "Initial host key file", "FILE", NULL },
    { "mac-file", 'm', "Current MAC file", "FILE", NULL },
    { "prev-key-file", 'p', "Previous host key file in iterative mode", "FILE", NULL },
    { "prev-mac-file", 'r', "Previous MAC file in iterative mode", "FILE", NULL },
    { NULL }
  };

  GOptionEntry entries[] =
  {
    { options[0].longname, options[0].shortname, 0, G_OPTION_ARG_NONE, &iterative, options[0].description, NULL },
    { options[1].longname, options[1].shortname, 0, G_OPTION_ARG_CALLBACK, &validFileNameArg, options[1].description, options[1].type },
    { options[2].longname, options[2].shortname, 0, G_OPTION_ARG_CALLBACK, &validFileNameArg, options[2].description, options[2].type },
    { options[3].longname, options[3].shortname, 0, G_OPTION_ARG_CALLBACK, &validFileNameArg, options[3].description, options[3].type },
    { options[4].longname, options[4].shortname, 0, G_OPTION_ARG_CALLBACK, &validFileNameArg, options[4].description, options[4].type },
    { NULL }
  };

  GError *error = NULL;
  GOptionContext *context = g_option_context_new("INPUTLOG OUTPUTLOG [COUNTER] - Log archive verification\n\n" \
                                                 "Examples:\n" \
                                                 "  normal mode:\n" \
                                                 "    ./slogverify\n" \
                                                 "    --key-file ./host.key\n" \
                                                 "    --mac-file ./mac.dat\n" \
                                                 "    ./messages.slog\n" \
                                                 "    ./messages_verified.txt\n\n"
                                                 "  iterative mode:\n" \
                                                 "    ./slogverify -i\n" \
                                                 "    --prev-key-file ./host0.key\n" \
                                                 "    --prev-mac-file ./mac0.dat\n" \
                                                 "    --mac-file ./mac1.dat\n" \
                                                 "    ./plainlog_1.out\n" \
                                                 "    ./plainlog_1.chk\n"
                                                );

  GOptionGroup *group = g_option_group_new("Basic options", "Basic log archive verification options", "basic", &options,
                                           NULL);

  g_option_group_add_entries(group, entries);
  g_option_context_set_main_group(context, group);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      GString *errorMsg = g_string_new(error->message);
      (void) slog_usage(context, group, errorMsg);
      return 1; //-- ERROR
    }

  if(argc < 2 || argc > 4)
    {
      (void) slog_usage(context, group, NULL);
      return 1; //-- ERROR
    }

  // Initialize internal messaging
  msg_init(TRUE);

  // Assign option arguments
  int index = 1;
  hostKeyPath = options[index++].arg;
  if (!iterative)
    {
      char *p_path_check = realpath(hostKeyPath, NULL);
      if (NULL == p_path_check)
        {
          msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Failed to check key-file!"));
          (void) slog_usage(context, group, NULL);
          return 1; //-- ERROR
        }
      free(p_path_check);
      p_path_check = NULL;
    } //-- not iterative

  curMacFilePath = options[index++].arg;
  if (!iterative)
    {
      char *p_path_check = realpath(curMacFilePath, NULL);
      if (NULL == p_path_check)
        {
          msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Failed to check mac-file!"));
          (void) slog_usage(context, group, NULL);
          return 1; //-- ERROR
        }
      free(p_path_check);
      p_path_check = NULL;
    } //-- not iterative

  prevHostKeyPath = options[index++].arg;
  if (iterative)
    {
      char *p_path_check = realpath(prevHostKeyPath, NULL);
      if (NULL == p_path_check)
        {
          msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Failed to check prev-key-file!"));
          (void) slog_usage(context, group, NULL);
          return 1; //-- ERROR
        }
      free(p_path_check);
      p_path_check = NULL;
    } //-- iterative

  prevMacFilePath = options[index++].arg;
  if (iterative )
    {
      char *p_path_check = realpath(prevMacFilePath, NULL);
      if (NULL == p_path_check)
        {
          msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Failed to check prev-mac-file!"));
          (void) slog_usage(context, group, NULL);
          return 1; //-- ERROR
        }
      free(p_path_check);
      p_path_check = NULL;
    } //-- iterative

  // Input and output file arguments
  index = 1;
  inputLogPath = argv[index++];
  if (!g_file_test(inputLogPath, G_FILE_TEST_IS_REGULAR))
    {
      GString *errorMsg = g_string_new(FILE_ERROR);
      g_string_append(errorMsg, inputLogPath);
      (void) slog_usage(context, group, errorMsg);
      return 1; //-- ERROR
    }

  outputLogPath = argv[index++];
  gchar *dir_part = g_path_get_dirname(outputLogPath);
  if (NULL != dir_part)
    {
      char *dir_check = realpath(dir_part, NULL);
      if (NULL == dir_check)
        {
          msg_error(SLOG_ERROR_PREFIX, evt_tag_str("Reason", "Failed to check OUTPUTLOG file!"));
          g_free(dir_part);
          dir_part = NULL;
          (void) slog_usage(context, group, NULL);
          return 1; //-- ERROR
        }
      free(dir_check);
      g_free(dir_part);
      dir_part = NULL;
    }
  if (outputLogPath == NULL)
    {
      (void) slog_usage(context, group, NULL);
      return 1; //-- ERROR
    }

  // Buffer size arguments if applicable
  if (argc == 4)
    {
      bufSize = atoi(argv[index]);
      if(bufSize <= MIN_BUF_SIZE || bufSize > MAX_BUF_SIZE)
        {
          msg_error(SLOG_ERROR_PREFIX,
                    evt_tag_str("Reason", "Invalid buffer size."),
                    evt_tag_int("Size", bufSize),
                    evt_tag_int("Minimum buffer size", MIN_BUF_SIZE),
                    evt_tag_int("Maximum buffer size", MAX_BUF_SIZE));
          g_option_context_free(context);
          return 1; //-- ERROR
        }
    }

  int ret = 0; //-- main logic, 0 errors
  if (iterative)
    {
      if (prevHostKeyPath == NULL || prevMacFilePath == NULL || curMacFilePath == NULL)
        {
          g_print("%s", g_option_context_get_help(context, TRUE, NULL));
          g_option_context_free(context);
          return 1; //-- ERROR
        }

      gboolean result = iterativeMode(prevHostKeyPath, prevMacFilePath, curMacFilePath, inputLogPath, outputLogPath, bufSize);
      if (!result)
        {
          ret = 1; //-- ERROR
        }
    }
  else
    {
      if (hostKeyPath == NULL || curMacFilePath  == NULL)
        {
          g_print("%s", g_option_context_get_help(context, TRUE, NULL));
          g_option_context_free(context);
          return 1; //-- ERROR
        }
      gboolean result = normalMode(hostKeyPath, curMacFilePath, inputLogPath, outputLogPath, bufSize);
      if (!result)
        {
          ret = 1; //-- ERROR
        }
    }

  // Release messaging resources
  msg_deinit();
  g_option_context_free(context);
  return ret;
}

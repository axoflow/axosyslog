/*
 * Copyright (c) 2024 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "cfg-source.h"

/* display CONTEXT lines before and after the offending line */
#define CONTEXT 5

static void
_format_source_prefix(gchar *line_prefix, gsize line_prefix_len, gint lineno, gboolean error_location)
{
  g_snprintf(line_prefix, line_prefix_len, "%d", lineno);
  if (error_location)
    {
      for (gint i = strlen(line_prefix); i < 6; i++)
        g_strlcat(line_prefix, "-", line_prefix_len);

      g_strlcat(line_prefix, ">", line_prefix_len);
    }
}

static void
_print_underline(const gchar *line, gint whitespace_before, gint number_of_carets)
{
  for (gint i = 0; line[i] && i < whitespace_before; i++)
    {
      fprintf(stderr, "%c", line[i] == '\t' ? '\t' : ' ');
    }

  /* NOTE: sometimes the yylloc has zero characters, print a caret even in
   * this case, that's why i == 0 is there */

  for (gint i = 0; i == 0 || i < number_of_carets; i++)
    fprintf(stderr, "^");
  fprintf(stderr, "\n");
}

static void
_print_underlined_source_block(const CFG_LTYPE *yylloc, gchar **lines, gsize num_lines, gint start_line)
{
  gint line_ndx;
  gchar line_prefix[12];

  for (line_ndx = 0; line_ndx < num_lines; line_ndx++)
    {
      gint lineno = start_line + line_ndx;
      const gchar *line = lines[line_ndx];
      gint line_len = strlen(line);
      gboolean line_ends_with_newline = line_len > 0 && line[line_len - 1] == '\n';

      _format_source_prefix(line_prefix, sizeof(line_prefix), lineno,
                            lineno >= yylloc->first_line && lineno <= yylloc->last_line);

      fprintf(stderr, "%-8s%s%s", line_prefix, line, line_ends_with_newline ? "" : "\n");

      if (lineno == yylloc->first_line)
        {
          /* print the underline right below the source line we just printed */
          fprintf(stderr, "%-8s", line_prefix);

          gboolean multi_line = yylloc->first_line != yylloc->last_line;

          _print_underline(line, yylloc->first_column - 1,
                           multi_line ? strlen(&line[yylloc->first_column]) + 1
                           : yylloc->last_column - yylloc->first_column);
        }
    }
}

static void
_report_file_location(const gchar *filename, const CFG_LTYPE *yylloc, gint start_line)
{
  FILE *f;
  gint lineno = 0;
  gsize buflen = 65520;
  gchar *buf = g_malloc(buflen);
  GPtrArray *context = g_ptr_array_new();
  gint end_line = start_line + 2*CONTEXT;

  if (start_line <= 0)
    {
      start_line = yylloc->first_line > CONTEXT ? yylloc->first_line - CONTEXT : 1;
      end_line = yylloc->first_line + CONTEXT;
    }

  f = fopen(filename, "r");
  if (f)
    {
      while (fgets(buf, buflen, f))
        {
          lineno++;
          if (lineno > end_line)
            break;
          else if (lineno < start_line)
            continue;
          g_ptr_array_add(context, g_strdup(buf));
        }
      fclose(f);
    }
  if (context->len > 0)
    _print_underlined_source_block(yylloc, (gchar **) context->pdata, context->len, start_line);

  g_free(buf);
  g_ptr_array_foreach(context, (GFunc) g_free, NULL);
  g_ptr_array_free(context, TRUE);
}

/* this will report source content from the buffer, but use the line numbers
 * of the file where the block was defined.
 *
 *    buffer_*  => tracks buffer related information
 *    file_*    => tracks file related information
 */
static void
_report_buffer_location(CfgIncludeLevel *level, const CFG_LTYPE *file_lloc, const CFG_LTYPE *buf_lloc)
{
  gchar **buffer_lines = level->buffer.original_lines;
  gint buffer_num_lines = level->buffer.num_original_lines;

  if (buffer_num_lines < buf_lloc->first_line)
    return;

  /* the line number in the file, which we report in the source dump, 1 based */
  gint range_backwards = CONTEXT;
  if (file_lloc->first_line <= range_backwards)
    range_backwards = file_lloc->first_line - 1;

  /* the index of the line in the buffer where we start printing 0-based */
  gint buffer_start_index = buf_lloc->first_line - 1 - range_backwards;
  if (buffer_start_index < 0)
    buffer_start_index = 0;

  _print_underlined_source_block(file_lloc, &buffer_lines[buffer_start_index], buffer_num_lines - buffer_start_index,
                                 file_lloc->first_line - range_backwards);
}

gboolean
cfg_source_print_source_text(const gchar *filename, gint line, gint column, gint start_line)
{
  CFG_LTYPE yylloc = {0};

  yylloc.name = filename;
  yylloc.first_line = yylloc.last_line = line;
  yylloc.first_column = yylloc.last_column = column;
  _report_file_location(yylloc.name, &yylloc, start_line);
  return TRUE;
}

gboolean
cfg_source_print_source_context(CfgLexer *lexer, CfgIncludeLevel *level, const CFG_LTYPE *yylloc)
{
  if (level->include_type == CFGI_FILE)
    {
      _report_file_location(yylloc->name, yylloc, -1);
    }
  else if (level->include_type == CFGI_BUFFER)
    {
      CFG_LTYPE buf_lloc = *yylloc;
      cfg_lexer_undo_set_file_location(lexer, &buf_lloc);

      _report_buffer_location(level, yylloc, &buf_lloc);
    }
  return TRUE;
}

static gboolean
_extract_source_from_file_location(GString *result, const gchar *filename, const CFG_LTYPE *yylloc)
{
  gint buflen = 65520;
  if (yylloc->first_column < 1 || yylloc->last_column < 1 ||
      yylloc->first_column > buflen-1 || yylloc->last_column > buflen-1)
    return FALSE;

  FILE *f = fopen(filename, "r");
  if (!f)
    return FALSE;

  gchar *line = g_malloc(buflen);
  gint lineno = 0;

  while (fgets(line, buflen, f))
    {
      lineno++;
      gint linelen = strlen(line);
      if (line[linelen-1] == '\n')
        {
          line[linelen-1] = 0;
          linelen--;
        }

      if (lineno > (gint) yylloc->last_line)
        break;
      else if (lineno < (gint) yylloc->first_line)
        continue;
      else if (lineno == yylloc->first_line)
        {
          if (yylloc->first_line == yylloc->last_line)
            g_string_append_len(result, &line[MIN(linelen, yylloc->first_column-1)], yylloc->last_column - yylloc->first_column);
          else
            g_string_append(result, &line[MIN(linelen, yylloc->first_column-1)]);
        }
      else if (lineno < yylloc->last_line)
        {
          g_string_append_c(result, ' ');
          g_string_append(result, line);
        }
      else if (lineno == yylloc->last_line)
        {
          g_string_append_c(result, ' ');
          g_string_append_len(result, line, yylloc->last_column);
        }
    }
  fclose(f);

  g_free(line);
  /* NOTE: do we have the appropriate number of lines? */
  return lineno > yylloc->first_line;
}

static gboolean
_extract_source_from_buffer_location(GString *result, CfgIncludeLevel *level, const CFG_LTYPE *yylloc)
{
  gchar **lines = level->buffer.original_lines;
  gint num_lines = level->buffer.num_original_lines;

  if (num_lines <= yylloc->first_line)
    goto exit;

  if (yylloc->first_column < 1)
    goto exit;

  for (gint lineno = yylloc->first_line; lineno < num_lines && lineno <= yylloc->last_line; lineno++)
    {
      gchar *line = lines[lineno - 1];
      gint linelen = strlen(line);

      if (lineno == yylloc->first_line)
        {
          gint token_start = MIN(linelen, yylloc->first_column - 1);

          if (yylloc->first_line == yylloc->last_line)
            {
              /* both last_column & first_column are 1 based, they cancel that out */
              gint token_len = yylloc->last_column - yylloc->first_column;
              if (token_start + token_len > linelen)
                token_len = linelen - token_start;
              g_string_append_len(result, &line[token_start], token_len);
            }
          else
            g_string_append(result, &line[token_start]);
        }
      else if (lineno < yylloc->last_line)
        {
          g_string_append_c(result, ' ');
          g_string_append(result, line);
        }
      else if (lineno == yylloc->last_line)
        {
          /* last_column is 1 based */
          gint token_len = yylloc->last_column - 1;
          if (token_len > linelen)
            token_len = linelen;
          g_string_append_c(result, ' ');
          g_string_append_len(result, line, token_len);
        }
    }

exit:
  return TRUE;
}

gboolean
cfg_source_extract_source_text(CfgLexer *lexer, const CFG_LTYPE *yylloc, GString *result)
{
  CfgIncludeLevel *level = &lexer->include_stack[lexer->include_depth];

  g_string_truncate(result, 0);
  if (level->include_type == CFGI_FILE)
    return _extract_source_from_file_location(result, yylloc->name, yylloc);
  else if (level->include_type == CFGI_BUFFER)
    {
      CFG_LTYPE buf_lloc = *yylloc;
      cfg_lexer_undo_set_file_location(lexer, &buf_lloc);

      return _extract_source_from_buffer_location(result, level, &buf_lloc);
    }
  else
    g_assert_not_reached();
}

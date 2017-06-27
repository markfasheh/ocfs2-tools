/*
 * jconfig.c
 * 
 * Routines for handling the config file format of
 * Helpers::JConfig.
 *
 * Copyright (C) 2002 Joel Becker <jlbec@evilplan.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License version 2 as published by the Free Software Foundation.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* Check G_LOG_DOMAIN before including glib.h */
#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN "JConfig"
#endif /* G_LOG_DOMAIN */

#include <glib.h>

#include "jiterator.h"
#include "jconfig.h"



/*
 * Defines
 */
#define CF_SCAN_WHITESPACE " \t"



/*
 * Types
 */
typedef struct _JOutputCtxt JOutputCtxt;


/*
 * Structures
 */
struct _JConfigCtxt
{
    JConfig *cf;
    JConfigStanza *cfs;
    gboolean verbose;
    gboolean error;
};

struct _JConfigStanza
{
    gchar *stanza_name;
    GHashTable *attrs;
};

struct _JConfig
{
    gchar *filename;
    GList *stanza_names;
    GHashTable *stanzas;
};

struct _JOutputCtxt
{
    FILE *output;
    gboolean success;
};



/*
 * File globals
 */
static const GScannerConfig test_config =
{
    ""                  /* cset_skip_characters */,
    (
     G_CSET_a_2_z
     "_0123456789"
     G_CSET_A_2_Z
    )                   /* cset_identifier_first */,
    (
     G_CSET_a_2_z
     "_0123456789"
     G_CSET_A_2_Z
    )                   /* cset_identifier_nth */,
    ""                  /* cpair_comment_single */,
    TRUE                /* case_sensitive */,
    FALSE               /* skip_comment_multi */,
    FALSE               /* skip_comment_single */,
    FALSE               /* scan_comment_multi */,
    TRUE                /* scan_identifier */,
    TRUE                /* scan_identifier_1char */,
    FALSE               /* scan_identifier_NULL */,
    TRUE                /* scan_symbols */,
    FALSE               /* scan_binary */,
    FALSE               /* scan_octal */,
    FALSE               /* scan_float */,
    FALSE               /* scan_hex */,
    FALSE               /* scan_hex_dollar */,
    FALSE               /* scan_string_sq */,
    FALSE               /* scan_string_dq */,
    FALSE               /* numbers_2_int */,
    FALSE               /* int_2_float */,
    FALSE               /* identifier_2_string */,
    FALSE               /* char_2_token */,
    TRUE                /* symbol_2_token */,
    FALSE               /* scope_0_fallback */
};




/*
 * Forward declarations
 */
static JConfig *j_config_parse_any(JConfigCtxt *cfc,
                                   const gchar *input_name,
                                   gint input_fd,
                                   const gchar *input_string,
                                   gint input_len);
static void j_config_parse_base(GScanner *scanner, JConfigCtxt *cfc);
static void j_config_parse_to_eol(GScanner *scanner);
static void j_config_parse_comment(GScanner *scanner);
static void j_config_parse_stanza_name(GScanner *scanner,
                                       JConfigCtxt *cfc);
static void j_config_parse_white_start(GScanner *scanner,
                                       JConfigCtxt *cfc);
static void j_config_parse_stanza_attr(GScanner *scanner,
                                       JConfigCtxt *cfc);
static void j_config_foreach_attr_print(gpointer key,
                                        gpointer value,
                                        gpointer o_ctxt);
static void j_config_foreach_stanza_print(gpointer key,
                                          gpointer value,
                                          gpointer o_ctxt);
static void j_config_attr_names_foreach(gpointer key,
                                        gpointer value,
                                        gpointer user_data);
static void j_config_free_stanza(JConfigStanza *cfs);
static JConfig *j_config_config_new(void);
static JConfigStanza *j_config_stanza_new(void);
static void j_config_free_stanza_node(gpointer key,
                                      gpointer value,
                                      gpointer thrway);
static void j_config_free_config_node(gpointer key,
                                      gpointer value,
                                      gpointer thrway);
static void j_config_free_stanza_proxy(gpointer data, gpointer thrway);
static void j_config_foreach_attr_append(gpointer key,
                                         gpointer value,
                                         gpointer fbuffer);
static void j_config_foreach_stanza_append(gpointer key,
                                           gpointer value,
                                           gpointer fbuffer);



/*
 * Functions
 */


/*
 * JConfigCtxt *j_config_new_context()
 *
 * Returns a new JConfigCtxt.
 */
JConfigCtxt *j_config_new_context(void)
{
    JConfigCtxt *cfc;

    cfc = g_new(JConfigCtxt, 1);
    if (cfc == NULL)
    {
        g_warning("Unable to create a JConfigCtxt structure: %s\n",
                  g_strerror(errno));
        return(NULL);
    }
    
    cfc->cfs = NULL;
    cfc->cf = NULL;
    cfc->verbose = TRUE;
    cfc->error = FALSE;

    return cfc;
}  /* j_config_context_new() */


/*
 * void j_config_context_free(JConfigCtxt *cfc)
 * 
 * Frees a JConfigCtxt.  Does *not* free the associated JConfig.
 */
void j_config_context_free(JConfigCtxt *cfc)
{
    g_return_if_fail(cfc != NULL);
    g_free(cfc);
}  /* j_config_context_free() */


/*
 * gboolean j_config_context_get_error(JConfigCtxt *cfc)
 *
 * Set zero for continuing past errors, nonzero to fail.
 */
gboolean j_config_context_get_error(JConfigCtxt *cfc)
{
    g_return_val_if_fail(cfc != NULL, TRUE);

    return cfc->error;
}  /* j_config_context_get_error() */


/*
 * void j_config_context_set_verbose(JConfigCtxt *cfc, gboolean verbose)
 *
 * Set zero for quiet, nonzero for verbose.
 */
void j_config_context_set_verbose(JConfigCtxt *cfc, gboolean verbose)
{
    g_return_if_fail(cfc != NULL);

    cfc->verbose = verbose;
}  /* j_config_context_set_verbose() */


/*
 * static JConfig *j_config_parse_any(JConfigCtxt *cfc,
 *                                    const gchar *input_name,
 *                                    gint input_fd,
 *                                    const gchar *input_string,
 *                                    gint input_len)
 *                                 
 * The real initial parsing routine.
 * Creates the JConfig and structures and then
 * calls j_config_parse_base();
 *                                                                 
 * some of this is ripped from gtkrc.c
 */
static JConfig *j_config_parse_any(JConfigCtxt *cfc,
                                   const gchar *input_name,
                                   gint input_fd,
                                   const gchar *input_string,
                                   gint input_len)
{
    GScanner *scanner;
    
    scanner = g_scanner_new((GScannerConfig *)&test_config);
    
    if (input_fd >= 0)
    {
        g_assert(input_string == NULL);
        g_scanner_input_file(scanner, input_fd);
    }
    else
    {
        g_assert(input_string != NULL);
        g_assert(input_len >= 0);
        g_scanner_input_text(scanner, input_string, input_len);
    }
    scanner->input_name = input_name;
    
    cfc->cf = j_config_config_new();
    if (cfc->cf == NULL)
    {
        if (cfc->verbose)
        {
            g_warning("Unable to create a JConfig structure: %s\n",
                      g_strerror(errno));
        }
        cfc->error = TRUE;
        g_scanner_destroy(scanner);
        return(NULL);
    }

    cfc->cf->filename = g_strdup(input_name);
    j_config_parse_base(scanner, cfc);
    
    g_scanner_destroy(scanner);
    
    return(cfc->cf);
}  /* j_config_parse_any() */


/*
 * static void j_config_parse_base(GScanner *scanner, JConfigCtxt *cfc)
 * 
 * The config file is line oriented.  In this scope, it is assumed
 * that the scanner is situated at the begining of a line _always_.
 * As such, every sub-function called must return with the scanner
 * at the beginning of a line (or EOF);
 * 
 * This function runs through line by line, deciding what
 * sub-function should handle each line.
 */
static void j_config_parse_base(GScanner *scanner, JConfigCtxt *cfc)
{
    gboolean done;
    GTokenValue *value;
    
    value = &scanner->next_value;

    done = FALSE;
    while (done == FALSE)
    {
        GTokenType token;

        token = g_scanner_peek_next_token(scanner);
        
        switch (token)
        {
            case G_TOKEN_EOF:
                done = TRUE;
                break;
            case G_TOKEN_NONE:
                /* will this ever happen? */
                break;
            case G_TOKEN_ERROR:
                /* should do something here */
                break;
            case G_TOKEN_CHAR:
                if (strchr(CF_SCAN_WHITESPACE, value->v_char) != NULL)
                    j_config_parse_white_start(scanner, cfc);
                else if (value->v_char == '#')
                    j_config_parse_comment(scanner);
                else if (value->v_char == '\n')
                {
#ifdef DEBUG
                    g_print("Newline\n");
#endif /* DEBUG */
                    g_scanner_get_next_token(scanner);
                    if (cfc->cfs != NULL)
                        cfc->cfs = NULL;
                }
                else
                {
                    if (cfc->verbose)
                    {
                        g_warning("Invalid character in stanza name: %c\n",
                                  value->v_char);
                    }
                    cfc->error = TRUE;
                    j_config_parse_to_eol(scanner);
                    if (cfc->cfs != NULL)
                        cfc->cfs = NULL;
                }
                break;
            case G_TOKEN_SYMBOL:
                /* Another one I don't think I should get */
                break;
            case G_TOKEN_IDENTIFIER:
                j_config_parse_stanza_name(scanner, cfc);
                break;
            default:
                if (cfc->verbose)
                    g_warning("Unknown token\n");
                cfc->error = TRUE;
                j_config_parse_to_eol(scanner);
                if (cfc->cfs != NULL)
                    cfc->cfs = NULL;
                break;
        }
    }
}  /* j_config_parse_base() */


/*
 * static void j_config_parse_to_eol(GScanner *scanner)
 * 
 * This is a simple function that just finds the end of a line,
 * throwing away anything it sees.
 */
static void j_config_parse_to_eol(GScanner *scanner)
{
    gboolean done;
    GTokenType token;
    GTokenValue *value;

    value = &scanner->value;

    done = FALSE;
    while (done == FALSE)
    {
        token = g_scanner_peek_next_token(scanner);
        if (token == G_TOKEN_EOF)
            done = TRUE;
        else
        {
            token = g_scanner_get_next_token(scanner);
            if ((token = G_TOKEN_CHAR) && (value->v_char == '\n'))
                done = TRUE;
        }
    }
}  /* j_config_parse_to_eol() */


/*
 * static void j_config_parse_comment(GScanner *scanner)
 * 
 * Parses a comment out of the file, throwing it away.
 */
static void j_config_parse_comment(GScanner *scanner)
{
    GTokenType token;
    GTokenValue *value;
    
    value = &scanner->value;
    token = g_scanner_get_next_token(scanner);
    
    g_assert(token == G_TOKEN_CHAR);
    g_assert(value->v_char == '#');
    
    j_config_parse_to_eol(scanner);
    
#ifdef DEBUG
    g_print("Skipped comment\n");
#endif /* DEBUG */
}  /* j_config_parse_comment() */


/*
 * static void j_config_parse_stanza_name(GScanner *scanner,
 *                                        JConfigCtxt *cfc)
 * 
 * If a line starts with a non-whitespace character, it signifies a
 * new stanza.  Stanza names are defined by the Perl-style regular
 * expression /^\w+:\s*\n/.  Just by virtue of entering this function,
 * any current stanza is closed.
 * 
 * The gotos are because I decided it was cleaner than a
 * g_free() at every error.
 */
static void j_config_parse_stanza_name(GScanner *scanner,
                                       JConfigCtxt *cfc)
{
    gboolean done;
    gchar *stanza_name;
    GTokenType token;
    GTokenValue *value;
    
    value = &scanner->value;
    token = g_scanner_get_next_token(scanner);
    
    g_assert(token == G_TOKEN_IDENTIFIER);
    
    if (cfc->cfs != NULL)
        cfc->cfs = NULL;

    stanza_name = g_strdup(value->v_identifier);

    token = g_scanner_peek_next_token(scanner);
    if (token == G_TOKEN_EOF)
    {
        if (cfc->verbose)
            g_warning("Invalid stanza name declaration: missing ':'\n");
        cfc->error = TRUE;
        goto stanza_name_free;
    }
    
    token = g_scanner_get_next_token(scanner);
    if (token != G_TOKEN_CHAR)
    {
        if (cfc->verbose)
            g_warning("Invalid stanza name declaration\n");
        cfc->error = TRUE;
        j_config_parse_to_eol(scanner);
        goto stanza_name_free;
    }
    if (value->v_char == '\n')
    {
        if (cfc->verbose)
            g_warning("Invalid stanza name declaration: missing ':'\n");
        cfc->error = TRUE;
        goto stanza_name_free;
    }
    if (value->v_char != ':')
    {
        if (cfc->verbose)
        {
            g_warning("Invalid character in stanza name declaration: %c\n",
                      value->v_char);
        }
        cfc->error = TRUE;
        j_config_parse_to_eol(scanner);
        goto stanza_name_free;
    }
    
    done = FALSE;
    while (done == FALSE)
    {
        token = g_scanner_peek_next_token(scanner);
        if (token == G_TOKEN_EOF)
            done = TRUE;
        else if (token == G_TOKEN_CHAR)
        {
            token = g_scanner_get_next_token(scanner);
            if (value->v_char == '\n')
                done = TRUE;
            else if (strchr(CF_SCAN_WHITESPACE, value->v_char) == NULL)
            {
                if (cfc->verbose)
                    g_warning("Trailing garbage on stanza name declaration\n");
                cfc->error = TRUE;
                j_config_parse_to_eol(scanner);
                goto stanza_name_free;
            }
        }
        else
        {
            if (cfc->verbose)
                g_warning("Trailing garbage on stanza name declaration\n");
            cfc->error = TRUE;
            j_config_parse_to_eol(scanner);
            goto stanza_name_free;
        }
    }
    
    cfc->cfs = j_config_add_stanza(cfc->cf, stanza_name);

#ifdef DEBUG
    g_print("New stanza: %s\n", stanza_name);
#endif /* DEBUG */

stanza_name_free:
    g_free(stanza_name);
}  /* j_config_parse_stanza_name() */


/*
 * static void j_config_parse_white_start(GScanner *scanner,
 *                                        JConfigCtxt *cfc)
 * 
 * If a line starts with whitespace, it is either a blank line, or an
 * attribute line.  We skip whitespace lines.
 * If it looks like an attribute line, we call the proper function.
 */
static void j_config_parse_white_start(GScanner *scanner,
                                       JConfigCtxt *cfc)
{
    gboolean done;
    GTokenType token;
    GTokenValue *value;
    
    value = &scanner->value;
    token = g_scanner_get_next_token(scanner);
    
    g_assert(token == G_TOKEN_CHAR);
    g_assert(strchr(CF_SCAN_WHITESPACE, value->v_char) != NULL);
    
    done = FALSE;
    while (done == FALSE)
    {
        token = g_scanner_peek_next_token(scanner);
        switch (token)
        {
            case G_TOKEN_EOF:
                done = TRUE;
                break;
            case G_TOKEN_IDENTIFIER:
                j_config_parse_stanza_attr(scanner, cfc);
                done = TRUE;
                break;
            case G_TOKEN_CHAR:
                token = g_scanner_get_next_token(scanner);
                if (value->v_char == '\n')
                {
#ifdef DEBUG
                    g_print("Skipped whitespace line\n");
#endif /* DEBUG */
                    if (cfc->cfs != NULL)
                        cfc->cfs = NULL;
                    done = TRUE;
                }
                else if (strchr(CF_SCAN_WHITESPACE, value->v_char) == NULL)
                {
                    if (cfc->verbose)
                    {
                        g_warning("Invalid character in attribute name: %c\n",
                                  value->v_char);
                    }
                    cfc->error = TRUE;
                    j_config_parse_to_eol(scanner);
                    done = TRUE;
                }
                break;
            default:
                break;
        }
    }
}  /* j_config_parse_white_start() */


/*
 * static void j_config_parse_stanza_attr(GScanner *scanner,
 *                                        JConfigCtxt *cfc)
 * 
 * An attribute is of the form <attribute name>=<attribute value>.
 * <attribute name> is defined by the Perl-style regular expression
 * /^\s+\w+\s* /.  Note that the leading whitespace (/^\s+/) has been
 * stripped by j_config_parse_white_start() already.
 * <attribute value> is defined as /\s*.*\n/.  The leading whitespace
 * (between the "=" and the first non-whitespace character in
 * <attribute value>) is stripped, as is the newline.
 *
 * This now supports continuation lines.  An attribute line ending in
 * '\<newline>' will cause a <newline> to be entered into the buffer
 * and the next line will be treated as part of the attribute.
 */
static void j_config_parse_stanza_attr(GScanner *scanner,
                                       JConfigCtxt *cfc)
{
    gboolean done, multi_line;
    gchar *attr_name;
    GString *attr_value;
    GTokenType token;
    GTokenValue *value;
    
    enum
    {
        CF_STATE_ID,
        CF_STATE_EQUAL,
        CF_STATE_VALUE
    } cur_state;
    
    value = &scanner->value;
    token = g_scanner_get_next_token(scanner);
    
    g_assert(token == G_TOKEN_IDENTIFIER);
    
    if (cfc->cfs == NULL)
    {
        if (cfc->verbose)
            g_warning("Attributes require a matching stanza\n");
        cfc->error = TRUE;
        j_config_parse_to_eol(scanner);
        return;
    }

    attr_name = g_strdup(value->v_identifier);
    attr_value = g_string_new(NULL);
    
    cur_state = CF_STATE_ID;
    done = FALSE;
    multi_line = FALSE;
    while (done == FALSE)
    {
        token = g_scanner_peek_next_token(scanner);
        if (token == G_TOKEN_EOF)
        {
            if (cur_state == CF_STATE_ID)
            {
                if (cfc->verbose)
                    g_warning("Invalid attribute: missing '='\n");
                cfc->error = TRUE;
                goto attribute_free;
            }
            done = TRUE;
        }
        else if (token == G_TOKEN_CHAR)
        {
            token = g_scanner_get_next_token(scanner);
            if (value->v_char == '\n')
            {
                if (cur_state == CF_STATE_ID)
                {
                    if (cfc->verbose)
                        g_warning("Invalid attribute: missing '='\n");
                    cfc->error = TRUE;
                    goto attribute_free;
                }
                if (multi_line == FALSE)
                    done = TRUE;
                else
                {
                    g_string_append_c(attr_value, value->v_char);
                    multi_line = FALSE;
                }
            }
            else if (multi_line == TRUE)
            {
                g_string_append_c(attr_value, '\\');
                multi_line = FALSE;

                if (value->v_char == '\\')
                    multi_line = TRUE;
                else
                    g_string_append_c(attr_value, value->v_char);
            }
            else if (cur_state == CF_STATE_ID)
            {
                if (value->v_char == '=')
                    cur_state = CF_STATE_EQUAL;
                else if (strchr(CF_SCAN_WHITESPACE, value->v_char) == NULL)
                {
                    if (cfc->verbose)
                        g_warning("Invalid attribute: expecting '='\n");
                    cfc->error = TRUE;
                    j_config_parse_to_eol(scanner);
                    goto attribute_free;
                }
            }
            else if (cur_state == CF_STATE_EQUAL)
            {
                if (strchr(CF_SCAN_WHITESPACE, value->v_char) == NULL)
                {
                    cur_state = CF_STATE_VALUE;
                    if (value->v_char == '\\')
                        multi_line = TRUE;
                    else
                        g_string_append_c(attr_value, value->v_char);
                }
            }
            else
            {
                if (value->v_char == '\\')
                    multi_line = TRUE;
                else
                    g_string_append_c(attr_value, value->v_char);
            }
        }
        else
        {
            token = g_scanner_get_next_token(scanner);
            if (cur_state == CF_STATE_ID)
            {
                if (cfc->verbose)
                    g_warning("Invalid attribute: expecting '='\n");
                cfc->error = TRUE;
                j_config_parse_to_eol(scanner);
                return;
            }
            else if (cur_state == CF_STATE_EQUAL)
                cur_state = CF_STATE_VALUE;

            if (token != G_TOKEN_IDENTIFIER)
            {
                if (cfc->verbose)
                    g_warning("Error parsing value\n");
                cfc->error = TRUE;
                j_config_parse_to_eol(scanner);
                goto attribute_free;
            }
            
            if (multi_line == TRUE)
            {
                g_string_append_c(attr_value, '\\');
                multi_line = FALSE;
            }

            g_string_append(attr_value, value->v_identifier);
        }
    }
    
    
    j_config_set_attribute(cfc->cfs, attr_name, attr_value->str);
#ifdef DEBUG
    g_print("Attribute: \"%s\" = \"%s\"\n", attr_name, attr_value->str);
#endif /* DEBUG */
    
attribute_free:
    g_free(attr_name);
    g_string_free(attr_value, TRUE);
}


/*
 * static JConfig *j_config_config_new()
 * 
 * Allocates a new JConfig structure
 */
static JConfig *j_config_config_new(void)
{
    JConfig *cf;
    
    cf = g_new(JConfig, 1);
    if (cf == NULL) return(NULL);
    
    cf->filename = NULL;
    cf->stanza_names = NULL;
    cf->stanzas = g_hash_table_new(g_str_hash, g_str_equal);
    if (cf->stanzas == NULL)
    {
        g_free(cf);
        return(NULL);
    }
    
    return(cf);
}  /* j_config_config_new() */


/*
 * static JConfigStanza *j_config_stanza_new()
 * 
 * Allocates a new JConfigStanza structure
 */
static JConfigStanza *j_config_stanza_new(void)
{
    JConfigStanza *cfs;
    
    cfs = g_new(JConfigStanza, 1);
    if (cfs == NULL) return(NULL);
    
    cfs->stanza_name = NULL;
    cfs->attrs = g_hash_table_new(g_str_hash, g_str_equal);
    if (cfs->attrs == NULL)
    {
        g_free(cfs);
        return(NULL);
    }
    
    return(cfs);
}  /* j_config_stanza_new() */


/*
 * static void j_config_foreach_attr_print(gpointer key,
 *                                         gpointer value,
 *                                         gpointer o_ctxt)
 *                                   
 * Prints each attribute -> value pair.
 */
static void j_config_foreach_attr_print(gpointer key,
                                        gpointer value,
                                        gpointer o_ctxt)
{
    gchar delimiter[2] = {'\n', '\0'};
    gchar **output_lines;
    gboolean first_line;
    gint i, rc;
    JOutputCtxt *ctxt;

    ctxt = (JOutputCtxt *)o_ctxt;
    if (ctxt->success == FALSE)
        return;

    if ((value == NULL) || (((gchar *)value)[0] == '\0'))
    {
        rc = fprintf(ctxt->output, "\t%s =\n", (gchar *)key);
        if (rc < 1)
            ctxt->success = FALSE;
        return;
    }

    output_lines = g_strsplit(value, delimiter, 0);
    if (output_lines == NULL)
    {
#if DEBUG
        g_warning("Unable to allocate memory for multiline attribute: %s\n",
                  g_strerror(errno));
#endif
        ctxt->success = FALSE;
        return;
    }

    first_line = TRUE;
    rc = 0;
    for (i = 0; output_lines[i] != NULL; i++)
    {
        if (first_line == TRUE)
        {
            rc = fprintf(ctxt->output, "\t%s = %s",
                         (gchar *)key, output_lines[i]);
            first_line = FALSE;
        }
        else
            rc = fprintf(ctxt->output, "\\\n%s", output_lines[i]);
    }
    g_strfreev(output_lines);

    if (rc < 1)
    {
        ctxt->success = FALSE;
        return;
    }

    rc = fprintf(ctxt->output, "\n");
    if (rc < 1)
        ctxt->success = FALSE;
}  /* j_config_foreach_attr_print() */


/*
 * static void j_config_foreach_stanza_print(gpointer key,
 *                                           gpointer value,
 *                                           gpointer o_ctxt)
 *                                     
 * Runs through each stanza, printing the header and
 * calling j_config_foreach_attr_print() on the attributes.
 */
static void j_config_foreach_stanza_print(gpointer key,
                                          gpointer value,
                                          gpointer o_ctxt)
{
    gint rc;
    GList *elem;
    JConfigStanza *cfs;
    JOutputCtxt *ctxt;
    
    elem = (GList *)value;
    ctxt = (JOutputCtxt *)o_ctxt;
    if (ctxt->success == FALSE)
        return;

    while (elem)
    {
        cfs = (JConfigStanza *)elem->data;
        rc = fprintf(ctxt->output, "%s:\n", (gchar *)key);
        if (rc < 1)
        {
            ctxt->success = FALSE;
            break;
        }

        g_hash_table_foreach(cfs->attrs, j_config_foreach_attr_print,
                             o_ctxt);

        rc = fprintf(ctxt->output, "\n");
        if (rc < 1)
        {
            ctxt->success = FALSE;
            break;
        }
        elem = g_list_next(elem);
    }
}  /* j_config_foreach_stanza_print() */


/*
 * gboolean j_config_dump_file(JConfig *cf,
 *                             const gchar *o_ctxt)
 * 
 * Prints the configuration.
 *
 * FIXME: This needs to do more work to protect the resulting file from
 * ENOSPC, etc.
 */
gboolean j_config_dump_file(JConfig *cf,
                            const gchar *output_file)
{
    gint rc;
    JOutputCtxt ctxt;

    g_return_val_if_fail(cf != NULL, FALSE);
    g_return_val_if_fail(output_file != NULL, FALSE);

    ctxt.success = TRUE;
    if (strcmp(output_file, "-") == 0)
        ctxt.output = stdout;
    else
        ctxt.output = fopen(output_file, "w");

    if (ctxt.output == NULL)
    {
#if DEBUG
        g_warning("Unable to open file \"%s\" for writing: %s\n",
                  output_file,
                  g_strerror(errno));
#endif
        return(FALSE);
    }
    g_hash_table_foreach(cf->stanzas,
                         j_config_foreach_stanza_print,
                         &ctxt);
    if (strcmp(output_file, "-") != 0)
    {
        rc = fclose(ctxt.output);
        if (rc != 0)
            ctxt.success = FALSE;
    }

    return(ctxt.success);
}  /* j_config_dump_file() */


/*
 * JIterator *j_config_get_stanza_names(JConfig *cf)
 * 
 * Returns a list of stanzas contained in this config
 */
JIterator *j_config_get_stanza_names(JConfig *cf)
{
    g_return_val_if_fail(cf != NULL, NULL);

    return(j_iterator_new_from_list(cf->stanza_names));
}  /* j_config_get_stanza_names() */


/*
 * JConfigStanza *j_config_get_stanza_nth(JConfig *cf,
 *                                        const gchar *stanza_name,
 *                                        guint n)
 *
 * Retreives the nth stanza with the given name, or NULL if
 * that does not exist
 */
JConfigStanza *j_config_get_stanza_nth(JConfig *cf,
                                       const gchar *stanza_name,
                                       guint n)
{
    JConfigStanza *cfs;
    GList *elem;

    g_return_val_if_fail(cf != NULL, NULL);
    g_return_val_if_fail(stanza_name != NULL, NULL);
    
    elem = g_hash_table_lookup(cf->stanzas, stanza_name);
    if (elem == NULL)
        return(NULL);
    
    elem = g_list_nth(elem, n);
    cfs = elem != NULL ? (JConfigStanza *)elem->data : NULL;

    return(cfs);
}  /* j_config_get_stanza_nth() */


/*
 * gchar *j_config_get_stanza_name(JConfigStanza *cfs)
 *
 * Returns the stanza's name
 */
gchar *j_config_get_stanza_name(JConfigStanza *cfs)
{
    g_return_val_if_fail(cfs != NULL, NULL);

    return(g_strdup(cfs->stanza_name));
}  /* j_config_get_stanza_name() */


/*
 * JConfigStanza *j_config_add_stanza(JConfig *cf,
 *                                    const gchar *stanza_name);
 *
 * Adds a new stanza to the configuration structure
 */
JConfigStanza *j_config_add_stanza(JConfig *cf,
                                   const gchar *stanza_name)
{
    JConfigStanza *cfs;
    GList *elem;

    g_return_val_if_fail(cf != NULL, NULL);
    g_return_val_if_fail(stanza_name != NULL, NULL);

    cfs = j_config_stanza_new();
    if (cfs == NULL)
    {
#if DEBUG
        g_warning("Unable to allocate memory for a new stanza: %s\n",
                  g_strerror(errno));
#endif
        return NULL;
    }

    cfs->stanza_name = g_strdup(stanza_name);
    elem = (GList *)g_hash_table_lookup(cf->stanzas, stanza_name);
    if (elem == NULL)
    {
        cf->stanza_names =
            g_list_append(cf->stanza_names, g_strdup(stanza_name));
        elem = g_list_append(NULL, cfs);
        g_hash_table_insert(cf->stanzas,
                            g_strdup(stanza_name),
                            elem);
    }
    else
        elem = g_list_append(elem, cfs);

    return(cfs);
}  /* j_config_add_stanza() */


/*
 * void j_config_delete_stanza(JConfig *cf,
 *                             JConfigStanza *cfs)
 *
 * Removes the stanza pointed to by cfs from the configuration.
 */
void j_config_delete_stanza(JConfig *cf,
                            JConfigStanza *cfs)
{
    GList *elem, *new_elem;
    gpointer orig_key;
    gchar *orig_data;

    g_return_if_fail(cf != NULL);
    g_return_if_fail(cfs != NULL);

    elem = (GList *)g_hash_table_lookup(cf->stanzas, cfs->stanza_name);
    if (elem == NULL)
    {
#if DEBUG
        g_warning("Unable to remove stanza - no such class: %s\n",
                  cfs->stanza_name);
#endif
        return;
    }

    new_elem = g_list_remove(elem, cfs);
    if (elem == NULL)
    {
        if (g_hash_table_lookup_extended(cf->stanzas,
                                         cfs->stanza_name,
                                         &orig_key,
                                         NULL))
        {
            g_hash_table_remove(cf->stanzas, cfs->stanza_name);
            g_free(orig_key);
        }

        elem = cf->stanza_names;
        while (elem != NULL)
        {
            orig_data = (gchar *)elem->data;
            if (strcmp(cfs->stanza_name, orig_data) == 0)
            {
                cf->stanza_names =
                    g_list_remove(cf->stanza_names, elem->data);
                g_free(orig_data);
                break;
            }
            elem = g_list_next(elem);
        }
    }
    else if (elem != new_elem)
    {
        if (g_hash_table_lookup_extended(cf->stanzas,
                                         cfs->stanza_name,
                                         &orig_key,
                                         NULL))
        {
            g_hash_table_remove(cf->stanzas, cfs->stanza_name);
            g_free(orig_key);
        }
        g_hash_table_insert(cf->stanzas,
                            g_strdup(cfs->stanza_name),
                            new_elem);
    }

    j_config_free_stanza(cfs);
}  /* j_config_delete_stanza() */


/*
 * void j_config_delete_stanza_nth(JConfig *cf,
 *                                 const gchar *stanza_name,
 *                                 guint n)
 *
 * Removes the nth stanza of class stanza_name from the
 * configuration.
 */
void j_config_delete_stanza_nth(JConfig *cf,
                                const gchar *stanza_name,
                                guint n)
{
    JConfigStanza *cfs;

    g_return_if_fail(cf != NULL);
    g_return_if_fail(stanza_name != NULL);

    cfs = j_config_get_stanza_nth(cf, stanza_name, n);
    if (cfs == NULL)
        return;

    j_config_delete_stanza(cf, cfs);
}  /* j_config_delete_stanza() */


/*
 * JConfig *j_config_parse_file_with_context(JConfigCtxt *cfc,
 *                                           const gchar *filename)
 * 
 * Parses a config file
 */
JConfig *j_config_parse_file_with_context(JConfigCtxt *cfc,
                                          const gchar *filename)
{
    gint fd;
    JConfig *cf;

    g_return_val_if_fail(cfc != NULL, NULL);
    g_return_val_if_fail(filename != NULL, NULL);

    if (strcmp(filename, "-") == 0)
        fd = STDIN_FILENO;
    else
        fd = open(filename, O_RDONLY);

    if (fd < 0)
    {
        if (cfc->verbose)
        {
            g_warning("Unable to open file \"%s\": %s\n",
                      filename,
                      g_strerror(errno));
        }
        cfc->error = TRUE;
        return(NULL);
    }
    
    cf = j_config_parse_any(cfc, filename, fd, NULL, 0);
    
    if (strcmp(filename, "-") != 0)
        close(fd);

    return(cf);
}  /* j_config_parse_file_with_context() */


/*
 * JConfig *j_config_parse_file(const gchar *filename)
 * 
 * Parses a config file, creating the context.
 */
JConfig *j_config_parse_file(const gchar *filename)
{
    JConfigCtxt *cfc;
    JConfig *cf = NULL;

    cfc = j_config_new_context();
    if (cfc)
    {
        cf = j_config_parse_file_with_context(cfc, filename);
        if (cfc->cfs)
            j_config_free_stanza(cfc->cfs);
        j_config_context_free(cfc);
    }

    return(cf);
}  /* j_config_parse_file() */


/*
 * JConfig *j_config_parse_memory_with_context(JConfigCtxt *cfc,
 *                                             gchar *buffer,
 *                                             gint buf_len)
 * 
 * Parses a config from a text buffer
 */
JConfig *j_config_parse_memory_with_context(JConfigCtxt *cfc,
                                            gchar *buffer,
                                            gint buf_len)
{
    JConfig *cf;
    
    g_return_val_if_fail(cfc != NULL, NULL);
    g_return_val_if_fail(buffer != NULL, NULL);

    if (buf_len < 0)
        buf_len = 0;
    
    cf = j_config_parse_any(cfc, "memory", -1, buffer, buf_len);
    
    return(cf);
}  /* j_config_parse_memory() */


/*
 * JConfig *j_config_parse_memory(gchar *buffer, gint buf_len)
 * 
 * Parses a buffer, creating the context.
 */
JConfig *j_config_parse_memory(gchar *buffer, gint buf_len)
{
    JConfigCtxt *cfc;
    JConfig *cf = NULL;

    cfc = j_config_new_context();
    if (cfc)
    {
        cf = j_config_parse_memory_with_context(cfc, buffer, buf_len);
        if (cfc->cfs)
            j_config_free_stanza(cfc->cfs);
        j_config_context_free(cfc);
    }

    return(cf);
}  /* j_config_parse_file() */


/*
 * JConfigMatch *j_config_match_build(guint num_matches, ...)
 *
 * Convenience function to build an array of JConfigMatch structures.
 * num_matches is the number of key, value pairs passed to this
 * function.  The number of varargs passed should therefore be double
 * num_matches.  eg:
 *
 * j_config_match_build(2, "foo", "bar", "quuz", "quuuz");
 *
 * This function builds a match that requires:
 *
 *     foo = bar
 *     quuz = quuuz
 *
 * Strings passed in are *NOT* copied.  They are merely referenced.
 * Because of this, the returned array of JConfigMatch structures can
 * be freed with g_free().  The caller must take care not to change or
 * destroy the referened strings until they are done with the
 * array of JConfigMatch structures.
 */
JConfigMatch *j_config_match_build(guint num_matches, ...)
{
    guint i;
    va_list args;
    JConfigMatch *matches;

    matches = g_new0(JConfigMatch, num_matches);

    va_start(args, num_matches);
    for (i = 0; i < num_matches; i++)
    {
        matches[i].type = J_CONFIG_MATCH_VALUE;
        matches[i].name = va_arg(args, gchar *);
        if (matches[i].name == NULL)
        {
            g_free(matches);
            matches = NULL;
            break;
        }
        matches[i].value = va_arg(args, gchar *);
    }
    va_end(args);

    return(matches);
}  /* j_config_match_build() */


/*
 * JIterator *j_config_get_stanzas(JConfig *cf,
 *                                 const gchar *stanza_name,
 *                                 JConfigMatch *matches,
 *                                 guint num_matches)
 * 
 * Gets the list of stanzas with a given name.  Optionally
 * return only those that staitisfy matches.  matches is an array
 * of JConfigMatch structures.  num_matches describes the number
 * of items in the array.  This array can be built by the caller
 * explicitly, or the caller can use the convenience function
 * j_config_match_build().  If num_matches is 0, matches can be
 * NULL.
 */
JIterator *j_config_get_stanzas(JConfig *cf,
                                const gchar *stanza_name,
                                JConfigMatch *matches,
                                guint num_matches)
{
    gint i;
    JConfigStanza *cfs;
    gchar *value;
    GList *elem, *tmp;
    JIterator *iter;
    
    g_return_val_if_fail(cf != NULL, NULL);
    g_return_val_if_fail(stanza_name != NULL, NULL);
    g_return_val_if_fail((num_matches == 0) || (matches != NULL), NULL);

    elem = (GList *)g_hash_table_lookup(cf->stanzas, stanza_name);

    if (num_matches > 0)
    {
        tmp = NULL;
        for (; elem != NULL; elem = g_list_next(elem))
        {
            cfs = (JConfigStanza *)elem->data;
            g_assert(cfs != NULL);

            for (i = 0; i < num_matches; i++)
            {
                g_assert(matches[i].name != NULL);
                value = j_config_get_attribute(cfs, matches[i].name);
                if (value != NULL)
                {
                    if (matches[i].value == NULL)
                    {
                        if (value[0] != '\0')
                        {
                            g_free(value);
                            break;
                        }
                    }
                    else if (strcmp(value, matches[i].value) != 0)
                    {
                        g_free(value);
                        break;
                    }
                    g_free(value);
                }
                else
                {
                    if (matches[i].value != NULL)
                        break;
                }
            }
            if (i >= num_matches) /* All tests succeeded */
                tmp = g_list_prepend(tmp, cfs);
        }
        elem = g_list_reverse(tmp);

        iter = j_iterator_new_from_list(elem);
        g_list_free(elem);
    }
    else
        iter = j_iterator_new_from_list(elem);
    
    return(iter);
}  /* j_config_get_stanzas() */


/*
 * void j_config_set_attribute(JConfigStanza *cfs,
 *                             const gchar *attr_name,
 *                             const gchar *attr_value)
 *
 * Sets the value of a given attribute name
 */
void j_config_set_attribute(JConfigStanza *cfs,
                            const gchar *attr_name,
                            const gchar *attr_value)
{
    g_return_if_fail(cfs != NULL);
    g_return_if_fail(attr_name != NULL);

    j_config_delete_attribute(cfs, attr_name);
    g_hash_table_insert(cfs->attrs,
                        g_strdup(attr_name),
                        g_strdup(attr_value));
}  /* j_config_set_attribute() */


/*
 * static void j_config_attr_names_foreach(gpointer key,
 *                                         gpointer value,
 *                                         gpointer user_data)
 *
 * Foreach function to build the attribute names list.
 */
static void j_config_attr_names_foreach(gpointer key,
                                        gpointer value,
                                        gpointer user_data)
{
    GList **list = (GList **)user_data;

    *list = g_list_append(*list, key);
}  /* j_config_attr_names_foreach() */


/*
 * JIterator *j_config_get_attribute_names(JConfigStanza *cfs)
 *
 * Returns an iterator over every attribute name in the stanza.
 */
JIterator *j_config_get_attribute_names(JConfigStanza *cfs)
{
    JIterator *iter;
    GList *attr_names = NULL;

    g_return_val_if_fail(cfs != NULL, NULL);

    g_hash_table_foreach(cfs->attrs, j_config_attr_names_foreach,
                         &attr_names);

    iter = j_iterator_new_from_list(attr_names);
    g_list_free(attr_names);

    return(iter);
}  /* j_config_get_attribute_names() */


/*
 * gchar *j_config_get_attribute(JConfigStanza *cfs,
 *                               const gchar *attr_name)
 * 
 * Retrieves the value of a given attribute name
 */
gchar *j_config_get_attribute(JConfigStanza *cfs,
                              const gchar *attr_name)
{
    gchar *s;

    g_return_val_if_fail(cfs != NULL, NULL);
    g_return_val_if_fail(attr_name != NULL, NULL);
    
    s = (gchar *)g_hash_table_lookup(cfs->attrs, attr_name);
    s = g_strdup(s);
    if (s)
        g_strchomp(s);

    return(s);
}  /* j_config_get_attribute() */


/*
 * void j_config_delete_attribute(JConfigStanza *cfs,
 *                                const gchar *attr_name)
 *
 * Removes the attribute attr_name from the stanza pointed to
 * by cfs.
 */
void j_config_delete_attribute(JConfigStanza *cfs,
                               const gchar *attr_name)
{
    gpointer orig_key, orig_value;

    g_return_if_fail(cfs != NULL);
    g_return_if_fail(attr_name != NULL);

    if (g_hash_table_lookup_extended(cfs->attrs,
                                     attr_name,
                                     &orig_key,
                                     &orig_value))
    {
        g_hash_table_remove(cfs->attrs,
                            attr_name);
        g_free(orig_key);
        g_free(orig_value);
    }
}  /* j_config_delete_attribute() */


/*
 * static void j_config_free_stanza_node(gpointer key,
 *                                       gpointer value,
 *                                       gpointer thrway)
 * 
 * Removes a node from a stanza
 */
static void j_config_free_stanza_node(gpointer key,
                                      gpointer value,
                                      gpointer thrway)
{
    g_free(key);
    g_free(value);
}  /* j_config_free_stanza_node() */


/*
 * static void j_config_free_stanza(JConfigStanza *cfs)
 * 
 * Clears a config file stanza
 */
static void j_config_free_stanza(JConfigStanza *cfs)
{
    g_return_if_fail(cfs != NULL);

    g_hash_table_foreach(cfs->attrs, j_config_free_stanza_node, NULL);
    g_hash_table_destroy(cfs->attrs);
    g_free(cfs->stanza_name);
    g_free(cfs);
}  /* j_config_free_stanza() */


/*
 * static void j_config_free_stanza_proxy(gpointer data,
 *                                        gpointer thrway)
 * 
 * A proxy to call j_config_free_stanza() as a GFunc()
 */
static void j_config_free_stanza_proxy(gpointer elem_data,
                                       gpointer thrway)
{
    j_config_free_stanza(elem_data);
}  /* j_config_free_stanza_proxy() */


/*
 * static void j_config_free_config_node(gpointer key,
 *                                       gpointer value,
 *                                       gpointer thrway)
 * 
 * Removes a node from a config
 */
static void j_config_free_config_node(gpointer key,
                                      gpointer value,
                                      gpointer thrway)
{
    GList *elem;
    
    elem = (GList *)value;
    g_list_foreach(elem, j_config_free_stanza_proxy, NULL);
    g_list_free(elem);
    g_free(key);
}  /* j_config_free_config_node() */


/*
 * void j_config_free(JConfig *cf)
 * 
 * Deletes a config file tree
 */
void j_config_free(JConfig *cf)
{
    g_return_if_fail(cf != NULL);
    GList *list;

    g_hash_table_foreach(cf->stanzas, j_config_free_config_node, NULL);
    g_hash_table_destroy(cf->stanzas);
    g_free(cf->filename);

    list = cf->stanza_names;
    while (list)
    {
        g_free(list->data);
        list->data = NULL;
        list = list->next;
    }
    g_list_free(cf->stanza_names);

    g_free(cf);
}  /* j_config_free() */


/*
 * static void j_config_foreach_attr_append(gpointer key,
 *                                          gpointer value,
 *                                          gpointer fbuffer)
 *                                   
 * Prints each attribute -> value pair.
 */
static void j_config_foreach_attr_append(gpointer key,
                                         gpointer value,
                                         gpointer fbuffer)
{
    gchar delimiter[2] = {'\n', '\0'};
    gchar **output_lines;
    gboolean first_line;
    gint i;
    GString *buffer;

    g_return_if_fail(fbuffer != NULL);

    buffer = (GString *)fbuffer;

    if ((value == NULL) || (((gchar *)value)[0] == '\0'))
    {
        g_string_append_printf(buffer, "\t%s =\n", (gchar *)key);
        return;
    }

    output_lines = g_strsplit(value, delimiter, 0);
    if (output_lines == NULL)
    {
        g_warning("Unable to allocate memory for multiline attribute: %s\n",
                  g_strerror(errno));
        return;
    }

    first_line = TRUE;
    for (i = 0; output_lines[i] != NULL; i++)
    {
        if (first_line == TRUE)
        {
            g_string_append_printf(buffer, "\t%s = %s", 
                              (gchar *)key, output_lines[i]);
            first_line = FALSE;
        }
        else
            g_string_append_printf(buffer, "\\\n%s", output_lines[i]);
    }
    g_string_append_printf(buffer, "\n");

    g_strfreev(output_lines);
}  /* j_config_foreach_attr_append() */


/*
 * static void j_config_foreach_stanza_append(gpointer key,
 *                                            gpointer value,
 *                                            gpointer fbuffer)
 *                                     
 * Runs through each stanza, printing the header and
 * calling j_config_foreach_attr_append() on the attributes.
 */
static void j_config_foreach_stanza_append(gpointer key,
                                           gpointer value,
                                           gpointer fbuffer)
{
    GList *elem;
    JConfigStanza *cfs;
    GString *buffer;
    
    g_return_if_fail(fbuffer != NULL);

    buffer = (GString *)fbuffer;
    elem = (GList *)value;

    while (elem)
    {
        cfs = (JConfigStanza *)elem->data;
        g_string_append_printf(buffer, "%s:\n", (gchar *)key);
        g_hash_table_foreach(cfs->attrs, j_config_foreach_attr_append,
                             fbuffer);
        g_string_append(buffer, "\n");
        elem = g_list_next(elem);
    }
}  /* j_config_foreach_stanza_append() */


/*
 * gchar *j_config_dump_memory(JConfig *cf)
 * 
 * Prints the configuration to an in-memory string.
 */
gchar *j_config_dump_memory(JConfig *cf)
{
    gchar *output_text;
    GString *output;

    g_return_val_if_fail(cf != NULL, NULL);

    output = g_string_new(NULL);
    if (output == NULL)
        return(NULL);
    g_hash_table_foreach(cf->stanzas,
                         j_config_foreach_stanza_append,
                         (gpointer)output);

    output_text = output->str;
    g_string_free(output, FALSE);
    return(output_text);
}  /* j_config_dump_memory() */


/*
 * jconfig.h
 * 
 * Header file for configuration file parser
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


#ifndef __JCONFIG_H
#define __JCONFIG_H



/*
 * Type definitions
 */
typedef struct _JConfigStanza JConfigStanza;
typedef struct _JConfig JConfig;
typedef struct _JConfigMatch JConfigMatch;
typedef struct _JConfigCtxt JConfigCtxt;



/*
 * Enums
 */
typedef enum _JConfigMatchType
{
    J_CONFIG_MATCH_VALUE = 0,
} JConfigMatchType;



/*
 * Structures
 */
struct _JConfigMatch
{
    JConfigMatchType type;
    gchar *name;
    gchar *value;
};



/*
 * Functions
 */

JConfig *j_config_parse_file(const gchar *filename);
JConfig *j_config_parse_memory(gchar *buffer, gint buf_len);
JConfigCtxt *j_config_new_context(void);
void j_config_context_free(JConfigCtxt *cfc);
void j_config_context_set_verbose(JConfigCtxt *cfc, gboolean verbose);
gboolean j_config_context_get_error(JConfigCtxt *cfc);
JConfig *j_config_parse_file_with_context(JConfigCtxt *cfc,
                                          const gchar *filename);
JConfig *j_config_parse_memory_with_context(JConfigCtxt *cfc,
                                            gchar *buffer,
                                            gint buf_len);
JIterator *j_config_get_stanza_names(JConfig *cf);
JConfigMatch *j_config_match_build(guint num_matches, ...);
JIterator *j_config_get_stanzas(JConfig *cf,
                                const gchar *stanza_name,
                                JConfigMatch *matches,
                                guint num_matches);
JConfigStanza *j_config_get_stanza_nth(JConfig *cf,
                                       const gchar *stanza_name,
                                       guint n);
gchar *j_config_get_stanza_name(JConfigStanza *cfs);
JConfigStanza *j_config_add_stanza(JConfig *cf,
                                   const gchar *stanza_name);
void j_config_delete_stanza(JConfig *cf, JConfigStanza *cfs);
void j_config_delete_stanza_nth(JConfig *cf,
                                const gchar *stanza_name,
                                guint n);
JIterator *j_config_get_attribute_names(JConfigStanza *cfs);
gchar *j_config_get_attribute(JConfigStanza *cfs,
                              const gchar *attr_name);
void j_config_set_attribute(JConfigStanza *cfs,
                            const gchar *attr_name,
                            const gchar *attr_value);
void j_config_delete_attribute(JConfigStanza *cfs,
                               const gchar *attr_name);
gboolean j_config_dump_file(JConfig *cf, const gchar *output_file);
gchar *j_config_dump_memory(JConfig *cf);
void j_config_free(JConfig *cf);

#endif /* __JCONFIG_H */

/* owd-euses: defines the information buffer and various reporting functions
 * Oliver Dixon. */

#ifndef _ERROR_H
#define _ERROR_H

#include "stack.h"

/* In general, these functions should be the only calls directly conferring with
 * a standard output buffer. They are primarily for error-reporting, so should
 * not be able to fail to a point which would invoke a (further) fatal error. */

void print_fatal ( const char *, int, const char * ( * ) ( int ) );
void print_warning ( int, const char * ( * ) ( int ) );
void populate_info_buffer ( const char * );
void print_version_info ( void );
void print_help_info ( const char * );
void list_repos ( struct repo_stack_t *, char * );

#define ERROR_MAX ( 256 )
extern char info_buffer [ ERROR_MAX ];

#endif /* _ERROR_H */


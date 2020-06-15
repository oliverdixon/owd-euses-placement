/* ash-euses: defines the information buffer and various reporting functions
 * Ashley Dixon. */

#ifndef _ERROR_H
#define _ERROR_H

#define ERROR_MAX ( 256 )

void print_fatal ( const char *, int, const char * ( * ) ( int ) );
void print_warning ( int, const char * ( * ) ( int ) );
void populate_info_buffer ( const char * );
void print_version_info ( void );
void print_help_info ( const char * );

extern char info_buffer [ ERROR_MAX ];

#endif /* _ERROR_H */


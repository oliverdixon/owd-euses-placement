/* ash-euses: defines the error buffer and various error-reporting functions
 * Ashley Dixon. */

#ifndef _ERROR_H
#define _ERROR_H

#define ERROR_MAX ( 256 )

void print_error ( const char *, int, const char * ( * get_detail ) ( int ) );
void populate_error_buffer ( const char * );

extern char error_buffer [ ERROR_MAX ];

#endif /* _ERROR_H */


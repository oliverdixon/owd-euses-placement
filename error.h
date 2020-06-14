/* ash-euses: defines the information buffer and various reporting functions
 * Ashley Dixon. */

#ifndef _ERROR_H
#define _ERROR_H

#define ERROR_MAX ( 256 )

enum error_severity_t {
        ERROR_FATAL = 0,
        ERROR_WARN  = 1
};

void print_info ( const char *, int, const char * ( * get_detail ) ( int ),
                enum error_severity_t );
void populate_info_buffer ( const char * );

extern char info_buffer [ ERROR_MAX ];

#endif /* _ERROR_H */


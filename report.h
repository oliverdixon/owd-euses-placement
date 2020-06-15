/* ash-euses: defines the information buffer and various reporting functions
 * Ashley Dixon. */

#ifndef _ERROR_H
#define _ERROR_H

#define ERROR_MAX ( 256 )

enum status_t {
        STATUS_ERRNO  =  1, /* c.f. perror or strerror on errno */
        STATUS_OK     =  0, /* everything is OK */
        STATUS_NOREPO = -1, /* no repository-description files were found */
        STATUS_NOGENR = -2, /* no gentoo.conf repository-description file */
        STATUS_ININME = -3, /* the ini file did not contain "[name]" */
        STATUS_INILOC = -4, /* the location attribute doesn't exist */
        STATUS_INILCS = -5, /* the location value exceeded PATH_MAX - 1 */ 
        STATUS_INIEMP = -6  /* the repository-description file was empty */ 
};

void print_fatal ( const char *, int, const char * ( * get_detail ) ( int ) );
void print_warning ( const char * );
void populate_info_buffer ( const char * );

extern char info_buffer [ ERROR_MAX ];

#endif /* _ERROR_H */


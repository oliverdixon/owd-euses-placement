/* ash-euses: integrated buffer testing; freestanding from ash-euses
 * Ashley Dixon. */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define BUFFER_SZ ( 4096 )

#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"

enum buffer_status_t {
        BUFSTAT_DUMMY =  3, /* no-op; will never be returned */
        BUFSTAT_BORDR =  2, /* borderline case: buffer full, file read */
        BUFSTAT_MORE  =  1, /* the file has been buffered; room for more */
        BUFSTAT_FULL  =  0, /* part of the file has been buffered; it is full */
        BUFSTAT_ERRNO = -1  /* an error occurred in fread/fopen; c.f. errno */
};

/* fnull: close and null a file pointer. */

static inline void fnull ( FILE ** fp )
{
        fclose ( *fp );
        *fp = NULL;
}

/* feof_stream: feof alternative, removing the reliance on file-handling
 * functions, such as fread, to set the end-of-file indicator. Unfortunately,
 * when reading a file of size 4096 into a buffer of identical capacity, fread
 * only flags EOF upon attempting to read the 4097th character; thus, feof
 * cannot be relied upon. This function returns -1 on error, zero if the file
 * has not reached the end, and 1 if the inverse is true. In any case, the file
 * cursor is reverted to its previous state. */

int feof_stream ( FILE * fp )
{
        long pos = 0, len = 0;

        if ( ( pos = ftell ( fp ) ) == -1 || fseek ( fp, 0, SEEK_END ) == -1 )
                return -1;

        if ( ( len = ftell ( fp ) ) == -1 ) {
                fseek ( fp, pos, SEEK_SET );
                return -1;
        }

        if ( fseek ( fp, pos, SEEK_SET ) == -1 )
                return -1;

        return ( pos == len );
}

/* idx must be persistent across calls if the buffer has not been filled
 * TODO: can this function be split, perhaps ? */

/* potentialities:
 *      BUFSTAT_MORE: the buffer has room for more, and the current file has
 *              been exhausted.
 *      BUFSTAT_BORDR: the buffer is full, but will require a new path for the
 *              next call.
 *      BUFSTAT_ERRNO: the buffer has room for more, because the fread
 *              operation failed (errno set appropriately).
 *      BUFSTAT_FULL: the buffer is full, and is ready for searching. */

enum buffer_status_t populate_buffer ( char * path, char buffer [ BUFFER_SZ ],
                FILE ** fp, size_t * idx )
{
        size_t bw = 0;

        /* ensure the buffer is null-terminated */
        buffer [ BUFFER_SZ - 1 ] = '\0';

        if ( *fp == NULL && ( *fp = fopen ( path, "r" ) ) == NULL )
                return BUFSTAT_ERRNO; /* the file cannot be opened */

        if ( ( bw = fread ( & ( buffer [ *idx ] ), sizeof ( char ),
                                        BUFFER_SZ - 1 - *idx, *fp ) )
                        < BUFFER_SZ - 1 ) {
                *idx += bw;

                if ( *idx == BUFFER_SZ - 1 ) {
                        *idx = 0;
                        return BUFSTAT_FULL;
                }

                buffer [ *idx ] = '\0';
                if ( feof_stream ( *fp ) == 1 ) {
                        /* the buffer has not been filled because the
                         * file has no more bytes */
                        fnull ( fp );
                        return BUFSTAT_MORE;
                }

                /* the buffer has not been filled because there was an error
                 * with fread, the details of which were written to errno */
                fnull ( fp );
                return BUFSTAT_ERRNO;
        }

        if ( bw == BUFFER_SZ - 1 ) {
                if ( feof_stream ( *fp ) == 1 ) {
                        /* borderline case: the buffer has been filled, and the
                         * file has ended */
                        *idx = 0;
                        fnull ( fp );
                        return BUFSTAT_BORDR;
                }

                /* the buffer has been filled, and there is still more in the
                 * current file */
                *idx = 0;
                return BUFSTAT_FULL;
        }

        return BUFSTAT_DUMMY; /* appease compilers */
}

static inline void print_buffer ( char buffer [ BUFFER_SZ ] )
{
        fputs ( buffer, stdout );
}

int main ( int argc, char ** argv )
{
        char * path = NULL, buffer [ BUFFER_SZ ];
        enum buffer_status_t status = BUFSTAT_DUMMY;
        FILE * fp = NULL;
        size_t idx = 0;

        if ( argc < 2 ) {
                fputs ( "not enough arguments\n", stderr );
                return EXIT_FAILURE;
        }

        path = argv [ 1 ];

        for ( int i = 1; i < argc; ) {
                status = populate_buffer ( path, buffer, &fp, &idx );

                if ( status == BUFSTAT_ERRNO ) {
                        fputs ( strerror ( errno ), stderr );
                        return EXIT_FAILURE;
                }

                if ( status == BUFSTAT_BORDR ) {
                        /* if borderline, do the same as BUFSTAT_FULL but queue
                         * up a new file to pass for the next call to
                         * populate_buffer */
                        print_buffer ( buffer );
                        puts ( "\n" KRED "HIT BUFSTAT_BORDR; SEARCH" KNRM );
                        if ( i + 1 <= argc ) {
                                i++;
                                path = argv [ i ];
                        }
                        continue;
                }

                if ( status == BUFSTAT_MORE ) {
                        print_buffer ( buffer );
                        puts ( "\n" KRED "HIT BUFSTAT_MORE" KNRM );
                        if ( i + 1 <= argc ) {
                                i++;
                                path = argv [ i ];
                        }
                        continue;
                }

                if ( status == BUFSTAT_FULL ) {
                        print_buffer ( buffer );
                        puts ( "\n" KRED "HIT BUFSTAT_FULL; SEARCH" KNRM );
                }
        }

        puts ( "\n" KRED "NO MORE FILES; SEARCH" KNRM );

        /* ONLY SEARCH ON BUFSTAT_FULL, BUFSTAT_BORDR OR NO MORE FILES TO
         * PROCESS IN THE HYPOTHETICAL DIRECTORY. */
        return EXIT_SUCCESS;
}


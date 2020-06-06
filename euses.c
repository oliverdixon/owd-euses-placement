/* ash-euses: main driver
 * Ashley Dixon. */

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#include "euses.h"

#define BUFFER_SZ ( 4096 )

#define ASCII_MIN ( 0x20 )
#define ASCII_MAX ( 0x7E )

#define CONFIGROOT_ENVNAME "PORTAGE_CONFIGROOT"
#define CONFIGROOT_SUFFIX  "/repos.conf/"
#define CONFIGROOT_DEFAULT "/etc/portage"
#define PROFILES_SUFFIX "/profiles/"

struct desc_t {
        char location [ PATH_MAX ];
        struct repo_t parent;
};

enum status_t {
        STATUS_OK     =  0, /* everything is OK */
        STATUS_ERRNO  = -1, /* c.f. perror or strerror on errno */
        STATUS_NOREPO = -2, /* no repository-description files were found */
        STATUS_NOGENR = -3, /* no gentoo.conf repository-description file */
        STATUS_DCLONG = -4, /* the description file is unusually long */
        STATUS_ININME = -5, /* the ini file did not contain "[name]" */
        STATUS_ININML = -6, /* the name in the ini file exceeded NAME_MAX */
        STATUS_INILOC = -7, /* the location attribute doesn't exist */
        STATUS_INILCS = -8, /* the location value exceeded PATH_MAX - 1 */ 
        STATUS_BADARG = -9  /* inadequate command-line arguments */ 
};

enum dir_status_t {
        DIRSTAT_DONE  =  1, /* no more files in the stream */
        DIRSTAT_MORE  =  0, /* there may be more files in the stream */
        DIRSTAT_ERRNO = -1  /* an error occurred; c.f. errno */
};

enum buffer_status_t {
        BUFSTAT_DUMMY =  3, /* no-op; will never be returned */
        BUFSTAT_BORDR =  2, /* the buffer is full and the file is fully read */
        BUFSTAT_MORE  =  1, /* the file has been buffered; room for more */
        BUFSTAT_FULL  =  0, /* part of the file has been buffered; it is full */
        BUFSTAT_ERRNO = -1  /* an error occurred in fread/fopen; c.f. errno */
};

/* provide_error: returns a human-readable string representing an error code, as
 * enumerated in status_t. If the passed code is STATUS_ERRNO, the strerror
 * function is used with the current value of errno. */

const char * provide_error ( enum status_t status )
{
        switch ( status ) {
                case STATUS_OK:     return "Everything is OK.";
                case STATUS_ERRNO:  return strerror ( errno );
                case STATUS_NOREPO: return "No repository-description files " \
                                        "were found.";
                case STATUS_NOGENR: return "gentoo.conf does not exist.";
                case STATUS_DCLONG: return "A repository-description file " \
                                        "is unusually voluminous.";
                case STATUS_ININME: return "A repository-description does " \
                                        "not contain a [name] clause at the" \
                                        " first opportunity.";
                case STATUS_ININML: return "A repository-description file " \
                                        "contains an unwieldy section name.";
                case STATUS_INILOC: return "A repository-description file " \
                                        "does not contain the location " \
                                        "attribute.";
                case STATUS_INILCS: return "A repository-description file" \
                                        "contains an unwieldy location value.";
                case STATUS_BADARG: return "Inadequate command-line arguments" \
                                        " were provided.";

                default: return "Unknown error";
        }
}

/* fnull: close and null a file pointer. */

static inline void fnull ( FILE ** fp )
{
        fclose ( *fp );
        *fp = NULL;
}

/* dnull: close and null a directory stream pointer. */

static inline void dnull ( DIR ** dp )
{
        closedir ( *dp );
        *dp = NULL;
}

/* get_base_dir: populates `base` with the Portage configuration root (usually
 * CONFIGROOT_DEFAULT) prepended to the CONFIGROOT_SUFFIX. On returning zero,
 * `base` contains the base directory of the various repository-description
 * files. If -1 is returned, the base path would exceed the limit defined by
 * PATH_MAX; errno is set appropriately. */

enum status_t get_base_dir ( char base [ PATH_MAX ] )
{
        char * base_ptr = NULL;

        strcpy ( base, ( base_ptr = getenv ( CONFIGROOT_ENVNAME ) ) != NULL ?
                        base_ptr : CONFIGROOT_DEFAULT );

        if ( strlen ( base ) + strlen ( CONFIGROOT_SUFFIX ) >= PATH_MAX ) {
                errno = ENAMETOOLONG;
                return STATUS_ERRNO;
        }

        strcat ( base, CONFIGROOT_SUFFIX );
        return STATUS_OK;
}

/* ini_get_name: retrieve the repository name from the description file; this is
 * the first non-"DEFAULT" section title, enclosed by '[' and ']'. On success,
 * STATUS_OK is returned, and `name` contains the section name, and on failure,
 * STATUS_ININME or STATUS_ININML is returned, the latter occurring if the given
 * name exceeds NAME_MAX. The `offset` value is set to the address of the
 * character in the buffer immediately succeeding the closing ']'. */

enum status_t ini_get_name ( char name [ NAME_MAX ], char buffer [ BUFFER_SZ ],
                int * offset )
{
        char * start = NULL, * end = NULL, * buffer_in = buffer;

        do {
                start = NULL;
                end = NULL;

                if ( ( start = strchr ( buffer, '[' ) ) == NULL
                                || ( end = strchr ( buffer, ']' ) ) == NULL ||
                                * ( start++ ) == '\0' || * ( end + 1 ) == '\0'
                                || end < start )
                        return STATUS_ININME;

                start [ end - start ] = '\0';
                buffer = end + 1;
        } while ( strcmp ( start, "DEFAULT" ) == 0 );

        if ( strlen ( start ) > NAME_MAX )
                return STATUS_ININML;

        *offset = ( end + 1 ) - buffer_in;
        strcpy ( name, start );
        return STATUS_OK;
}

/* skip_whitespace: skips horizontal spacing in `str` and returns the position
 * of the next non-whitespace character. If a null-terminator appears before a
 * non-whitespace character is found, NULL is returned. */

char * skip_whitespace ( char * str )
{
        for ( unsigned int i = 0; ; i++ )
                switch ( str [ i ] ) {
                        case 0x20:
                        case 0x09:
                                continue;
                        case 0x00:
                                /* assumes the string is null-terminated */
                                return NULL;
                        default:
                                return & ( str [ i ] );
                }
}

/* ini_get_location: gets the value of the `location` key in the ini file
 * provided in `buffer`, ignoring all horizontal whitespace; see the
 * skip_whitespace function. On success, this function returns STATUS_OK and
 * writes the appropriate value into the `location` string. On failure,
 * STATUS_INILOC is returned. */

enum status_t ini_get_location ( char location [ PATH_MAX ],
                char buffer [ BUFFER_SZ ] )
{
        const char * key = "location";
        char * start = strstr ( buffer, key ), * end = NULL;
        int keyvalue_found = 0;

        if ( start == NULL )
                return STATUS_INILOC;

        start += strlen ( key );
        if ( ( start = skip_whitespace ( start ) ) == NULL )
                return STATUS_INILOC;

        for ( int i = 0; !keyvalue_found && start [ i ] != '\0' &&
                        i < BUFFER_SZ; i++ ) {
                if ( start [ i ] == '=' )
                        keyvalue_found = 1;

                break;
        }

        if ( !keyvalue_found || * ( start++ ) < ASCII_MIN ||
                        * ( start ) > ASCII_MAX )
                return STATUS_INILOC;

        if ( ( start = skip_whitespace ( start ) ) == NULL ||
                        ( end = strchr ( start, '\n' ) ) == NULL ||
                        end < start )
                return STATUS_INILOC;

        start [ end - start ] = '\0';
        if ( strlen ( start ) >= PATH_MAX )
                return STATUS_INILCS;

        strcpy ( location, start );
        return STATUS_OK;
}

/* buffer_repo_description: employing length-checks, load, buffer, and close,
 * the given repository-description file, allowing the driver to disregard the
 * file pointer. On success, STATUS_OK is returned, and on failure, either
 * STATUS_ERRNO (with errno set appropriately), or STATUS_DCLONG is returned. */

enum status_t buffer_repo_description ( char path [ ],
                char buffer [ BUFFER_SZ ] )
{
        FILE * fp = fopen ( path, "r" );
        long f_len = 0;
        size_t bytes_read = 0;

        if ( fp == NULL )
                return STATUS_ERRNO;

        if ( fseek ( fp, 0, SEEK_END ) == -1 ||
                        ( f_len = ftell ( fp ) ) == -1 ) {
                fclose ( fp );
                return STATUS_ERRNO;
        }

        rewind ( fp );
        if ( f_len >= BUFFER_SZ ) {
                fclose ( fp );
                return STATUS_DCLONG;
        }

        if ( ( bytes_read = fread ( buffer, sizeof ( char ), BUFFER_SZ - 1,
                                        fp ) ) < BUFFER_SZ - 1
                        && !feof ( fp ) ) {
                fclose ( fp );
                return STATUS_ERRNO;
        }

        fclose ( fp );
        buffer [ bytes_read ] = '\0';
        return STATUS_OK;
}

/* parse_repo_description: parse the repository-description file, attaining the
 * base location and the name, populating the given struct accordingly. If the
 * repository-description file exceeds the generous amount as provided by the
 * buffer, STATUS_DCLONG is returned. */

enum status_t parse_repo_description ( struct repo_t * repo,
                char desc_path [ ] )
{
        char buffer [ BUFFER_SZ ];
        enum status_t status = STATUS_OK;
        int offset = 0;

        if ( ( status = buffer_repo_description ( desc_path, buffer ) )
                        != STATUS_OK || ( status =
                                ini_get_name ( repo->name, buffer, &offset ) )
                        != STATUS_OK || ( status = 
                                ini_get_location ( repo->location,
                                        & ( buffer [ offset ] ) ) )
                        != STATUS_OK )
                return status;

        return STATUS_OK;
}

/* register_repo: allocates memory for a repository, initialises it with empty
 * values its location on the filesystem, and adds it to the stack. If the
 * physical location exceeds the length allowed by PATH_MAX, or malloc cannot
 * allocate enough memory, errno is set appropriately and STATUS_ERRNO is
 * returned. On success, STATUS_OK is returned. */

enum status_t register_repo ( char base [ ], char * filename,
                struct repo_stack_t * stack )
{
        struct repo_t * repo = malloc ( sizeof ( struct repo_t ) );
        unsigned int len = 0;
        enum status_t status = STATUS_OK;
        char desc_path [ PATH_MAX ];

        if ( repo == NULL )
                return STATUS_ERRNO;

        if ( ( len = strlen ( filename ) + strlen ( base ) ) >= PATH_MAX ) {
                errno = ENAMETOOLONG;
                return STATUS_ERRNO;
        }

        strcpy ( desc_path, base );
        strcat ( desc_path, filename );
        desc_path [ len ] = '\0';

        if ( ( status = parse_repo_description ( repo, desc_path ) )
                        != STATUS_OK )
                return status;

        repo->next = NULL;
        stack_push ( stack, repo );

        return STATUS_OK;
}

/* enumerate_repo_descriptions: adds the regular files (repository configuration
 * files) contained within `base` to the given. On success, this function
 * returns STATUS_OK, otherwise STATUS_ERRNO, in which case errno is set
 * appropriately. The file `gentoo.conf` must exist within the base directory;
 * if this is not the case, STATUS_NOGENR is returned.
 *
 * https://wiki.gentoo.org/wiki//etc/portage/repos.conf#Format */

enum status_t enumerate_repo_descriptions ( char base [ ],
                struct repo_stack_t * stack )
{
        DIR * dp = NULL;
        struct dirent * dir = NULL;
        int gentoo_hit = 0; /* special case: base/gentoo.conf must exist */
        enum status_t status = STATUS_OK;

        if ( ( dp = opendir ( base ) ) == NULL )
                return STATUS_ERRNO;

        while ( ( dir = readdir ( dp ) ) != NULL )
                if ( dir->d_type == DT_REG ) {
                        if ( !gentoo_hit && strcmp ( "gentoo.conf",
                                                dir->d_name ) == 0 )
                                gentoo_hit = 1;

                        if ( ( status = register_repo ( base, dir->d_name,
                                                        stack ) )
                                        != STATUS_OK ) {
                                dnull ( &dp );
                                return status;
                        }
                }

        dnull ( &dp );
        return ( gentoo_hit ) ? STATUS_OK : STATUS_NOGENR;
}

/* get_file_ext: returns the file extension---from the final '.' to the
 * null-terminator---of the given string. If the string does not contain '.',
 * or it is immediately followed by a null-terminator, NULL is returned. */

char * get_file_ext ( const char * filename )
{
        char * ext = strrchr ( filename, '.' );
        return ( ext == NULL || * ( ext++ ) == '\0' ) ? NULL : ext;
}

/* find_next_desc_file: get the name of the next regular file with the "desc"
 * extension in the repo_base directory. If there is an error reading the
 * directory stream, DIRSTAT_ERRNO is returned, and the caller can refer to
 * `errno`. DIRSTAT_DONE indicates that the current directory stream has been
 * exhausted for regular files, and this function should not be called again
 * until `dp` points to a new directory pointer supplied by
 * construct_profiles_path (opendir). If DIRSTAT_MORE is returned, this function
 * can be called again with an externally unmodified value of `dp` to reveal a
 * new regular file in the directory. If the name of the file cannot be
 * concatenated to the repository base path due to length, errno is set
 * appropriately and DIRSTAT_ERRNO is returned. */

enum dir_status_t find_next_desc_file ( char repo_base [ PATH_MAX ], DIR ** dp )
{
        struct dirent * dir = NULL;
        char * ext = NULL;
        errno = 0;

        do {
                if ( ( dir = readdir ( *dp ) ) == NULL ) {
                        if ( errno != 0 )
                                /* On reaching the end of the stream,
                                 * errno is not changed. */
                                return DIRSTAT_ERRNO;
                        return DIRSTAT_DONE;
                }

                ext = get_file_ext ( dir->d_name );
        } while ( dir->d_type != DT_REG || ( !ext ||
                                strcmp ( ext, "desc" ) != 0 ) );

        if ( strlen ( repo_base ) + strlen ( dir->d_name ) >= PATH_MAX ) {
                errno = ENAMETOOLONG;
                return DIRSTAT_ERRNO;
        }

        /* This is safe; the previously appended suffix should end in a oblique
         * - see construct_profiles_path. */
        strcat ( repo_base, dir->d_name );
        return DIRSTAT_MORE;
}

/* construct_profiles_path: concatenate the base repository path and profile
 * suffix, and attempt to open the resultant directory. If this function returns
 * zero, the caller must clean-up the opendir call, otherwise, -1 on failure.
 * This only needs to be called once-per-repository. */

int construct_profiles_path ( char path [ PATH_MAX ], DIR ** dp )
{
        if ( strlen ( PROFILES_SUFFIX ) + strlen ( path ) >= PATH_MAX ) {
                errno = ENAMETOOLONG;
                return -1;
        }

        strcat ( path, PROFILES_SUFFIX );
        return ( ( *dp = opendir ( path ) ) == NULL ) ? -1 : 0;
}

/* buffer_desc_file: buffer the USE-description file, specified by `path`. The
 * caller should not modify the value of `fp` itself - just keep it in a
 * persistent state. This function should be called recursively until
 * BUFSTAT_ERRNO or BUFSTAT_FULL is returned, modifying the path to the next
 * contender returned by find_next_desc_file when BUFSTAT_MORE is returned. */

enum buffer_status_t buffer_desc_file ( char buffer [ BUFFER_SZ ], FILE ** fp,
                char path [ PATH_MAX ], size_t * buffer_idx )
{
        size_t bytes_written = 0;

        if ( *fp == NULL && ( *fp = fopen ( path, "r" ) ) == NULL )
                return BUFSTAT_ERRNO;

        bytes_written = fread ( & ( buffer [ *buffer_idx ] ),
                                sizeof ( char ), BUFFER_SZ - 1, *fp );

        if ( bytes_written <= BUFFER_SZ ) {
                if ( feof ( * fp ) ) {
                        *buffer_idx += bytes_written - 1;
                        buffer [ *buffer_idx ] = '\0';
                        fnull ( fp );
                        return BUFSTAT_MORE;
                }

                fnull ( fp );
                return BUFSTAT_ERRNO;
        }

        if ( bytes_written == BUFFER_SZ - 1 ) {
                *buffer_idx += BUFFER_SZ - 1;
                buffer [ *buffer_idx ] = '\0';

                if ( feof ( *fp ) ) {
                        /* borderline case: file fills the buffer perfectly */
                        fnull ( fp );
                        return BUFSTAT_BORDR;
                }

                return BUFSTAT_FULL;
        }

        return BUFSTAT_DUMMY; /* appease compilers; this will never occur */
}

/* aggregate_buffer: fill a buffer or exhaust a directory stream by
 * concatenating the appropriate files in the given repo->location base path.
 * *dp maintains a pointer to the currently open directory stream. If this
 * function returns zero, the buffer is full and ready for searching. If it
 * returns -1, an error has occurred, and 1 indicates that the directory stream
 * has been exhausted of description files.
 *
 * TODO: comprehensive error-checking for boundary and excessive cases */

int aggregate_buffer ( char buffer [ BUFFER_SZ ], struct repo_t * repo,
                DIR ** dp )
{
                          /* *dp holds the current directory stream */
        FILE * fp = NULL; /* holds the current file being buffered */
        unsigned int repo_base_len = 0;
        enum buffer_status_t bufstat = BUFSTAT_DUMMY;
        enum dir_status_t dirstat = DIRSTAT_DONE;
        size_t buffer_idx = 0;

        repo_base_len = strlen ( repo->location );

        while ( bufstat != BUFSTAT_ERRNO ) {
                if ( bufstat == BUFSTAT_BORDR || bufstat == BUFSTAT_FULL ) {
                        return 0; /* the buffer is full; ready for searching */
                }

                if ( ( dirstat = find_next_desc_file ( repo->location, dp ) )
                                == DIRSTAT_DONE ) {
                        dnull ( dp );
                        return 1; /* description files exhausted */
                }

                bufstat = buffer_desc_file ( buffer, &fp, repo->location,
                                &buffer_idx );

                puts ( repo->location );
                /* truncate to original length
                 * (pre-find_next_desc_file) */
                repo->location [ repo_base_len ] = '\0';
        }

        dnull ( dp );
        repo->location [ repo_base_len ] = '\0';
        return -1; /* error */
}

/* search_files: search the *.{,local.}desc files in the repo `location`
 * directory to find any of the given needles. Once a repository's files have
 * completely been scanned, it is popped from the stack. */

enum status_t search_files ( struct repo_stack_t * stack, char ** needles )
{
        struct repo_t * repo = NULL;
        char buffer [ BUFFER_SZ ];
        DIR * dp = NULL;

        while ( ( repo = stack_pop ( stack ) ) != NULL ) {
                if ( construct_profiles_path ( repo->location, &dp ) == -1 )
                        return STATUS_ERRNO;

                if ( aggregate_buffer ( buffer, repo, &dp ) == 1 ) {
                        puts ( "buffer done" );
                        puts ( buffer );
                }

                free ( repo );
                if ( dp != NULL )
                        dnull ( &dp );
        } 

        return STATUS_OK;
}

int main ( int argc, char ** argv )
{
        char base [ PATH_MAX ];
        struct repo_stack_t repo_stack;
        enum status_t status = STATUS_OK;

        if ( argc < 2 ) {
                fputs ( provide_error ( STATUS_BADARG ), stderr );
                fputc ( '\n', stderr );
                return EXIT_FAILURE;
        }

        stack_init ( &repo_stack );

        if ( ( status = get_base_dir ( base ) != STATUS_OK ) ||
                        ( status = enumerate_repo_descriptions ( base,
                                &repo_stack ) ) != STATUS_OK ) {
                fputs ( "Could not use the repository-description " \
                                "base directory:\n\t", stderr );
                fputs ( provide_error ( status ), stderr );
                fputc ( '\n', stderr );
                stack_cleanse ( &repo_stack );
                return EXIT_FAILURE;
        }

        stack_print ( &repo_stack );
        putchar ( '\n' );

        /* TODO: split main to allow direct-printing to stderr, possibly with an
         * optional prefix. */
        if ( ( status = search_files ( &repo_stack, & ( argv [ 1 ] ) ) )
                        != STATUS_OK ) {
                fputs ( "Could not load the USE-description files:\n\t",
                                stderr );
                fputs ( provide_error ( status ), stderr );
                fputc ( '\n', stderr );
                stack_cleanse ( &repo_stack );
                return EXIT_FAILURE;
        }

        stack_cleanse ( &repo_stack );
        return EXIT_SUCCESS;
}


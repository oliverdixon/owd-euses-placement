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

#define NOPUTS(str) puts ( str )
#define KNRM "\x1B[0m"
#define KRED "\x1B[31m"

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
        STATUS_ININME = -4, /* the ini file did not contain "[name]" */
        STATUS_INILOC = -5, /* the location attribute doesn't exist */
        STATUS_INILCS = -6, /* the location value exceeded PATH_MAX - 1 */ 
        STATUS_INIEMP = -7  /* the repository-description file was empty */ 
};

enum dir_status_t {
        DIRSTAT_DONE  =  1, /* no more files in the stream */
        DIRSTAT_MORE  =  0, /* there may be more files in the stream */
        DIRSTAT_ERRNO = -1  /* an error occurred; c.f. errno */
};

enum buffer_status_t {
        BUFSTAT_BORDR =  2, /* the buffer is full and the file is fully read */
        BUFSTAT_MORE  =  1, /* the file has been buffered; room for more */
        BUFSTAT_FULL  =  0, /* part of the file has been buffered; it is full */
        BUFSTAT_ERRNO = -1  /* an error occurred in fread/fopen; c.f. errno */
};

/* buffer_info_t should be kept persistent by the caller, for use by functions
 * directly modifying the trans-directory and trans-file buffer. This is a
 * spurious attempt to eliminate the need for static variables inside the
 * buffered reader functions, thus ensuring thread-safety in all cases, should
 * such a need arise. Multiple instances of a buffer-directory set can also be
 * maintained simultaneously, regardless of the threading style. We don't need
 * another strtok(_r) situation. */

struct buffer_info_t {
        FILE * fp; /* the file currently being read */
        size_t idx; /* the index into the current buffer; DO NOT TOUCH */
        enum buffer_status_t status; /* for the caller: status of the reader */
        char buffer [ BUFFER_SZ ]; /* buffer, assumed to be of size BUFFER_SZ */
};

/* provide_error: returns a human-readable string representing an error code, as
 * enumerated in status_t. If the passed code is STATUS_ERRNO, the strerror
 * function is used with the current value of errno. */

static const char * provide_error ( enum status_t status )
{
        switch ( status ) {
                case STATUS_OK:     return "Everything is OK.";
                case STATUS_ERRNO:  return strerror ( errno );
                case STATUS_NOREPO: return "No repository-description files " \
                                        "were found.";
                case STATUS_NOGENR: return "gentoo.conf does not exist.";
                case STATUS_ININME: return "A repository-description does " \
                                        "not contain a [name] clause at the" \
                                        " first opportunity.";
                case STATUS_INILOC: return "A repository-description file " \
                                        "does not contain the location " \
                                        "attribute.";
                case STATUS_INILCS: return "A repository-description file" \
                                        "contains an unwieldy location value.";

                default: return "Unknown error";
        }
}

/* fnull: close and null a non-null file pointer. */

static inline void fnull ( FILE ** fp )
{
        if ( *fp != NULL ) {
                fclose ( *fp );
                *fp = NULL;
        }
}

/* dnull: close and null a non-null directory stream pointer. */

static inline void dnull ( DIR ** dp )
{
        if ( *dp != NULL ) {
                closedir ( *dp );
                *dp = NULL;
        }
}

/* get_base_dir: populates `base` with the Portage configuration root (usually
 * CONFIGROOT_DEFAULT) prepended to the CONFIGROOT_SUFFIX. On returning zero,
 * `base` contains the base directory of the various repository-description
 * files. If -1 is returned, the base path would exceed the limit defined by
 * PATH_MAX; errno is set appropriately. */

static enum status_t get_base_dir ( char base [ PATH_MAX ] )
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

static enum status_t ini_get_name ( char name [ NAME_MAX ],
                char buffer [ BUFFER_SZ ], int * offset )
{
        char * start = NULL, * end = NULL, * buffer_in = buffer;

        do {
                start = NULL;
                end = NULL;

                if ( ( start = strchr ( buffer, '[' ) ) == NULL
                                || ( end = strchr ( buffer, ']' ) ) == NULL ||
                                * ( start++ ) == '\0' || * ( end + 1 ) == '\0'
                                || end < start ) {
                        return STATUS_ININME;
                }

                start [ end - start ] = '\0';
                buffer = end + 1;
        } while ( strcmp ( start, "DEFAULT" ) == 0 );

        if ( strlen ( start ) > NAME_MAX ) {
                errno = ENAMETOOLONG;
                return STATUS_ERRNO;
        }

        *offset = ( end + 1 ) - buffer_in;
        strcpy ( name, start );
        return STATUS_OK;
}

/* skip_whitespace: skips horizontal spacing in `str` and returns the position
 * of the next non-whitespace character. If a null-terminator appears before a
 * non-whitespace character is found, NULL is returned. */

static char * skip_whitespace ( char * str )
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

static enum status_t ini_get_location ( char location [ PATH_MAX ],
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
 * STATUS_ERRNO (with errno set appropriately), or STATUS_DCLONG is returned. If
 * the file is empty, STATUS_INIEMP is returned. */

static enum status_t buffer_repo_description ( char path [ ],
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
                /* the repository-description file would not fit the buffer */
                fclose ( fp );
                errno = EFBIG;
                return STATUS_ERRNO;
        }

        if ( f_len == 0 ) {
                /* the file is empty, and the INI-parser would fail */
                fclose ( fp );
                return STATUS_INIEMP;
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

static enum status_t parse_repo_description ( struct repo_t * repo,
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

static enum status_t register_repo ( char base [ ], char * filename,
                struct repo_stack_t * stack )
{
        struct repo_t * repo = malloc ( sizeof ( struct repo_t ) );
        unsigned int len = 0;
        enum status_t status = STATUS_OK;
        char desc_path [ PATH_MAX ];

        if ( repo == NULL )
                return STATUS_ERRNO;

        if ( ( len = strlen ( filename ) + strlen ( base ) ) >= PATH_MAX ) {
                free ( repo );
                errno = ENAMETOOLONG;
                return STATUS_ERRNO;
        }

        strcpy ( desc_path, base );
        strcat ( desc_path, filename );
        desc_path [ len ] = '\0';

        if ( ( status = parse_repo_description ( repo, desc_path ) )
                        != STATUS_OK ) {
                free ( repo );
                return status;
        }

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

static enum status_t enumerate_repo_descriptions ( char base [ ],
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
                                if ( status == STATUS_INIEMP )
                                        continue; /* ignore empty files */

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

static char * get_file_ext ( const char * filename )
{
        char * ext = strrchr ( filename, '.' );
        return ( ext == NULL || * ( ext++ ) == '\0' ) ? NULL : ext;
}

/* find_next_desc_file: get the name of the next regular file with the "desc"
 * extension in the repo_base directory. If there is an error reading the
 * directory stream, DIRSTAT_ERRNO is returned, and the caller can refer to
 * `errno`. DIRSTAT_DONE indicates that the current directory stream has been
 * exhausted for regular files, and this function should not be called again
 * until `dp` points to a new directory pointer supplied by open_profiles_path
 * (opendir). If DIRSTAT_MORE is returned, this function can be called again
 * with an externally unmodified value of `dp` to reveal a new regular file in
 * the directory. If the name of the file cannot be concatenated to the
 * repository base path due to length, errno is set appropriately and
 * DIRSTAT_ERRNO is returned. */

static enum dir_status_t find_next_desc_file ( char repo_base [ PATH_MAX ],
                DIR ** dp )
{
        struct dirent * dir = NULL;
        char * ext = NULL;
        errno = 0;

        do {
                if ( ( dir = readdir ( *dp ) ) == NULL ) {
                        if ( errno != 0 )
                                /* On reaching the end of the stream,
                                 * errno has changed to indicate an error. */
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
         * - see open_profiles_path. */
        strcat ( repo_base, dir->d_name );
        return DIRSTAT_MORE;
}

/* open_profiles_path: concatenate the base repository path and profile
 * suffix, and attempt to open the resultant directory. If this function returns
 * zero, the caller must clean-up the opendir call, otherwise, -1 on failure.
 * This only needs to be called once-per-repository. */

static int open_profiles_path ( char path [ PATH_MAX ], DIR ** dp )
{
        if ( strlen ( PROFILES_SUFFIX ) + strlen ( path ) >= PATH_MAX ) {
                errno = ENAMETOOLONG;
                return -1;
        }

        strcat ( path, PROFILES_SUFFIX );
        return ( ( *dp = opendir ( path ) ) == NULL ) ? -1 : 0;
}

/* feof_stream: feof alternative, removing the reliance on file-handling
 * functions, such as fread, to set the end-of-file indicator. Unfortunately,
 * when reading a file of size 4096 into a buffer of identical capacity, fread
 * only flags EOF upon attempting to read the 4097th character; thus, feof
 * cannot be relied upon. This function returns -1 on error, zero if the file
 * has not reached the end, and 1 if the inverse is true. In any case, the file
 * cursor is reverted to its previous state. */

static int feof_stream ( FILE * fp )
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

/* populate_buffer: assuming the buffer_info_t structure remains persistent and
 * unmodified by the caller, this function loads a file, provided by `path` into
 * the given buffer of BUFFER_SZ. If the buffer is filled, BUFSTAT_FULL or
 * BUFSTAT_BORDR is returned, dependent upon the position of the file cursor; if
 * the file has filled the buffer perfectly, BUFSTAT_BORDR (borderline case) is
 * returned. If the buffer has not been filled due to a lack of bytes in the
 * file, BUFSTAT_MORE is returned, and the next call to this function should
 * contain a new path. If the file cannot be opened or read, BUFSTAT_ERRNO is
 * returned, and the caller must confer with errno. */

static enum buffer_status_t populate_buffer ( char * path,
                struct buffer_info_t * b_inf )
{
        size_t bw = 0;

        /* ensure the buffer is null-terminated */
        b_inf->buffer [ BUFFER_SZ - 1 ] = '\0';

        if ( b_inf->fp == NULL && ( b_inf->fp = fopen ( path, "r" ) ) == NULL )
                return BUFSTAT_ERRNO; /* the file cannot be opened */

        if ( ( bw = fread ( & ( b_inf->buffer [ b_inf->idx ] ),
                                        sizeof ( char ),
                                        BUFFER_SZ - 1 - b_inf->idx,
                                        b_inf->fp ) )
                        < BUFFER_SZ - 1 ) {
                if ( ( b_inf->idx += bw ) == BUFFER_SZ - 1 ) {
                        b_inf->idx = 0;
                        return BUFSTAT_FULL;
                }

                b_inf->buffer [ b_inf->idx ] = '\0';
                if ( feof_stream ( b_inf->fp ) == 1 ) {
                        /* the buffer has not been filled because the
                         * file has no more bytes */
                        fnull ( & ( b_inf->fp ) );
                        return BUFSTAT_MORE;
                }

                /* the buffer has not been filled because there was an error
                 * with fread, the details of which were written to errno */
                fnull ( & ( b_inf->fp ) );
                return BUFSTAT_ERRNO;
        }

        if ( bw == BUFFER_SZ - 1 ) {
                b_inf->idx = 0;

                if ( feof_stream ( b_inf->fp ) == 1 ) {
                        /* borderline case: the buffer has been filled, and the
                         * file has ended */
                        fnull ( & ( b_inf->fp ) );
                        return BUFSTAT_BORDR;
                }

                /* the buffer has been filled, and there is still more in the
                 * current file */
                return BUFSTAT_FULL;
        }

        errno = ENOSYS; /* appease compilers; this should never occur */
        return BUFSTAT_ERRNO;
}

/* request_new_desc_file: small wrapper function for find_next_desc_file,
 * freeing the passed objects on failure. The caller should confer with errno on
 * the event that DIRSTAT_ERRNO is returned. On success, this function returns
 * DIRSTAT_FULL or DIRSTAT_MORE, dependent on the status of the directory stream
 * (exhausted ?). */

static enum dir_status_t request_new_desc_file ( struct repo_t * repo,
                DIR ** dp )
{
        enum dir_status_t dirstat = DIRSTAT_MORE;

        if ( ( dirstat = find_next_desc_file ( repo->location, dp ) )
                        == DIRSTAT_ERRNO ) {
                free ( repo );
                dnull ( dp );
                return DIRSTAT_ERRNO;
        }

        return dirstat;
}

/* init_buffer_instance: initialise a buffer_info_t structure with default
 * values. */

static void init_buffer_instance ( struct buffer_info_t * b_inf )
{
        b_inf->fp = NULL;
        b_inf->idx = 0;
        b_inf->status = BUFSTAT_MORE;
}

/* search_files: search the *.{,local.}desc files in the repo `location`
 * directory to find any of the given needles. Once a repository's files have
 * completely been scanned, it is popped from the stack.
 *
 * Can this function be split up ? I really dislike its ugliness currently. */

static enum status_t search_files ( struct repo_stack_t * stack,
                char ** needles )
{
        struct repo_t * repo = NULL;
        enum dir_status_t dirstat = -1;
        unsigned int repo_base_len = 0;
        DIR * dp = NULL;
        struct buffer_info_t bi;

        init_buffer_instance ( &bi );

        while ( ( repo = stack_pop ( stack ) ) != NULL ) {
                if ( open_profiles_path ( repo->location, &dp ) == -1 ) {
                        free ( repo );
                        dnull ( &dp );
                        return STATUS_ERRNO;
                }

                repo_base_len = strlen ( repo->location );

                for ( ; ; ) {
                        if ( bi.status == BUFSTAT_BORDR
                                        || bi.status == BUFSTAT_MORE ) {
                                repo->location [ repo_base_len ] = '\0';
                                if ( ( dirstat = request_new_desc_file ( repo,
                                                                &dp ) )
                                                == DIRSTAT_ERRNO )
                                        return STATUS_ERRNO;
                                else if ( dirstat == DIRSTAT_DONE )
                                        break; /* no more files in this repo */

                                NOPUTS ( "\n" KRED "NEW FILE: " );
                                NOPUTS ( repo->location );
                                NOPUTS ( KNRM );
                        }

                        switch ( bi.status = populate_buffer ( repo->location,
                                                &bi ) ) {
                                case BUFSTAT_ERRNO:
                                        fputs ( strerror ( errno ), stderr );
                                        free ( repo );
                                        dnull ( &dp );
                                        return STATUS_ERRNO;
                                case BUFSTAT_BORDR:
                                case BUFSTAT_MORE:
                                        NOPUTS ( bi.buffer );
                                        NOPUTS ( "\n" KRED "HIT BUFSTAT_MORE"
                                                        KNRM );
                                        break;
                                case BUFSTAT_FULL:
                                        NOPUTS ( bi.buffer );
                                        NOPUTS ( "\n" KRED "HIT " \
                                                        "BUFSTAT_FULL; SEARCH"
                                                        KNRM );
                                        break;
                        }
                }

                free ( repo );
                dnull ( &dp );
        }

        NOPUTS ( "\n" KRED "NO MORE FILES; SEARCH (IF NOT BUFSTAT_BORDR)"
                        KNRM );
        return STATUS_OK;
}

int main ( )
{
        char base [ PATH_MAX ];
        struct repo_stack_t repo_stack;
        enum status_t status = STATUS_OK;

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

        /* TODO: split main to allow direct-printing to stderr, possibly with an
         * optional prefix. */
        if ( ( status = search_files ( &repo_stack, NULL ) ) != STATUS_OK ) {
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


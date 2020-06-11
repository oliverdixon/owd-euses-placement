/* ash-euses: main driver
 * Ashley Dixon. */

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <assert.h>

#include "euses.h"
#include "args.h"

#define BUFFER_SZ ( 4096 )
#define QUERY_MAX ( 256  )

#define ASCII_MIN ( 0x20 )
#define ASCII_MAX ( 0x7E )

#define CONFIGROOT_ENVNAME "PORTAGE_CONFIGROOT"
#define CONFIGROOT_SUFFIX  "/repos.conf/"
#define CONFIGROOT_DEFAULT "/etc/portage"
#define PORTAGE_MAKECONF   "/make.conf" /* to be appended to the config root */
#define PROFILES_SUFFIX    "/profiles/"
#define DEFAULT_REPO_NAME  "gentoo"

/* Globally accessible message buffer for better error-reporting; it should only
 * be written to using the populate_error_buffer function, as it provides
 * overflow-protection and pretty truncation. */
char error_buffer [ PATH_MAX ];

enum status_t {
        STATUS_OK     =  0, /* everything is OK */
        STATUS_ERRNO  = -1, /* c.f. perror or strerror on errno */
        STATUS_NOREPO = -2, /* no repository-description files were found */
        STATUS_NOGENR = -3, /* no gentoo.conf repository-description file */
        STATUS_ININME = -4, /* the ini file did not contain "[name]" */
        STATUS_INILOC = -5, /* the location attribute doesn't exist */
        STATUS_INILCS = -6, /* the location value exceeded PATH_MAX - 1 */ 
        STATUS_INIEMP = -7, /* the repository-description file was empty */ 
        STATUS_BADARG = -8  /* inadequate arguments were provided */
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
                case STATUS_INILOC: return "A description file does not " \
                                        "contain the location attribute.";
                case STATUS_INILCS: return "A repository-description file" \
                                        "contains an unwieldy location value.";
                case STATUS_BADARG: return "Inadequate command-line arguments" \
                                        " were provided. Try \"-h\".";

                default: return "Unknown error";
        }
}

/* print_error: format a `status` code, prefixed with `prefix`, and send it to
 * stderr. This function relies on the global error buffer, `error_buffer`. */

static inline void print_error ( const char * prefix, enum status_t status )
{
        fprintf ( stderr, PROGRAM_NAME " caught a fatal error and cannot " \
                        "continue.\nRe-run with \"--help\" or \"-h\" for help" \
                        ".\n\n\tSummary: \"%s\"\n\tOffending Article:" \
                        " \"%s\"\n\tError Detail: \"%s\"\n\tExit Code: %d\n",
                        prefix, ( error_buffer [ 0 ] != '\0' ) ? error_buffer :
                        "N/A", provide_error ( status ), EXIT_FAILURE );
}

/* populate_error_buffer: copy the `message` into the global `error_buffer`,
 * truncating with " [...]" if necessary. This function assumes that
 * error_buffer is of the size PATH_MAX. */

static void populate_error_buffer ( const char * message )
{
        size_t msg_len = 0;
        error_buffer [ 0 ] = '\0';

        strncpy ( error_buffer, message, PATH_MAX - 1 );

        if ( ( msg_len = strlen ( message ) ) >= PATH_MAX - 1 ) {
                /* indicate that the message in the error buffer has been
                 * truncated */
                error_buffer [ PATH_MAX - 7 ] = ' ';
                error_buffer [ PATH_MAX - 6 ] = '[';
                error_buffer [ PATH_MAX - 5 ] = '.';
                error_buffer [ PATH_MAX - 4 ] = '.';
                error_buffer [ PATH_MAX - 3 ] = '.';
                error_buffer [ PATH_MAX - 2 ] = ']';
                error_buffer [ PATH_MAX - 1 ] = '\0';
        }
}

/* print_version_info: uses the various PROGRAM_* definitions to print
 * program information to stdout. */

static inline void print_version_info ( )
{
        puts ( "This is " PROGRAM_NAME ", v. " PROGRAM_VERSION " by "
                        PROGRAM_AUTHOR " (" PROGRAM_YEAR ").\nFor support, " 
                        "send e-mail to " PROGRAM_AUTHOR " <"
                        PROGRAM_AUTHOR_EMAIL ">.\n\nThis code is licensed under"
                        " the " PROGRAM_LICENCE_NAME ",\nthe details of which "
                        "can be found at " PROGRAM_LICENCE_URL ".\n" );
}

/* print_help_info: pretty-print information regarding the possible command-
 * line arguments, with their abbreviated form and a brief description.
 *
 * Use this format-specifier for adding argument documentation:
 * "--%-13s -%-3s\t%s\n" with "<long name>", "<shortname>", "<description>". */

static inline void print_help_info ( const char * invocation )
{
        printf ( PROGRAM_NAME " command-line argument summary.\n" \
                        "Syntax: %s [options] substrings\n" \
                        "\n--%-13s -%-3s\t%s\n--%-13s -%-3s\t%s\n" \
                        "--%-13s -%-3s\t%s\n--%-13s -%-3s\t%s\n" \
                        "--%-13s -%-3s\t%s\n--%-13s -%-3s\t%s\n" \
                        "--%-13s -%-3s\t%s\n", invocation,
                        "list-repos", "r", "Prepend a list of located " \
                                "repositories (repos.conf/ only).",
                        "repo-names", "n", "Print repository names for " \
                                "each match.",
                        "repo-paths", "p", "Print repository names for " \
                                "each match (implies repo-names).",
                        "help", "h", "Print this help information and exit.",
                        "version", "v", "Prepend version and license " \
                                "information to the output.",
                        "strict", "s", "Search only in the flag field, " \
                                "excluding the description.",
                        "quiet", "q", "Do not complain about PORTDIR." );
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

        if ( strlen ( base ) + strlen ( CONFIGROOT_SUFFIX ) >= PATH_MAX - 1 ) {
                populate_error_buffer ( base );
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
                char buffer [ BUFFER_SZ ], const char * key )
{
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
                                        & ( buffer [ offset ] ), "location" ) )
                        != STATUS_OK ) {
                /* All these functions operate on the same desc_path. */
                populate_error_buffer ( desc_path );
                return status;
        }

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

        if ( repo == NULL ) {
                populate_error_buffer ( filename );
                return STATUS_ERRNO;
        }

        if ( ( len = strlen ( filename ) + strlen ( base ) ) >= PATH_MAX ) {
                populate_error_buffer ( filename );
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
 * if this is not the case, STATUS_NOGENR is returned. If ARG_LIST_REPOS was set
 * on the command-line, this function will pretty-print the repository stack and
 * base directory on success.
 *
 * https://wiki.gentoo.org/wiki//etc/portage/repos.conf#Format */

static enum status_t enumerate_repo_descriptions ( char base [ ],
                struct repo_stack_t * stack )
{
        DIR * dp = NULL;
        struct dirent * dir = NULL;
        int gentoo_hit = 0; /* special case: base/gentoo.conf must exist */
        enum status_t status = STATUS_OK;

        if ( ( dp = opendir ( base ) ) == NULL ) {
                populate_error_buffer ( base );
                return STATUS_ERRNO;
        }

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
        if ( gentoo_hit ) {
                /* print the repository stack, if appropriate */
                if ( CHK_ARG ( options, ARG_LIST_REPOS ) != 0 ) {
                        fputs ( "Configuration directory: ", stdout );
                        puts ( base );
                        putchar ( '\n' );
                        stack_print ( stack );
                        putchar ( '\n' );
                }

                return STATUS_OK;
        }

        return STATUS_NOGENR;
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

/* find_line_bounds: find the previous '\n', and the next '\n', and return an
 * accordingly null-terminated version of buffer_start, using substr_start as a
 * point of reference. `marker` is then set as the end of the current buffer, to
 * avoid getting stuck in an infinite loop (this should be reset by the relevant
 * caller(s) every time a new needle/search term is sought). This function
 * returns the start of the appropriately null-terminated line. If the entry is
 * poorly formatted, NULL is returned. */

static char * find_line_bounds ( char * buffer_start, char * substr_start,
                char ** marker )
{
        char tmp = '\0', * start = NULL, * end = NULL;
        long key_idx = substr_start - buffer_start;

        assert ( key_idx >= 0 );

        if ( key_idx != 0 ) {
                /* This not is the first entry in the buffer. */
                tmp = buffer_start [ key_idx ];
                buffer_start [ key_idx ] = '\0';

                if ( ( start = strrchr ( buffer_start, '\n' ) ) == NULL )
                        start = buffer_start;
                else if ( * ( start++ ) == '\0' ) {
                        /* the entry legitimate/poorly formatted */
                        buffer_start [ key_idx ] = tmp;
                        return NULL;
                }

                buffer_start [ key_idx ] = tmp;
        } else
                start = buffer_start;

        end = strchr ( substr_start, '\n' );
        *marker = end; /* marker is set to NULL if there's no closing newline */

        if ( end != NULL )
                /* A match appears at the end of one buffer, and continues in
                 * another, in which case the match can be classified by
                 * printers as "truncated". This might be a false-positive due
                 * to the file ending abruptly (no line feed/EOF), but that is
                 * incredibly rare and only causes a very slight output
                 * modification. */
                substr_start [ end - substr_start ] = '\0';

        return start;
}

/* print_search_result: print a search result, `result_str`, from the repo
 * `repo`, to stdout, respecting the ARG_PRINT_REPO_PATHS and
 * ARG_PRINT_REPO_NAMES command-line arguments. If `truncated` is set (the
 * result continues in another buffer), the acceptable workaround is to append
 * "[...]" to the output match, as the important information has already been
 * written (flags succeed package names). Perhaps this can be changed in the
 * future. */

static void print_search_result ( const char * result_str,
                struct repo_t * repo, int truncated )
{
        if ( CHK_ARG ( options, ARG_PRINT_REPO_PATHS ) != 0 )
                /* ARG_PRINT_REPO_PATHS implies ARG_PRINT_REPO_NAMES */
                printf ( "%s%s\n\t(::%s => %s)\n", result_str, ( truncated ) ?
                                " [...]" : "",repo->name,
                                repo->location );
        else if ( CHK_ARG ( options, ARG_PRINT_REPO_NAMES ) != 0 )
                printf ( "%s%s (::%s)\n", result_str, ( truncated ) ? " [...]"
                                : "", repo->name );
        else {
                fputs ( result_str, stdout );
                if ( truncated )
                        puts ( " [...]" );
                else
                        putchar ( '\n' );
        }
}

/* search_buffer: search the `buffer` for the provided `needles`, of which there
 * are `ncount`. This function searches and prints the results as soon as they
 * are found, and, providing uninterrupted execution, exits with `buffer`
 * unchanged. TODO: investigate the advantages of strstr(3) alternative
 * implementations, such as Boyer-Moore. */

static void search_buffer ( char buffer [ BUFFER_SZ ], char ** needles,
                int ncount, struct repo_t * repo )
{
        char * ptr = NULL, query [ QUERY_MAX ], * buffer_start = buffer,
                * substr = NULL;
        size_t len = 0; 

        for ( int i = 0; i < ncount; i++ ) {
                buffer = buffer_start;

                if ( ( len = strlen ( needles [ i ] ) + 3 ) >= QUERY_MAX ) {
                        fprintf ( stderr, "<%s is too long; skipping>\n",
                                        needles [ i ] );
                        continue;
                }

                if ( CHK_ARG ( options, ARG_SEARCH_STRICT ) != 0 ) {
                        strcpy ( query, needles [ i ] );
                        strcat ( query, " - " );
                        query [ len - 1 ] = '\0';
                        substr = query;
                } else
                        substr = needles [ i ];

                if ( ( ptr = strstr ( buffer, substr ) ) != NULL ) {
                        if ( ( ptr = find_line_bounds ( buffer, ptr, &buffer ) )
                                        == NULL )
                                break;

                        print_search_result ( ptr, repo, ( buffer == NULL ) );
                        if ( buffer == NULL )
                                break; /* end of buffer; see `marker` */

                        /* undo the terminator added by find_line_bounds */
                        ptr [ buffer - ptr ] = '\n';
                }
        }
}

/* search_files: search the profiles / *.desc files in the repo `location`
 * directory to find any of the given needles. Once a repository's files have
 * completely been scanned, it is popped from the stack and freed. This function
 * manages the buffering and searching of the files in a recursive (call-stack)
 * manner, and passes errors down the chain to the caller; all errors are
 * reduced to be of the type status_t, allowing for the safe use of
 * provide_error.
 *
 * TODO: this function, along with enumerate_repo_description, relies upon the
 * d_type field of the dirent structure, which is not recognised in strict
 * standards-compliance mode (-std=c99). This isn't particularly a "big deal",
 * as it is very widely supported, however I would like to eradicate its use if
 * there's an easier way to detect the nature of results from readdir(3). */

static enum status_t search_files ( struct repo_stack_t * stack,
                char ** needles, int ncount )
{
        struct repo_t * repo = NULL;
        enum dir_status_t dirstat = -1;
        unsigned int repo_base_len = 0;
        DIR * dp = NULL;
        struct buffer_info_t bi;

        init_buffer_instance ( &bi );

        while ( ( repo = stack_pop ( stack ) ) != NULL ) {
                bi.buffer [ 0 ] = '\0'; /* new buffer on repo change */
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
                                        break;
                                case BUFSTAT_FULL:
                                        search_buffer ( bi.buffer, needles,
                                                        ncount, repo );
                                        break;
                        }
                }

                if ( bi.status != BUFSTAT_FULL )
                        /* BUFSTAT_FULL: buffer already searched */
                        search_buffer ( bi.buffer, needles, ncount, repo );

                free ( repo );
                dnull ( &dp );
        }

        return STATUS_OK;
}

/* portdir_complain: providing the absence of the ARG_NO_COMPLAINING flag, this
 * function prints a pre-defined warning regarding the existence of the now-
 * deprecated PORTDIR environment variable/make.conf attribute. */

static inline void portdir_complain (  )
{
        if ( CHK_ARG ( options, ARG_NO_COMPLAINING ) != 0 )
                return; /* this will be inlined, so it's not _that_ terrible */

        fputs ( "WARNING: " PROGRAM_NAME " has detected the existence of " \
                        "PORTDIR,\neither as an environment variable, or " \
                        "existing in " PORTAGE_MAKECONF ".\nIt will be " \
                        "respected over the repos.conf/ format for this\n" \
                        "session, however to remove this warning from each " \
                        "run of\n" PROGRAM_NAME ", please remove all traces " \
                        "of it from your system\nand adopt the new, more " \
                        "flexible syntax.\n\n", stderr  );
}

/* get_repos: populate the stack with a list of repositories, returning
 * STATUS_OK on success; confer with get_base_dir, enumerate_repo_descriptions,
 * and their derivatives for more explicit information regrading the potential
 * errors. TODO (P.I.) This function first attempts to find the deprecated
 * PORTDIR value, either as an environment value, or as a key-value pair
 * in PORTAGE_MAKECONF. If this is found, it is used in favour of repos.conf/,
 * but a warning is issued as a means of encouraging users to drop deprecated
 * features. If, for any reason, PORTDIR cannot be taken from one of the two
 * sources, the standard repos.conf/ mechanism is used.
 *
 * TODO: reading from the environment variable is complete. Now, extracting from
 * the make.conf must be implemented. I suspect that ini_get_location (possibly
 * renamed to something more general) can be used, providing "PORTDIR" as the
 * key substring.
 *
 * If this function is successful, it dynamically allocates some memory for each
 * of the encountered repositories and pushes them to the `stack`, which can be
 * recursively cleaned/freed with stack_cleanse. If it fails at any stage, the
 * caller needn't free the stack. If the PORTDIR mechanism is used, the stack
 * contains one item, as PORTDIR-era systems did not facilitate multiple Portage
 * repositories. */

static enum status_t get_repos ( char base [ PATH_MAX ],
                struct repo_stack_t * stack )
{
        enum status_t status = STATUS_OK;
        char * portdir_val = NULL;
        struct repo_t * tmp_repo = NULL;

        stack_init ( stack );

        /* PORTDIR environment variable */
        if ( ( portdir_val = getenv ( "PORTDIR" ) ) != NULL )
                if ( ( tmp_repo = malloc ( sizeof ( struct repo_t ) ) )
                                != NULL ) {
                        strcpy ( tmp_repo->name, DEFAULT_REPO_NAME );

                        if ( strlen ( portdir_val ) < PATH_MAX ) {
                                strcpy ( tmp_repo->location, portdir_val );
                                stack_push ( stack, tmp_repo );
                                portdir_complain ( );

                                return STATUS_OK;
                        }

                        free ( tmp_repo );
                }

        /* Use repos.conf/ */
        if ( ( status = get_base_dir ( base ) != STATUS_OK ) ||
                        ( status = enumerate_repo_descriptions ( base, stack ) )
                        != STATUS_OK ) {
                stack_cleanse ( stack );
                return status;
        }

        return STATUS_OK;
}

int main ( int argc, char ** argv )
{
        char base [ PATH_MAX ];
        struct repo_stack_t repo_stack;
        enum status_t status = STATUS_OK;
        int arg_idx = 0;
        error_buffer [ 0 ] = '\0';

        if ( argc < 2 || process_args ( argc, argv, &arg_idx ) == -1 ) {
                fputs ( provide_error ( STATUS_BADARG ), stderr );
                fputc ( '\n', stderr );
                return EXIT_FAILURE;
        }

        if ( CHK_ARG ( options, ARG_SHOW_VERSION ) != 0 )
                print_version_info ( );

        if ( CHK_ARG ( options, ARG_SHOW_HELP ) != 0 ) {
                print_help_info ( argv [ 0 ] );
                return EXIT_SUCCESS; /* show help and quit */
        }

        if ( ( status = get_repos ( base, &repo_stack ) ) ) {
                print_error ( "Could not use the repository-description " \
                                "base directory.", status );
                return EXIT_FAILURE;
        }

        if ( argc - arg_idx > 0 && ( status = search_files ( &repo_stack, & (
                                                argv [ arg_idx ] ), argc -
                                        arg_idx ) ) != STATUS_OK ) {
                /* TODO: improved error-reporting not yet implemented past
                 * get_repos or for the argument-processor. */
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


/* ash-euses: main driver
 * Ashley Dixon. */

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <glob.h>

#include "euses.h"
#include "args.h"
#include "report.h"

#define QUERY_MAX ( 256  )
#define BUFFER_SZ ( 4096 )
#define LBUF_SZ   ( 8192 )

#define ASCII_MIN ( 0x20 )
#define ASCII_MAX ( 0x7E )

#define CONFIGROOT_ENVNAME "PORTAGE_CONFIGROOT"
#define CONFIGROOT_SUFFIX  "/repos.conf/"
#define CONFIGROOT_DEFAULT "/etc/portage"
#define PORTAGE_MAKECONF   "/../make.conf"
#define DEFAULT_REPO_NAME  "gentoo"

/* Globbin' patterns relative to the repository base locations. */
#define GLOB_PATTERN_ROOT "/profiles/*.desc"
#define GLOB_PATTERN_DESC "/profiles/desc/*.desc"

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

enum warning_t {
        WARNING_ERRNO =  1, /* c.f. errno */
        WARNING_QLONG = -1, /* a query exceeded QUERY_MAX - 1 */
        WARNING_RNONE = -2, /* no repositories; nothing to do */
        WARNING_QNONE = -3  /* no queries; nothing to do */
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
        char * buffer; /* buffer pointer, assumed to be of size LBUF_SZ */
};

/* provide_gen_error: returns a human-readable string representing an error
 * code, as enumerated in status_t. If the passed code is STATUS_ERRNO, the
 * strerror function is used with the current value of errno. This function
 * takes an integer as opposed to the `status_t` enum so it can be used with
 * similar functions in a function pointer. */

static const char * provide_gen_error ( int status )
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

                default: return "Unknown error.";
        }
}

/* provide_gen_warning: identical to `provide_gen_error`, except this function
 * deals with non-fatal warnings, returning a human-readable string representing
 * the warning. The `status` code should be compatible with the `warning_t`
 * type. */

static const char * provide_gen_warning ( int status )
{
        switch ( status ) {
                case WARNING_ERRNO: return strerror ( errno );
                case WARNING_QLONG: return "The query is too long.";
                case WARNING_RNONE: return "No repositories were found.";
                case WARNING_QNONE: return "No queries were provided.";

                default: return "Unknown warning.";
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

/* construct_path: copy `a` to `dest`, and then append `b`. This function
 * returns zero on success, or -1 on failure. In the latter case, errno is set
 * appropriately and the error buffer is populated with `b`. The destination
 * string is null-terminated on success, and truncated to zero on failure. If
 * `a` appears as NULL, `b` is appended to the current contents of `dest`. */

static int construct_path ( char * dest, const char * a, const char * b )
{
        size_t len = 0;

        if ( a == NULL )
                a = dest;
        else
                dest [ 0 ] = '\0';

        if ( ( len = strlen ( a ) + strlen ( b ) ) >= PATH_MAX - 1 ) {
                populate_info_buffer ( b );
                errno = ENAMETOOLONG;
                dest [ 0 ] = '\0';
                return -1;
        }

        dest [ len - 1 ] = '\0';

        if ( a != dest )
                /* `a` was (or should have been) NULL */
                strcpy ( dest, a );

        strcat ( dest, b );
        return 0;
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

/* replace_char: replace all occurrences of `find` with `replace` in `str`. */

static void replace_char ( char * str, const char find, const char replace )
{
        char * pos = str;

        while ( ( pos = strchr ( pos, find ) ) != NULL ) {
                str [ pos - str ] = replace;
                if ( * ( pos++ ) == '\0' )
                        break;
        }
}

/* get_keyval_value: gets the value of the `location` key in the ini file
 * provided in `buffer`, ignoring all horizontal whitespace; see the
 * skip_whitespace function. On success, this function returns STATUS_OK and
 * writes the appropriate value into the `location` string. On failure,
 * STATUS_INILOC is returned. */

static enum status_t get_keyval_value ( char location [ PATH_MAX ],
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
                                get_keyval_value ( repo->location,
                                        & ( buffer [ offset ] ), "location" ) )
                        != STATUS_OK ) {
                /* All these functions operate on the same desc_path. */
                populate_info_buffer ( desc_path );
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
        enum status_t status = STATUS_OK;
        char desc_path [ PATH_MAX ];

        if ( repo == NULL ) {
                populate_info_buffer ( filename );
                return STATUS_ERRNO;
        }

        if ( construct_path ( desc_path, base, filename ) == -1 ) {
                free ( repo );
                return STATUS_ERRNO;
        }

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
 * https://wiki.gentoo.org/wiki//etc/portage/repos.conf#Format
 *
 * TODO: this function relies upon the d_type field of the dirent structure,
 * which is not recognised in strict standards-compliance mode (-std=c99). This
 * isn't particularly a "big deal", as it is very widely supported, however I
 * would like to eradicate its use if there's an easier way to detect the nature
 * of results from readdir(3). */

static enum status_t enumerate_repo_descriptions ( char base [ ],
                struct repo_stack_t * stack )
{
        DIR * dp = NULL;
        struct dirent * dir = NULL;
        int gentoo_hit = 0; /* special case: base/gentoo.conf must exist */
        enum status_t status = STATUS_OK;

        if ( ( dp = opendir ( base ) ) == NULL ) {
                populate_info_buffer ( base );
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
 * the given buffer of LBUF_SZ. If the buffer is filled, BUFSTAT_FULL or
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

        if ( ( b_inf->fp == NULL && ( b_inf->fp = fopen ( path, "r" ) )
                                == NULL ) ) {
                /* the file cannot be opened */
                populate_info_buffer ( path );
                return BUFSTAT_ERRNO;
        }

        /* ensure the buffer is null-terminated */
        b_inf->buffer [ LBUF_SZ - 1 ] = '\0';

        if ( ( bw = fread ( & ( b_inf->buffer [ b_inf->idx ] ),
                                        sizeof ( char ),
                                        LBUF_SZ - 1 - b_inf->idx,
                                        b_inf->fp ) )
                        < LBUF_SZ - 1 ) {
                if ( ( b_inf->idx += bw ) == LBUF_SZ - 1 ) {
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
                populate_info_buffer ( path );
                fnull ( & ( b_inf->fp ) );
                return BUFSTAT_ERRNO;
        }

        if ( bw == LBUF_SZ - 1 ) {
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

/* init_buffer_instance: initialise a buffer_info_t structure with default
 * values, allocating the large file buffer. This function returns -1 on error
 * (errno is set appropriately by malloc), or zero on success. */

static int init_buffer_instance ( struct buffer_info_t * b_inf )
{
        b_inf->fp = NULL;
        b_inf->idx = 0;
        b_inf->status = BUFSTAT_MORE;

        if ( ( b_inf->buffer = malloc ( sizeof ( char ) * LBUF_SZ ) )
                        == NULL ) {
                populate_info_buffer ( "Large file buffer" );
                return -1;
        }

        return 0;
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

        if ( key_idx != 0 ) {
                /* This not is the first entry in the buffer. */
                tmp = buffer_start [ key_idx ];
                buffer_start [ key_idx ] = '\0';

                if ( ( start = strrchr ( buffer_start, '\n' ) ) == NULL )
                        start = buffer_start;
                else if ( * ( start++ ) == '\0' ) {
                        /* the entry illegitimate/poorly formatted */
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

/* populate_glob: collate all entries matching repo_base + GLOB_PATTERN_
 * {ROOT,DESC} in the glob_buf using glob(3). This function returns -1 on
 * failure---in which case errno and the information buffer are set
 * appropriately, and zero on success. It is the responsibility of the caller to
 * use globfree(3) for cleaning up the static allocations of glob. */

static int populate_glob ( char repo_base [ NAME_MAX + 1 ], glob_t * glob_buf )
{
        int status = 0;
        unsigned int base_len = strlen ( repo_base );
        glob_buf->gl_pathc = 0;

        /* This is an awkward chain of returns, but it must be done as such
         * (avoid `goto`). */
        if ( construct_path ( repo_base, NULL, GLOB_PATTERN_ROOT ) == -1 )
                return -1;

        if ( ( status = glob ( repo_base, 0, NULL, glob_buf ) ) == GLOB_NOSPACE
                        || status == GLOB_ABORTED ) {
                populate_info_buffer ( repo_base );
                return -1;
        }

        repo_base [ base_len ] = '\0';
        if ( construct_path ( repo_base, NULL, GLOB_PATTERN_DESC ) == -1 )
                return -1;

        if ( ( status = glob ( repo_base, GLOB_APPEND, NULL, glob_buf ) )
                        == GLOB_NOSPACE || status == GLOB_ABORTED ) {
                populate_info_buffer ( repo_base );
                return -1;
        }

        repo_base [ base_len ] = '\0';
        return 0;
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
                printf ( "%s::%s::", repo->location, repo->name ); 
        else if ( CHK_ARG ( options, ARG_PRINT_REPO_NAMES ) != 0 )
                printf ( "%s::", repo->name );

        fputs ( result_str, stdout );
        if ( truncated )
                puts ( " [...]" );
        else
                putchar ( '\n' );
}

/* construct_query: construct an appropriate query, taking `str`, applying the
 * correct filters according to the command-line arguments, and copying an
 * appropriate substring into `query`. The return value of this function should
 * never invoke a fatal error; the query should just be skipped. */

static int construct_query ( char query [ QUERY_MAX ], const char * str )
{
        int modified = 0;
        size_t len = 0;

        if ( CHK_ARG ( options, ARG_SEARCH_STRICT ) != 0 ) {
                if ( ( len = strlen ( str ) + 3 ) >= QUERY_MAX - 1 ) {
                        populate_info_buffer ( str );
                        print_warning ( WARNING_QLONG, &provide_gen_warning );
                        return -1;
                }

                strcpy ( query, str );
                strcat ( query, " - " );
                query [ len - 1 ] = '\0';
                modified = 1;
        }

        /* If the query has not been modified according to external factors
         * (such as command-line arguments), the caller must use the original
         * `str` as the needle. */
        return ( !modified );
}

/* search_buffer: search the `buffer` for the provided `needles`, of which there
 * are `ncount`. This function searches and prints the results as soon as they
 * are found, and, providing uninterrupted execution, exits with `buffer`
 * unchanged. TODO: investigate the advantages of strstr(3) alternative
 * implementations, such as Boyer-Moore. */

static void search_buffer ( char buffer [ LBUF_SZ ], char ** needles,
                int ncount, struct repo_t * repo )
{
        int bare_query = 1;
        char * ptr = NULL, query [ QUERY_MAX ], * buffer_start = buffer,
                * ( * searcher ) ( const char *, const char * ) =
                        ( CHK_ARG ( options, ARG_SEARCH_NO_CASE ) != 0 ) ?
                        &strcasestr : &strstr;

        for ( int i = 0; i < ncount; i++ ) {
                buffer = buffer_start;

                if ( ( bare_query = construct_query ( query, needles [ i ] ) )
                                == -1 )
                        continue;

                do {
                        if ( ( ptr = searcher ( buffer, ( bare_query == 1 )
                                                ? needles [ i ] : query ) )
                                        != NULL ) {
                                if ( ( ptr = find_line_bounds ( buffer, ptr,
                                                                &buffer ) )
                                                == NULL )
                                        break;

                                print_search_result ( ptr, repo,
                                                ( buffer == NULL ) );
                                if ( buffer == NULL )
                                        break; /* end of buffer; see `marker` */

                                /* undo the terminator added by
                                 * find_line_bounds */
                                ptr [ buffer - ptr ] = '\n';
                        }
                } while ( ptr != NULL );
        }
}

/* get_next_file: if the glob_buf has a path beyond gl_pathv [ *idx ], this
 * function returns a pointer to the relevant string, and the given index is
 * incremented. If no such path exists, NULL is returned. */

static inline char * get_next_file ( glob_t * glob_buf, size_t * idx )
{
        return ( *idx >= glob_buf->gl_pathc ) ? NULL :
                glob_buf->gl_pathv [ ( *idx )++ ];
}

/* process_glob_list: given a populated glob_t structure, this function searches
 * all files specified in `gl_pathv` for each of the `needles`, of which there
 * should be `ncount`. The `repo` also enabled increased verbosity by the
 * printing functions, should it have been requested at the command-line. `bi`
 * is a persistent buffer held by the caller; it is the responsibility of the
 * caller to free `glob_buf` and `repo`, as they are statically allocated. On
 * success, this function returns zero, or -1 on failure. In the latter event,
 * STATUS_ERRNO should be assumed. The information buffer is populated
 * appropriately. */

static int process_glob_list ( struct buffer_info_t * bi, glob_t * glob_buf,
                char ** needles, int ncount, struct repo_t * repo )
{
        char * file_path = NULL;
        size_t file_idx = 0;

        for ( ; ; ) {
                /* attempt to get the next file */
                if ( ( bi->status == BUFSTAT_BORDR
                                || bi->status == BUFSTAT_MORE ) &&
                                ( ( file_path = get_next_file ( glob_buf,
                                                                &file_idx ) )
                                  == NULL ) )
                        break; /* exhausted; next repo */

                switch ( bi->status = populate_buffer ( file_path,
                                        bi ) ) {
                        case BUFSTAT_ERRNO:
                                return -1;
                        case BUFSTAT_BORDR:
                        case BUFSTAT_MORE:
                                break;
                        case BUFSTAT_FULL:
                                search_buffer ( bi->buffer, needles,
                                                ncount, repo );
                                break;
                }
        }

        if ( bi->status != BUFSTAT_FULL )
                /* BUFSTAT_FULL: buffer already searched */
                search_buffer ( bi->buffer, needles, ncount, repo );

        return 0;
}

/* search_files: search the profiles / *.desc files in the repo `location`
 * directory to find any of the given needles. Once a repository's files have
 * been completely scanned, it is popped from the stack and freed. This function
 * manages the buffering and searching of the files in a recursive (call-stack)
 * manner, and passes errors down the chain to the caller; all errors are
 * reduced to be of the type status_t, allowing for the safe use of
 * provide_gen_error. All sub-functions populate the global information buffer
 * when appropriate. */

static enum status_t search_files ( struct repo_stack_t * stack,
                char ** needles, int ncount )
{
        struct repo_t * repo = NULL;
        struct buffer_info_t bi;
        glob_t glob_buf = { .gl_pathc = 0 };

        if ( init_buffer_instance ( &bi ) == -1 )
                return STATUS_ERRNO;

        while ( ( repo = stack_pop ( stack ) ) != NULL ) {
                /* discard previous buffer on repo change */
                bi.buffer [ 0 ] = '\0';

                if ( populate_glob ( repo->location, &glob_buf ) == -1 ||
                                process_glob_list ( &bi, &glob_buf, needles,
                                        ncount, repo ) == -1 ) {
                        free ( bi.buffer );
                        free ( repo );
                        globfree ( &glob_buf );
                        return STATUS_ERRNO;
                }

                free ( repo );
                globfree ( &glob_buf );
        }

        free ( bi.buffer );
        return STATUS_OK;
}

/* portdir_makeconf: attempt to extract the value from the "PORTDIR" key-value
 * pair in $PORTAGE_CONFIGROOT/make.conf. On success, this function returns
 * STATUS_OK. The caller can determine whether a key has been found by testing
 * the first character of `value` for a null-terminator. There is no requirement
 * to confer with the error buffer here, as errors are non-fatal. */

static enum status_t portdir_makeconf ( char base [ PATH_MAX ],
                char value [ PATH_MAX ] )
{
        char buffer [ BUFFER_SZ ];
        FILE * fp = NULL;
        enum status_t status = STATUS_OK;

        if ( construct_path ( value, base, PORTAGE_MAKECONF ) == -1 ||
                        ( fp = fopen ( value, "r" ) ) == NULL )
                return STATUS_ERRNO;

        value [ 0 ] = '\0';

        if ( fread ( buffer, sizeof ( char ), PATH_MAX - 1, fp ) <
                        PATH_MAX - 1 && ! feof ( fp ) ) {
                fclose ( fp );
                return STATUS_ERRNO;
        }

        fclose ( fp );

        if ( ( status = get_keyval_value ( value, buffer, "PORTDIR" ) )
                        != STATUS_OK )
                return ( status == STATUS_INILOC ) ? STATUS_OK : status;
        else
                /* Extraneous hyphens make no difference when placed as prefixes
                 * and suffixes to a UNIX path. */
                replace_char ( value, '"', '/' );

        return STATUS_OK;
}

/* portdir_attempt_envvar: attempt to retrieve the value of PORTDIR from the
 * environment variable string with getenv(3). If, for any reason, this cannot
 * be completed, -1 is returned; this is not necessarily a fatal error, and
 * should not be treated as such. On success, some heap memory is allocated and
 * pushed to the stack, and zero is returned. */

static int portdir_attempt_envvar ( struct repo_stack_t * stack )
{
        struct repo_t * tmp_repo = NULL;
        char * value = getenv ( "PORTDIR" );

        if ( value != NULL && ( tmp_repo = malloc ( sizeof ( struct repo_t ) ) )
                        != NULL ) {
                strcpy ( tmp_repo->name, DEFAULT_REPO_NAME );

                if ( strlen ( value ) < PATH_MAX ) {
                        strcpy ( tmp_repo->location, value );
                        stack_push ( stack, tmp_repo );
                        portdir_complain ( );

                        return 0;
                }

                free ( tmp_repo );
        }

        return -1;
}

/* portdir_attempt_file: this function exhibits very similar behaviour to
 * portdir_attempt_envvar, except it confers with a file instead of the
 * environment variables. Similar to portdir_attempt_envvar, this function can
 * dynamically allocate some memory on the stack (free with stack_cleanse), and
 * errors should not be treat as fatal. */

static int portdir_attempt_file ( struct repo_stack_t * stack,
                char base [ PATH_MAX ] )
{
        struct repo_t * tmp_repo = NULL;

        if ( ( tmp_repo = malloc ( sizeof ( struct repo_t ) ) ) != NULL ) {
                if ( portdir_makeconf ( base, tmp_repo->location )
                                != STATUS_OK ) {
                        free ( tmp_repo );
                        return -1;
                }

                if ( tmp_repo->location [ 0 ] != '\0' ) {
                        strcpy ( tmp_repo->name, DEFAULT_REPO_NAME );
                        stack_push ( stack, tmp_repo );
                        portdir_complain ( );

                        return 0;
                } else
                        free ( tmp_repo );
        }

        return -1;
}

/* get_repos: populate the stack with a list of repositories, returning
 * STATUS_OK on success; confer with get_base_dir, enumerate_repo_descriptions,
 * and their derivatives for more explicit information regrading the potential
 * errors. This function first attempts to find the deprecated PORTDIR value,
 * either as an environment value, or as a key-value pair in PORTAGE_MAKECONF.
 * If this is found, it is used in favour of repos.conf/, but a warning is
 * issued as a means of encouraging users to drop deprecated features. If, for
 * any reason, PORTDIR cannot be taken from one of the two sources, the standard
 * repos.conf/ mechanism is used.
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
        char * base_ptr = NULL;
        stack_init ( stack );

        /* construct the base path */
        if ( construct_path ( base, ( base_ptr = getenv ( CONFIGROOT_ENVNAME ) )
                                != NULL ? base_ptr : CONFIGROOT_DEFAULT,
                        CONFIGROOT_SUFFIX ) == -1 )
                return STATUS_ERRNO;


        /* Try the PORTDIR environment variable followed by make.conf if the
         * ARG_ATTEMPT_PORTDIR option is set. */
        if ( CHK_ARG ( options, ARG_ATTEMPT_PORTDIR ) != 0
                                && ( portdir_attempt_envvar ( stack ) == 0 ||
                                portdir_attempt_file ( stack, base ) == 0 ) ) {
                return STATUS_OK; /* successfully pushed to stack */
        }

        /* Use repos.conf/ */
        if ( ( status = enumerate_repo_descriptions ( base, stack ) )
                        != STATUS_OK ) {
                stack_cleanse ( stack );
                return status;
        }

        return STATUS_OK;
}

/* main: entry point for ash-euses. See args.h for a list and description of the 
 * accepted arguments.
 *
 * Syntax: [OPTION]... [SUBSTRING]... */

int main ( int argc, char ** argv )
{
        char base [ PATH_MAX ];
        struct repo_stack_t repo_stack;
        enum status_t status = STATUS_OK;
        int arg_idx = 0;

        info_buffer [ 0 ] = '\0';

        if ( process_args ( argc, argv, &arg_idx ) == -1 )
                return EXIT_FAILURE; /* process_args invokes print_fatal */

        if ( CHK_ARG ( options, ARG_SHOW_VERSION ) != 0 )
                print_version_info ( );

        if ( CHK_ARG ( options, ARG_SHOW_HELP ) != 0 ) {
                print_help_info ( argv [ 0 ] );
                return EXIT_SUCCESS; /* show help and quit */
        }

        if ( argc - arg_idx <= 0 ) {
                populate_info_buffer ( NULL ); /* no queries; nothing to do */
                print_warning ( WARNING_QNONE, &provide_gen_warning );
                return EXIT_SUCCESS;
        }

        /* push the repositories onto the stack */
        if ( ( status = get_repos ( base, &repo_stack ) ) ) {
                print_fatal ( "Could not use the repository-description " \
                                "base directory.", status, &provide_gen_error );
                return EXIT_FAILURE;
        }

        if ( repo_stack.size == 0 ) {
                populate_info_buffer ( NULL );
                print_warning ( WARNING_RNONE, &provide_gen_warning );
                stack_cleanse ( &repo_stack );
                return EXIT_SUCCESS;
        }

        /* buffer and search the repository USE-description files */
        if ( ( status = search_files ( &repo_stack, & ( argv [ arg_idx ] ), argc
                                        - arg_idx ) ) != STATUS_OK ) {
                print_fatal ( "Could not load the USE-description files.",
                                status, &provide_gen_error );
                stack_cleanse ( &repo_stack );
                return EXIT_FAILURE;
        }

        stack_cleanse ( &repo_stack );
        return EXIT_SUCCESS;
}


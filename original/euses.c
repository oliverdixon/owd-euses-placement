/* owd-euses: main driver
 * Oliver Dixon. */

#define _GNU_SOURCE
/* strcasestr */
#include <string.h>
#undef _GNU_SOURCE

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <dirent.h>
#include <stddef.h> /* ptrdiff_t */

#include "euses.h"
#include "args.h"
#include "converse.h"
#include "stack.h"
#include "colour.h"
#include "globbing.h"

#define BUFFER_SZ   ( 4096 ) /* Non-primary buffer size */
#define SBUF_SZ     ( 512  ) /* Read-ahead size for printing underflows. */
#define LBUF_SZ_MIN ( 512  ) /* Minimum primary buffer size. */

#ifndef LBUF_SZ
/* A user/distributor may want to change this. For an explanation of the
 * potential reasoning behind this, see the QUIRKS section of the manual. */
#define LBUF_SZ     ( 8192 ) /* Default primary buffer size. */
#else
#if LBUF_SZ < LBUF_SZ_MIN
/* Anything lower than LBUF_SZ_MIN would cause the output to be about as
 * readable as Finnegans Wake, as the previous (contextual) buffer information
 * would be skipped. */
#error "LBUF_SZ cannot be so minute."
#endif /* LBUF_SZ < LBUF_SZ_MIN */
#endif /* LBUF_SZ */

#define ASCII_MIN    ( 0x20 )
#define ASCII_MAX    ( 0x7E )
#define LINE_COMMENT ( '#'  )

#define CONFIGROOT_ENVNAME "PORTAGE_CONFIGROOT"
#define CONFIGROOT_SUFFIX  "/repos.conf/"
#define CONFIGROOT_DEFAULT "/etc/portage"
#define PORTAGE_MAKECONF   "/../make.conf"
#define DEFAULT_REPO_NAME  "gentoo"

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
    WARNING_OK    =  0, /* everything is OK */
    WARNING_RNONE = -1, /* no repositories; nothing to do */
    WARNING_QNONE = -2, /* no queries; nothing to do */
    WARNING_NONWL = -3, /* no newline found in the small buffer */
    WARNING_PDEXT = -4, /* PORTDIR was detected */
    WARNING_PDLST = -5, /* ARG_LIST_REPOS was set with PORTDIR */
    WARNING_NOEOF = -6  /* a file was improperly/abruptly terminated */
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
    char * path; /* path of `fp` */
    int truncated; /* truncation status */
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
        case WARNING_RNONE: return "No repositories were found.";
        case WARNING_QNONE: return "No queries were provided.";
        case WARNING_NONWL: return "The entry did not end with a " \
                    "new-line.";
        case WARNING_PDEXT: return PROGRAM_NAME " has detected the " \
                    "existence of PORTDIR, either as an " \
                    "environment variable, or existing " \
                    "in a Portage configuration file. It " \
                    "will be respected over the " \
                    "repos.conf/ format for this session" \
                    ", however it is important to update" \
                    "your Gentoo-like system to the " \
                    "latest standards.";
        case WARNING_PDLST: return "Disregarding the repository-" \
                    "listing request due to the presence" \
                    " of PORTDIR.";
        case WARNING_NOEOF: return "The file was rudely terminated.";

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

/* [exposed function] construct_path: copy `a` to `dest`, and then append `b`.
 * This function returns zero on success, or -1 on failure. In the latter case,
 * errno is set appropriately and the error buffer is populated with `b`. The
 * destination string is null-terminated on success, and truncated to zero on
 * failure. If `a` appears as NULL, `b` is appended to the current contents of
 * `dest`. */

int construct_path ( char * dest, const char * a, const char * b )
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
                || end < start )
            return STATUS_ININME;

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
            case 0x20: /* space */
            case 0x09: /* horizontal tabulation */
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
            i < BUFFER_SZ; i++ )
        if ( start [ i ] == '=' )
            keyvalue_found = 1;

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
        fnull ( &fp );
        return STATUS_ERRNO;
    }

    rewind ( fp );
    if ( f_len >= BUFFER_SZ ) {
        /* the repository-description file would not fit the buffer */
        fnull ( &fp );
        errno = EFBIG;
        return STATUS_ERRNO;
    }

    if ( f_len == 0 ) {
        /* the file is empty, and the INI-parser would fail */
        fnull ( &fp );
        return STATUS_INIEMP;
    }

    if ( ( bytes_read = fread ( buffer, sizeof ( char ), BUFFER_SZ - 1,
                    fp ) ) < BUFFER_SZ - 1
            && !feof ( fp ) ) {
        fnull ( &fp );
        return STATUS_ERRNO;
    }

    fnull ( &fp );
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
 * files) contained within `base` to the given stack. On success, this function
 * returns STATUS_OK, otherwise STATUS_ERRNO, in which case errno is set
 * appropriately. The file `gentoo.conf` must exist within the base directory;
 * if this is not the case, STATUS_NOGENR is returned. If ARG_LIST_REPOS was set
 * on the command-line, this function will pretty-print the repository stack and
 * base directory on success.
 *
 * https://wiki.gentoo.org/wiki//etc/portage/repos.conf#Format
 *
 * NOTE: this function relies upon the d_type field of the dirent structure,
 * which is not recognised in strict standards-compliance mode (-std=c99). This
 * isn't particularly a "big deal", as it is very widely supported, however I
 * would like to eradicate its use if there's an easier way to detect the nature
 * of results from readdir(3). The POSIX S_ISDIR macro (inode(7): regular
 * file=0100000 S_IFREG) seemed plausible, but it requires an additional stat(2)
 * call on each file, which is not an acceptable performance hit. */

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
        if ( CHK_ARG ( options, ARG_LIST_REPOS ) != 0 )
            list_repos ( stack, base );

        return STATUS_OK;
    }

    return STATUS_NOGENR;
}

/* feof_stream: feof alternative, removing the reliance on file-handling
 * functions, such as fread, to set the end-of-file indicator. Unfortunately,
 * when reading a file of size N into a buffer of identical capacity, fread
 * only flags EOF upon attempting to read the (N+1)th character; thus, feof
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

    /* Due to the behaviour of `get_seamless_buffer`, `pos` may exceed
     * `len`, but still be valid (and indicate end-of-file). */
    return ( pos >= len );
}

/* organise_buffer: given a populated buffer `bi->buffer`, this function
 * determines the nature of the buffer in relation to the status of the file
 * pointer, `bi->fp`. If the buffer is full, and there is more to be read
 * from the file, BUFSTAT_FULL is returned. If the buffer is full, and the file
 * has ended, BUFSTAT_BORDR (the borderline case) is returned, whereas
 * BUFSTAT_MORE is returned if the file has been exhausted and there is more
 * room in the buffer. If any error occurs, BUFSTAT_ERRNO is returned, and
 * `errno` and the information buffer are populated appropriately.
 *
 * This function always closes (and NULLs; see fnull) the relevant file
 * pointer should (a) the file have ended, or (b) an error occurs. */

static enum buffer_status_t determine_buffer_nature ( size_t bw,
        struct buffer_info_t * bi, char * path )
{
    if ( bw < LBUF_SZ - 1 ) {
        if ( ( bi->idx += bw ) == LBUF_SZ - 1 ) {
            bi->idx = 0;
            return BUFSTAT_FULL;
        }

        bi->buffer [ bi->idx ] = '\0';
        if ( feof_stream ( bi->fp ) == 1 ) {
            /* the buffer has not been filled because the file has
             * no more bytes */
            fnull ( & ( bi->fp ) );
            return BUFSTAT_MORE;
        }

        /* the buffer has not been filled because there was an error
         * with fread, the details of which were written to errno */
        populate_info_buffer ( path );
        fnull ( & ( bi->fp ) );
        return BUFSTAT_ERRNO;
    } else {
        bi->idx = 0;

        if ( feof_stream ( bi->fp ) == 1 ) {
            /* borderline case: the buffer has been filled, and the
             * file has ended */
            fnull ( & ( bi->fp ) );
            return BUFSTAT_BORDR;
        }

        /* the buffer has been filled, and there is still more in the
         * current file */
        return BUFSTAT_FULL;
    }
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

static enum buffer_status_t populate_buffer ( struct buffer_info_t * bi )
{
    size_t bw = 0;

    if ( ( bi->fp == NULL && ( bi->fp = fopen ( bi->path, "r" ) )
                == NULL ) ) {
        /* the file cannot be opened */
        populate_info_buffer ( bi->path );
        return BUFSTAT_ERRNO;
    }

    /* ensure the buffer is null-terminated */
    bi->buffer [ LBUF_SZ - 1 ] = '\0';
    bw = fread ( & ( bi->buffer [ bi->idx ] ), sizeof ( char ),
            LBUF_SZ - 1 - bi->idx, bi->fp );

    return determine_buffer_nature ( bw, bi, bi->path );
}

/* init_buffer_instance: initialise a buffer_info_t structure with default
 * values, allocating the large file buffer (LBUF_SZ). This function returns -1
 * on error (errno is set appropriately by malloc), or zero on success. */

static int init_buffer_instance ( struct buffer_info_t * bi )
{
    if ( ( bi->buffer = malloc ( sizeof ( char ) * LBUF_SZ ) )
            == NULL ) {
        populate_info_buffer ( "Large file buffer" );
        return -1;
    }

    bi->fp = NULL;
    bi->idx = 0;
    bi->status = BUFSTAT_MORE;
    bi->path = NULL;

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

/* process_seamless_buffer: validate/parse and print from the start of the
 * seamless buffer to the first newline. If any of the sub-functions fail, the
 * appropriate warning is returned; WARNING_OK being returned on success. */

static enum warning_t process_seamless_buffer ( struct buffer_info_t * bi,
        char buffer [ SBUF_SZ ], long * pos )
{
    ptrdiff_t newline_idx = 0;
    char * newline_pos = strchr ( buffer, '\n' );

    if ( newline_pos == NULL )
        /* No newline found. The warning for a failed fseek takes
         * precedence over the absence of a newline. */
        return ( fseek ( bi->fp, *pos, SEEK_SET ) == -1 ) ?
            WARNING_ERRNO : WARNING_NONWL;

    *pos += ( newline_idx = newline_pos - buffer );

    if ( errno != 0 || fseek ( bi->fp, *pos, SEEK_SET ) == -1 )
        /* an error occurred with the buffer-collation or the
         * position-reversal (fseek to `pos`). */
        return WARNING_ERRNO;

    /* This buffer is temporary; it will be thrown away by the caller. Thus,
     * adding this arbitrary NULL-terminator is perfectly safe. */
    buffer [ newline_idx ] = '\0';
    return WARNING_OK;
}

/* get_seamless_buffer: give the illusion of a seamless buffer, by allocating a
 * relatively small buffer (SBUF_SZ) to accommodate from the current position to
 * the end of the line, should that line appear in a subsequent buffer. This
 * function places the file cursor of `bi->fp` to the point after the newline,
 * to avoid double-searching. If, for any reason, the next line cannot be read
 * and parsed, a non-OK warning code is returned, and the information buffer is
 * populated accordingly. On success, the appropriate contents are placed in
 * `buffer` and WARNING_OK is returned. */

static enum warning_t get_seamless_buffer ( char buffer [ SBUF_SZ ],
        struct buffer_info_t * bi ) 
{
    enum warning_t warn_status = WARNING_OK;
    long pos = 0;

    errno = 0;

    /* (Attempt to) take the current position of the file, so it can be
     * reverted after the `fread`. This allows `populate_buffer` to
     * correctly read in the next buffer. To prevent researching the
     * remainder of the buffer, the position should be incremented to the
     * immediate character after the located newline, as a double-match in a
     * line would lead to the same line being printed twice. */

    if ( bi->fp == NULL ) {
        /* This case should not be silently skipped, as the truncation
         * flag was a false-positive (the bufferer closed the file due
         * to hitting the end, but the file wasn't properly EOF-
         * terminated). */
        populate_info_buffer ( bi->path );
        return WARNING_NOEOF;
    }

    if ( ( pos = ftell ( bi->fp ) ) == -1 ) {
        populate_info_buffer ( bi->path );
        return WARNING_ERRNO;
    }

    fread ( buffer, sizeof ( char ), SBUF_SZ - 1, bi->fp );

    if ( ( warn_status = process_seamless_buffer ( bi, buffer, &pos ) )
            != WARNING_OK ) {
        populate_info_buffer ( bi->path );
        return warn_status;
    }

    return WARNING_OK;
}

/* print_uncoloured_output: print the `result_str` uncoloured to stdout. If the
 * buffer has been truncated, `get_seamless_buffer` is used to pick up the rest
 * of the line. */

static void print_uncoloured_output ( char * result_str,
        struct buffer_info_t * bi )
{
    if ( ! ( bi->truncated ) ) {
        puts ( result_str );
        return;
    }

    /* The buffer has been truncated. Declaring variables here is rather
     * poor practice, however needlessly expanding the stack by SBUF_SZ for
     * 99% of matches would also be poor practice. */
    char extra_buffer [ SBUF_SZ ];
    enum warning_t warn_status = WARNING_OK;

    fputs ( result_str, stdout );

    if ( ( warn_status = get_seamless_buffer ( extra_buffer, bi ) )
            == WARNING_OK )
        /* If a warning has been issued by `print_warning`, there is no
         * need to add an additional newline. */
        puts ( extra_buffer );
    else {
        puts ( " [...]" );
        if ( CHK_ARG ( options, ARG_NO_MIDBUF_WARN ) == 0 )
            print_warning ( warn_status, &provide_gen_warning );
    }
}

/* correct_transbuffer_indexes: if the previous indexes of colour-changing
 * delimiters are infeasible, this function searches for them in the read-ahead
 * buffer, `buffer`, and sets the indexes and their respective corresponding
 * buffers. If the entry is poorly formatted, -1 is returned; zero is returned
 * on success. */

static int correct_transbuffer_indexes ( const char * buffer,
        ptrdiff_t * sep1_idx, ptrdiff_t * sep2_idx, char ** idx1_buf,
        char ** idx2_buf )
{
    /* If either of the indexes present with values suggesting an
     * unaddressable location, they do not exist in the primary buffer, but
     * may exist in the secondary. Thus, check there. The secondary buffer
     * is always NULL-terminated, so a return value of NULL definitively
     * states that the delimiter does not exist on the line. */

    if ( *sep1_idx <= 0 || *sep1_idx >= BUFFER_SZ ) {
        *sep1_idx = strchr ( buffer, ':' ) - buffer;
        *idx1_buf = ( char * ) buffer;
    }

    if ( *sep2_idx <= 0 || *sep2_idx >= BUFFER_SZ ) {
        *sep2_idx = strstr ( buffer, " - " ) - buffer;
        *idx2_buf = ( char * ) buffer;
    }

    return ( *sep2_idx <= 0 ) ? -1 : 0; /* poorly formatted entry ? */
}

/* print_coloured_block: properly format and print a coloured block, which could
 * be transbuffer, according to the defined sequences in "colour.h". This
 * function assumes that the `sep{1,2}_idx` indexes have been properly
 * normalised, and will not lead to segfaults when being used to index into
 * their respective buffers. */

static void print_coloured_block ( char * primary_buffer, char * idx1_buf,
        ptrdiff_t sep1_idx, char * idx2_buf, ptrdiff_t sep2_idx )
{
    idx2_buf [ sep2_idx ] = '\0';

    if ( sep1_idx > 0 ) {
        /* category-package */
        fputs ( HIGHLIGHT_PACKAGE, stdout );
        if ( idx1_buf != primary_buffer )
            fputs ( primary_buffer, stdout );
        idx1_buf [ sep1_idx ] = '\0';
        fputs ( idx1_buf, stdout );
        fputs ( HIGHLIGHT_STD ":" HIGHLIGHT_USEFLAG, stdout );
        fputs ( & ( idx1_buf [ sep1_idx + 1 ] ), stdout );
    } else {
        /* global USE-flag */
        fputs ( HIGHLIGHT_USEFLAG, stdout );
        fputs ( idx1_buf, stdout );
    }

    fputs ( HIGHLIGHT_STD, stdout );
    idx2_buf [ sep2_idx ] = ' ';
    fputs ( & ( idx2_buf [ sep2_idx ] ), stdout );
}

/* print_coloured_transbuffer_result: print a match in colour from a truncated
 * buffer. This function rectifies nonsensical indexes, by normalising them to
 * the secondary read-ahead buffer (c.f. `correct_transbuffer_indexes`). */

static void print_coloured_transbuffer_result ( char * result_str,
        struct buffer_info_t * bi, ptrdiff_t sep1_idx,
        ptrdiff_t sep2_idx )
{
    enum warning_t warn_status = WARNING_OK;
    char extra_buffer [ SBUF_SZ ], * idx1_buf = result_str,
     * idx2_buf = result_str;

    if ( ( warn_status = get_seamless_buffer ( extra_buffer, bi ) )
            != WARNING_OK ) {
        /* If the seamless buffer cannot be retrieved for some reason,
         * just print the match from the primary buffer without
         * colouring. */
        fputs ( result_str, stdout );
        puts ( " [...]" );
        if ( CHK_ARG ( options, ARG_NO_MIDBUF_WARN ) == 0 )
            print_warning ( warn_status, &provide_gen_warning );

        return;
    }

    if ( correct_transbuffer_indexes ( extra_buffer, &sep1_idx, &sep2_idx,
                &idx1_buf, &idx2_buf ) == -1 )
        return;

    print_coloured_block ( result_str, idx1_buf, sep1_idx, idx2_buf,
            sep2_idx );

    if ( idx1_buf == result_str && idx2_buf == result_str )
        /* finish printing the description */
        puts ( extra_buffer );
    else
        putchar ( '\n' );
}

/* locate_field_delims: find the index of the two field-delimiters in `str`,
 * placing the index of the package-flag separator and flag-description
 * separator in `pkg_flag` and `flagdesc` respectively. */

static inline void locate_field_delims ( char * str, ptrdiff_t * pkgflag,
        ptrdiff_t * flagdesc )
{
    ptrdiff_t tmp;
    *flagdesc = strstr ( str, " - " ) - str;

    if ( ( tmp = strchr ( str, ':' ) - str ) < *flagdesc )
        /* Ensure the package-flag delimiter precedes the
         * flag-description delimiter. */
        *pkgflag = tmp;

    /* strstr, on most libc implementations, is extremely fast for short
     * needles (usually a maximum of three characters). Only when the needle
     * exceeds 256 characters is the standard two-way algorithm used, and
     * even that uses a shift table. */
}

/* print_coloured_result: print `result_str` to stdout using the
 * HIGHLIGHT_PACKAGE and HIGHLIGHT_USEFLAG colours, with the flag description
 * being printed in HIGHLIGHT_STD. If an entry is poorly formatted, it is
 * silently skipped. */

static void print_coloured_result ( char * result_str,
        struct buffer_info_t * bi )
{
    ptrdiff_t sep1_idx = -1, sep2_idx = -1;
    locate_field_delims ( result_str, &sep1_idx, &sep2_idx );

    if ( bi->truncated ) {
        print_coloured_transbuffer_result ( result_str, bi, sep1_idx,
                sep2_idx );
        return;
    }

    if ( sep2_idx <= 0 )
        return; /* poorly formatted entry; skip */

    /* In single-buffer mode, both indexes regard the primary buffer. */
    print_coloured_block ( result_str, result_str, sep1_idx, result_str,
            sep2_idx );
    putchar ( '\n' );
}

/* print_search_result: print a search result, `result_str`, from the repo
 * `repo`, to stdout, respecting the ARG_PRINT_REPO_PATHS and
 * ARG_PRINT_REPO_NAMES command-line arguments. If `bi->truncated` is set (the
 * result continues in another buffer), a temporary SBUF_SZ buffer is allocated
 * on the stack to print the rest of the line (up until the next '\n'). If this
 * fails for any reason, " [...]" is printed to indicate a truncation. See
 * `get_seamless_buffer` for more information. */

static void print_search_result ( char * result_str, struct repo_t * repo,
        char * needle, struct buffer_info_t * bi )
{
    if ( CHK_ARG ( options, ARG_PRINT_NEEDLE ) != 0 )
        /* `needle` should probably be the original search string; not
         * modified by `construct_query`. */
        printf ( "(%s) ", needle );

    if ( CHK_ARG ( options, ARG_PRINT_REPO_PATHS ) != 0 )
        /* ARG_PRINT_REPO_PATHS implies ARG_PRINT_REPO_NAMES */
        printf ( ( CHK_ARG ( options, ARG_NO_COLOUR ) ) ?
                "%s::%s::" : HIGHLIGHT_REPO "%s" HIGHLIGHT_STD
                "::" HIGHLIGHT_REPO "%s" HIGHLIGHT_STD "::",
                repo->location, repo->name );
    else if ( CHK_ARG ( options, ARG_PRINT_REPO_NAMES ) != 0 )
        printf ( ( CHK_ARG ( options, ARG_NO_COLOUR ) ) ? "%s::" :
                HIGHLIGHT_REPO "%s" HIGHLIGHT_STD "::",
                repo->name );

    if ( CHK_ARG ( options, ARG_NO_COLOUR ) )
        print_uncoloured_output ( result_str, bi );
    else
        print_coloured_result ( result_str, bi );
}

/* verify_strict_compliance: assuming `ARG_SEARCH_STRICT` is set, this function
 * determines whether `mt_start` begins in the flag field, returning zero if it
 * does, and -1 otherwise. */

static inline int verify_strict_compliance ( char * ln_start, char * mt_start )
{
    ptrdiff_t pkgflag = -1, flagdesc = -1, idx = mt_start - ln_start;

    locate_field_delims ( ln_start, &pkgflag, &flagdesc );
    return ( ( pkgflag <= 0 || idx > pkgflag ) && idx < flagdesc ) ? 0 : -1;
}

/* search_buffer: search the `buffer` for the provided `needles`, of which there
 * are `ncount`. This function searches and prints the results as soon as they
 * are found, and, providing uninterrupted execution, exits with `buffer`
 * unchanged. */

static void search_buffer ( char buffer [ LBUF_SZ ], char ** needles,
        int ncount, struct repo_t * repo, struct buffer_info_t * bi )
{
    /* ln_start: start of the matching line; mt_start: start of the match */
    char * ln_start = NULL, * buffer_start = buffer, * mt_start = NULL,
        * ( * searcher ) ( const char *, const char * ) =
            ( CHK_ARG ( options, ARG_SEARCH_NO_CASE ) != 0 ) ?
            &strcasestr : &strstr;

    for ( int i = 0; i < ncount; i++ ) {
        buffer = buffer_start;

        if ( needles [ i ] [ 0 ] == '\0' )
            /* Ignore entries consisting of erroneous or empty
             * needles. */
            continue;

        while ( ( mt_start = searcher ( buffer, needles [ i ] ) )
                != NULL ) {
            if ( ( ln_start = find_line_bounds ( buffer, mt_start,
                            &buffer ) ) == NULL
                    || *ln_start == LINE_COMMENT )
                break;

            if ( CHK_ARG ( options, ARG_SEARCH_STRICT ) != 0
                    && verify_strict_compliance ( ln_start,
                        mt_start ) == -1 ) {
                if ( buffer == NULL )
                    break;

                mt_start [ buffer - mt_start ] = '\n';
                continue;
            }

            bi->truncated = ( buffer == NULL );
            print_search_result ( ln_start, repo,
                    needles [ i ], bi );
            if ( buffer == NULL )
                break; /* end of buffer; see `marker` */

            /* undo the change made by `find_line_bounds` */
            mt_start [ buffer - mt_start ] = '\n';
        }
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
 * should be `ncount`. The `repo` also enables increased verbosity by the
 * printing functions, should it have been requested at the command-line. `bi`
 * is a persistent buffer held by the caller; it is the responsibility of the
 * caller to free `glob_buf` and `repo`, as they are statically allocated. On
 * success, this function returns zero, or -1 on failure. In the latter event,
 * STATUS_ERRNO should be assumed. The information buffer is populated
 * appropriately. */

static int process_glob_list ( struct buffer_info_t * bi, glob_t * glob_buf,
        char ** needles, int ncount, struct repo_t * repo )
{
    size_t file_idx = 0;

    for ( ; ; ) {
        /* attempt to get the next file */
        if ( ( bi->status == BUFSTAT_BORDR
                || bi->status == BUFSTAT_MORE ) &&
                ( ( bi->path = get_next_file ( glob_buf,
                                &file_idx ) )
                  == NULL ) )
            break; /* exhausted; next repo */

        switch ( bi->status = populate_buffer ( bi ) ) {
            case BUFSTAT_ERRNO:
                return -1;
            case BUFSTAT_BORDR:
            case BUFSTAT_MORE:
                break;
            case BUFSTAT_FULL:
                search_buffer ( bi->buffer, needles,
                        ncount, repo, bi );
                break;
        }
    }

    if ( bi->status != BUFSTAT_FULL )
        /* BUFSTAT_FULL: buffer already searched */
        search_buffer ( bi->buffer, needles, ncount, repo, bi );

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
        fnull ( &fp );
        return STATUS_ERRNO;
    }

    fnull ( &fp );

    if ( ( status = get_keyval_value ( value, buffer, "PORTDIR" ) )
            != STATUS_OK )
        return ( status == STATUS_INILOC ) ? STATUS_OK : status;
    else
        /* Extraneous obliques make no difference when placed as prefixes
         * and suffixes to a UNIX path. */
        replace_char ( value, '"', '/' );

    return STATUS_OK;
}

/* portdir_complain: issue an appropriate warning regarding the existence of
 * PORTDIR; this can be disabled entirely using ARG_NO_COMPLAINING. If
 * ARG_LIST_REPOS is also set, a further warning is issued stating that a
 * session with PORTDIR renders a repository-listing useless. */

static void portdir_complain ( )
{
    if ( CHK_ARG ( options, ARG_NO_COMPLAINING ) == 0 ) {
        print_warning ( WARNING_PDEXT, &provide_gen_warning );
        putchar ( '\n' );

        if ( CHK_ARG ( options, ARG_LIST_REPOS ) != 0 ) {
            print_warning ( WARNING_PDLST, &provide_gen_warning );
            putchar ( '\n' );
        }
    }
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

/* prelim_checks: perform some preliminary checks, primarily revolving around
 * the argument-processing stage. This function returns zero on success, -1 on
 * hard-failure, and 1 on soft-failure (the program should probably terminate,
 * but not return a failing status code). `argc` and `argv` should be the raw
 * values provided by the environment, and `arg_idx` is the index, in `argv`, of
 * the first non-argument entry. */

static int prelim_checks ( int argc, char ** argv, int * arg_idx )
{
    if ( process_args ( argc, argv, arg_idx ) == -1 )
        return -1; /* process_args invokes print_fatal */

    if ( CHK_ARG ( options, ARG_SHOW_VERSION ) != 0 )
        print_version_info ( );

    if ( CHK_ARG ( options, ARG_SHOW_HELP ) != 0 ) {
        print_help_info ( argv [ 0 ] );
        return 1; /* show help and quit */
    }

    if ( argc - *arg_idx <= 0 ) {
        populate_info_buffer ( NULL ); /* no queries; nothing to do */
        print_warning ( WARNING_QNONE, &provide_gen_warning );
        return 1;
    }

    return 0;
}

/* main: entry point for owd-euses. See args.h for a list and description of the 
 * accepted arguments. EXIT_SUCCESS does not necessarily imply a complete
 * execution, but only indicates that no "hard" error was encountered.
 *
 * Syntax: [OPTION]... [SUBSTRING]... */

int main ( int argc, char ** argv )
{
    char base [ PATH_MAX ];
    struct repo_stack_t repo_stack;
    enum status_t status = STATUS_OK;
    int arg_idx = 0, prelim_status = 0;

    info_buffer [ 0 ] = '\0';

    if ( ( prelim_status = prelim_checks ( argc, argv, &arg_idx ) ) == -1 )
        return EXIT_FAILURE;
    else if ( status == 1 )
        return EXIT_SUCCESS;

    /* push the repositories onto the stack */
    if ( ( status = get_repos ( base, &repo_stack ) ) != STATUS_OK ) {
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


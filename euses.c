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
#define PORTDIR_ENVNAME "PORTDIR"

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
        STATUS_INILCS = -8  /* the location value exceeded PATH_MAX - 1 */ 
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

                default: return "Unknown error";
        }
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
        unsigned int i = 0;

        for ( ; ; i++ )
                switch ( str [ i ] ) {
                        case 0x20:
                        case 0x09:
                                continue;
                        case 0x00:
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
                                closedir ( dp );
                                return status;
                        }
                }

        closedir ( dp );
        return ( gentoo_hit ) ? STATUS_OK : STATUS_NOGENR;
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
        stack_cleanse ( &repo_stack );
        return EXIT_SUCCESS;
}


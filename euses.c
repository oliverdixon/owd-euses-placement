/* ash-euses: main driver
 * Ashley Dixon. */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>

#include "euses.h"

#define CONFIGROOT_ENVNAME "PORTAGE_CONFIGROOT"
#define CONFIGROOT_SUFFIX  "/repos.conf/"
#define CONFIGROOT_DEFAULT "/etc/portage"

struct desc_t {
        char location [ PATH_MAX ];
        struct repo_t parent;
};

enum status_t {
        STATUS_OK     =  0, /* everything is OK */
        STATUS_ERRNO  = -1, /* use perror or strerror on errno */
        STATUS_NOREPO = -2, /* no repository-description files were found */
        STATUS_NOGENR = -3  /* no gentoo.conf repository-description file */
};

/* provide_error: returns a human-readable string representing an error code, as
 * enumerated in status_t. If the passed code is STATUS_ERRNO, the strerror
 * function is used with the current value of errno. */

const char * provide_error ( enum status_t status )
{
        switch ( status ) {
                case STATUS_OK:     return "Everything is TTY.";
                case STATUS_ERRNO:  return strerror ( errno );
                case STATUS_NOREPO: return "No repository-description " \
                                                "files were found.";
                case STATUS_NOGENR: return "gentoo.conf does not exist.";

                default: return "Unknown error.";
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

/* register_repo: allocates memory for a repository, initialises it with empty
 * values and its location on the filesystem, and adds it to the stack. If the
 * physical location exceeds the length allowed by PATH_MAX, or malloc cannot
 * allocate enough memory, errno is set appropriately and STATUS_ERRNO is
 * returned. On success, STATUS_OK is returned. */

enum status_t register_repo ( char base [ ], char * filename,
                struct repo_stack_t * stack )
{
        struct repo_t * repo = malloc ( sizeof ( struct repo_t ) );
        unsigned int len = 0;

        if ( repo == NULL )
                return STATUS_ERRNO;

        if ( ( len = strlen ( filename ) + strlen ( base ) ) >= PATH_MAX ) {
                errno = ENAMETOOLONG;
                return STATUS_ERRNO;
        }

        strcpy ( repo->location, base );
        strcat ( repo->location, filename );
        repo->location [ len ] = '\0';
        repo->next = NULL;
        repo->ptr = NULL;

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

/* parse_repo_descriptions: I'm not sure what the function should do yet. I want
 * to avoid having loads of passes on the single struct, and I don't
 * particularly like accessing the next pointer manually either. */

void parse_repo_descriptions ( struct repo_stack_t * stack )
{
        struct repo_t * repo = stack_peek ( stack );

        do {
                /* Make the safe assumption that there exist at least one
                 * repository-description file on the stack. */
                puts ( repo->location );
        } while ( ( repo = repo->next ) != NULL );
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
                fputs ( "Could not use the repository-description base " \
                                "directory: ", stderr );
                fputs ( provide_error ( status ), stderr );
                fputc ( '\n', stderr );
                stack_cleanse ( &repo_stack );
                return EXIT_FAILURE;
        }

        parse_repo_descriptions ( &repo_stack );
        stack_cleanse ( &repo_stack );
        return EXIT_SUCCESS;
}


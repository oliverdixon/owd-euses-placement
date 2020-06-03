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

/* get_base_dir: populates `base` with the Portage configuration root (usually
 * CONFIGROOT_DEFAULT) prepended to the CONFIGROOT_SUFFIX. On returning zero,
 * `base` contains the base directory of the various repository-description
 * files. If -1 is returned, the base path would exceed the limit defined by
 * PATH_MAX; errno is set appropriately. */

int get_base_dir ( char base [ PATH_MAX ] )
{
        char * base_ptr = NULL;

        strcpy ( base, ( base_ptr = getenv ( CONFIGROOT_ENVNAME ) ) != NULL ?
                        base_ptr : CONFIGROOT_DEFAULT );

        if ( strlen ( base ) + strlen ( CONFIGROOT_SUFFIX ) >= PATH_MAX ) {
                errno = ENAMETOOLONG;
                return -1;
        }

        strcat ( base, CONFIGROOT_SUFFIX );
        return 0;
}

/* register_repo: allocates memory for a repository, initialises it with empty
 * values and its location on the filesystem, and adds it to the stack. If the
 * physical location exceeds the length allowed by PATH_MAX, or malloc cannot
 * allocate enough memory, errno is set appropriately and -1 is returned. On
 * success, zero is returned. */

int register_repo ( char base [ ], char * filename,
                struct repo_stack_t * stack )
{
        struct repo_t * repo = malloc ( sizeof ( struct repo_t ) );

        if ( repo == NULL )
                return -1;

        if ( strlen ( filename ) + strlen ( base ) >= PATH_MAX ) {
                errno = ENAMETOOLONG;
                return -1;
        }

        strcpy ( repo->location, base );
        strcat ( repo->location, filename );
        repo->next = NULL;
        repo->ptr = NULL;

        stack_push ( stack, repo );
        return 0;
}

/* enumerate_repo_descriptions: prints the regular files (repository
 * configuration files) contained within `base`. On success, this function
 * returns zero, otherwise -1, in which case errno is set appropriately. */

int enumerate_repo_descriptions ( char base [ ] ) 
{
        DIR * dp = NULL;
        struct dirent * dir = NULL;

        if ( ( dp = opendir ( base ) ) == NULL )
                return -1;

        while ( ( dir = readdir ( dp ) ) != NULL )
                if ( dir->d_type == DT_REG )
                        puts ( dir->d_name );

        closedir ( dp );
        return 0;
}

int main ( )
{
        char base [ PATH_MAX ];

        if ( get_base_dir ( base ) == -1 ||
                        enumerate_repo_descriptions ( base ) == -1 ) {
                perror ( "Could not use the repository-description base " \
                                "directory" );
                fputs ( "Euses cannot continue. Quitting.\n", stderr );
                return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
}


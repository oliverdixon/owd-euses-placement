/* ash-euses
 * Ashley Dixon. */

#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <linux/limits.h>

#define BUFFER_SZ ( 4096 )
#define CONFIGROOT_ENVNAME "PORTAGE_CONFIGROOT"
#define CONFIGROOT_SUFFIX  "/repos.conf/"
#define CONFIGROOT_DEFAULT "/etc/portage"

struct repo_t {
        char location [ PATH_MAX ], buffer [ BUFFER_SZ ];
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


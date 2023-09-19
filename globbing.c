/* ash-euses: globbing functions
 * Oliver Dixon. */

#include <string.h>

#include "euses.h"
#include "globbing.h"
#include "args.h"
#include "converse.h"

enum pattern_types_t {
    PATTERN_STD = 0,
    PATTERN_PKG = 1,
    PATTERN_GLB = 2
};

const char * glob_patterns [ 3 ] [ 2 ] = {
    /* Globbing patterns relative to the repository base locations. */
    { "/profiles/*\\.desc", "/profiles/desc/*\\.desc" },

    /* Patterns for ARG_PKG_FILES_ONLY */
    { "/profiles/*\\.local*\\.desc", "/profiles/desc/*\\.local*\\.desc" },

    /* Patterns for ARG_GLOBAL_ONLY */
    { "/profiles/*[!\\.local]\\.desc",
        "/profiles/desc/*[!\\.local]\\.desc" }
};

/* select_glob_patterns: set the `patterns` to the appropriate globbing
 * patterns, determined by the command-line arguments. `patterns [ 0 ]` is set
 * to the root pattern, and `patterns [ 1 ]` is set to the desc/ variant. */

static void select_glob_patterns ( char * patterns [ 2 ] )
{
    enum pattern_types_t idx = PATTERN_STD;

    if ( CHK_ARG ( options, ARG_PKG_FILES_ONLY ) != 0 )
        idx = PATTERN_PKG;
    else if ( CHK_ARG ( options, ARG_GLOBAL_ONLY ) != 0 )
        idx = PATTERN_GLB;

    patterns [ 0 ] = ( char * ) glob_patterns [ idx ] [ 0 ];
    patterns [ 1 ] = ( char * ) glob_patterns [ idx ] [ 1 ];
}

/* [exposed function] populate_glob: collate all entries matching repo_base +
 * GLOB_PATTERN_ {ROOT,DESC} in the glob_buf using glob(3).  This function
 * returns -1 on failure---in which case errno and the information buffer are
 * set appropriately, and zero on success.  It is the responsibility of the
 * caller to use globfree(3) for cleaning up the static allocations of glob. */

int populate_glob ( char repo_base [ NAME_MAX + 1 ], glob_t * glob_buf )
{
    int status = 0;
    unsigned int base_len = strlen ( repo_base );
    char * patterns [ 2 ];

    glob_buf->gl_pathc = 0;
    select_glob_patterns ( patterns );

    if ( construct_path ( repo_base, NULL, patterns [ 0 ] ) == -1 )
        return -1;

    if ( ( status = glob ( repo_base, 0, NULL, glob_buf ) ) == GLOB_NOSPACE
            || status == GLOB_ABORTED ) {
        populate_info_buffer ( repo_base );
        return -1;
    }

    repo_base [ base_len ] = '\0';
    if ( construct_path ( repo_base, NULL, patterns [ 1 ] ) == -1 )
        return -1;

    if ( ( status = glob ( repo_base, GLOB_APPEND, NULL, glob_buf ) )
            == GLOB_NOSPACE || status == GLOB_ABORTED ) {
        populate_info_buffer ( repo_base );
        return -1;
    }

    repo_base [ base_len ] = '\0';
    return 0;
}


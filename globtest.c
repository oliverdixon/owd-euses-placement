/* glob(3) test/template */

#include <stdio.h>
#include <glob.h>
#include <string.h>
#include <errno.h>

int main ( )
{
        const char * query = "./.profiles/*.desc";
        glob_t glob_buf = { .gl_pathc = 0 };
        int status = 0;

        if ( ( status = glob ( query, 0, NULL, &glob_buf ) ) == GLOB_NOSPACE
                        || status == GLOB_ABORTED ) {
                fputs ( strerror ( errno ), stderr );
                putc ( '\n', stderr );
                globfree ( &glob_buf );
                return 1;
        }

        if ( glob_buf.gl_pathc > 0 ) {
                puts ( "Here comes the results...\n" );

                for ( int i = 0; i < glob_buf.gl_pathc; i++ )
                        puts ( glob_buf.gl_pathv [ i ] );
        } else
                puts ( "No results." );

        globfree ( &glob_buf );
        return 0;
}


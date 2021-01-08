/* ash-euses: globbing function signatures
 * Oliver Dixon. */

#ifndef _GLOBBING_H
#define _GLOBBING_H

#include <glob.h>
#include <linux/limits.h>

int populate_glob ( char [ NAME_MAX + 1 ], glob_t * );

#endif /* _GLOBBING_H */


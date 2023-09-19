/* owd-euses: globbing function signatures
 * Oliver Dixon. */

#ifndef GLOBBING_H
#define GLOBBING_H

#include <glob.h>
#include <linux/limits.h>

int populate_glob ( char [ NAME_MAX + 1 ], glob_t * );

#endif /* GLOBBING_H */


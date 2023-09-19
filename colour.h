/* owd-euses: A.N.S.I.\ colouring sequences for highlighting search results
 * Oliver Dixon. */

#ifndef _COLOUR_H
#define _COLOUR_H

/* Various common colour codes. Set HIGHLIGHT_PACKAGE and HIGHLIGHT_USEFLAG to
 * one of the following to highlight the package names and USE-flags in the
 * output. */

#define ANSI_PREFIX "\033["

#define COLOUR_DEFAULT      ANSI_PREFIX "0m"
#define COLOUR_RED          ANSI_PREFIX "0;31m"
#define COLOUR_BOLD_RED     ANSI_PREFIX "1;31m"
#define COLOUR_GREEN        ANSI_PREFIX "0;32m"
#define COLOUR_BOLD_GREEN   ANSI_PREFIX "1;32m"
#define COLOUR_YELLOW       ANSI_PREFIX "0;33m"
#define COLOUR_BOLD_YELLOW  ANSI_PREFIX "1;33m"
#define COLOUR_BLUE         ANSI_PREFIX "0;34m"
#define COLOUR_BOLD_BLUE    ANSI_PREFIX "1;34m"
#define COLOUR_MAGENTA      ANSI_PREFIX "0;35m"
#define COLOUR_BOLD_MAGENTA ANSI_PREFIX "1;35m"
#define COLOUR_CYAN         ANSI_PREFIX "0;36m"
#define COLOUR_BOLD_CYAN    ANSI_PREFIX "1;36m"

#ifndef HIGHLIGHT_STD
/* Standard colour for output */
#define HIGHLIGHT_STD COLOUR_DEFAULT
#endif

#ifndef HIGHLIGHT_PACKAGE
/* Colour for category-package names */
#define HIGHLIGHT_PACKAGE COLOUR_CYAN
#endif

#ifndef HIGHLIGHT_USEFLAG
/* Colour for USE-flags */
#define HIGHLIGHT_USEFLAG COLOUR_BOLD_GREEN
#endif

#ifndef HIGHLIGHT_REPO
/* Colour for repository names */
#define HIGHLIGHT_REPO COLOUR_YELLOW
#endif

#endif /* _COLOUR_H */


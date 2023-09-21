#!/bin/bash
# OWD 2023

# I do not want to store the PDF page (39K at 150dpi) in version control. You
# can use this script to re-download the entire paper and extract the first page
# with GhostScript.

ORIG_URL="http://monge.univ-mlv.fr/~mac/Articles-PDF/CP-1991-jacm.pdf"
DEST_FILE="$(git rev-parse --show-toplevel)/presentation/twoway-cover.pdf"

wget -O - "$ORIG_URL" | gs       \
    -sDEVICE=pdfwrite            \
    -dNOPAUSE                    \
    -dBATCH                      \
    -dFirstPage=1                \
    -dLastPage=1                 \
    -dCompatibilityLevel=1.5     \
    -dDownsampleMonoImages=true  \
    -dMonoImageResolution=150    \
    -sOutputFile="$DEST_FILE" -


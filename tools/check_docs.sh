#!/bin/sh
# check_docs.sh - every relative link and image path in the Markdown documentation must exist.
#
#   sh tools/check_docs.sh            (run from the repository root)
#
# Scans README.md, CONTRIBUTING.md and docs/*.md for Markdown links/images "](target)",
# ignores absolute URLs (http, https, mailto) and pure in-page anchors ("#..."), strips a
# trailing "#anchor" and checks that the target file or directory exists relative to the
# Markdown file that contains the link. Exit status 1 if any target is missing.
set -u
missing=0
checked=0
for md in README.md CONTRIBUTING.md docs/*.md; do
    [ -f "$md" ] || continue
    dir=$(dirname "$md")
    # one target per line: the text between "](" and the next ")"
    targets=$(grep -o '](\([^)]*\))' "$md" | sed 's/^](//; s/)$//')
    for t in $targets; do
        case "$t" in
            http://*|https://*|mailto:*|\#*) continue ;;
        esac
        path=${t%%#*}
        [ -n "$path" ] || continue
        checked=$((checked + 1))
        if [ ! -e "$dir/$path" ]; then
            echo "check_docs: $md: missing link target '$t'"
            missing=$((missing + 1))
        fi
    done
done
if [ "$missing" -ne 0 ]; then
    echo "check_docs: $missing of $checked relative links are broken"
    exit 1
fi
echo "check_docs: all $checked relative links resolve"

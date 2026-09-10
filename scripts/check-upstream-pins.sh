#!/usr/bin/env bash
#
# Report pinned upstream versions that no longer match their upstream's latest
# release.
#
# The Makefile pins each upstream tree it clones (see the version variables at
# the top of the Makefile). When a firmware drops or renames a symbol that the
# upstream api.c calls, a binary built against an older pin still links fine but
# fails to load on the newer firmware, e.g. the h700-rc9 removal of GetMute from
# libmsettings. Nothing in the build catches that, so this check surfaces a
# stale pin instead.
#
# Tags are not comparable as versions across these upstreams (h700-rc9,
# v6.14.0, v20251127-1), so this is a plain string comparison and reports
# "differs", not "behind".
#
# Requires the gh CLI, authenticated for read access to public repos.

set -u

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# MY355_NEXTUI_VERSION is deliberately absent: it tracks the my355-latest branch
# rather than a release (see docs/nextui.md), so there is no tag to compare.
PINS="
MINUI_VERSION shauninman/MinUI
NEXTUI_VERSION loveRetro/NextUI
H700_VERSION pvaibhav/NextUI
"

stale=0

pinned_version() { # <VAR>
    make --no-print-directory -C "$REPO_ROOT" "print-$1" | cut -d= -f2-
}

latest_release() { # <owner/repo>
    gh release view --repo "$1" --json tagName -q .tagName
}

while read -r var repo; do
    [ -n "$var" ] || continue

    pinned="$(pinned_version "$var")"
    if [ -z "$pinned" ]; then
        echo "ERROR   $var: could not read the pin from the Makefile"
        stale=1
        continue
    fi

    latest="$(latest_release "$repo")"
    if [ -z "$latest" ]; then
        echo "ERROR   $var: could not read the latest release of $repo"
        stale=1
        continue
    fi

    if [ "$pinned" = "$latest" ]; then
        echo "ok      $var: $pinned matches the latest $repo release"
    else
        echo "DIFFERS $var: pinned $pinned, latest $repo release is $latest"
        stale=1
    fi
done <<< "$PINS"

exit "$stale"

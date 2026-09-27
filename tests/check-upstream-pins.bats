#!/usr/bin/env bats
#
# Tests for scripts/check-upstream-pins.sh.
#
# The gh CLI is replaced by a stub on PATH that answers with a tag per upstream
# repo and logs its arguments, so these run offline. The pins themselves are
# read from the Makefile, so the tests do not need updating when a pin moves.

setup() {
    REPO_ROOT="$(cd "$(dirname "$BATS_TEST_FILENAME")/.." && pwd)"
    STUB_DIR="$BATS_TEST_TMPDIR/bin"
    export GH_STUB_LOG="$BATS_TEST_TMPDIR/gh.log"
    mkdir -p "$STUB_DIR"

    # The stub answers with the tag in GH_STUB_TAG_<owner>_<repo> for the
    # --repo it is asked about.
    cat > "$STUB_DIR/gh" <<'EOF'
#!/usr/bin/env bash
echo "$*" >> "$GH_STUB_LOG"
repo=""
while [ $# -gt 0 ]; do
    if [ "$1" = "--repo" ]; then
        repo="$2"
        shift
    fi
    shift
done
var="GH_STUB_TAG_$(echo "$repo" | tr '/-' '__')"
echo "${!var:-}"
EOF
    chmod +x "$STUB_DIR/gh"
    PATH="$STUB_DIR:$PATH"

    export GH_STUB_TAG_shauninman_MinUI="$(pin MINUI_VERSION)"
    export GH_STUB_TAG_loveRetro_NextUI="$(pin NEXTUI_VERSION)"
    export GH_STUB_TAG_pvaibhav_NextUI="$(pin H700_VERSION)"
}

pin() { # <VAR>
    make --no-print-directory -C "$REPO_ROOT" "print-$1" | cut -d= -f2-
}

@test "reports ok and exits 0 when every pin matches the newest release" {
    run bash "$REPO_ROOT/scripts/check-upstream-pins.sh"
    [ "$status" -eq 0 ]
    [[ "$output" == *"ok      MINUI_VERSION"* ]]
    [[ "$output" == *"ok      NEXTUI_VERSION"* ]]
    [[ "$output" == *"ok      H700_VERSION"* ]]
}

@test "reports a differing pin and exits 1 when a newer release exists" {
    export GH_STUB_TAG_pvaibhav_NextUI="h700-rc999"
    run bash "$REPO_ROOT/scripts/check-upstream-pins.sh"
    [ "$status" -eq 1 ]
    [[ "$output" == *"DIFFERS H700_VERSION: pinned $(pin H700_VERSION), newest pvaibhav/NextUI release is h700-rc999"* ]]
    [[ "$output" == *"ok      MINUI_VERSION"* ]]
}

@test "reports an error and exits 1 when an upstream has no release" {
    export GH_STUB_TAG_loveRetro_NextUI=""
    run bash "$REPO_ROOT/scripts/check-upstream-pins.sh"
    [ "$status" -eq 1 ]
    [[ "$output" == *"ERROR   NEXTUI_VERSION: could not read the newest release of loveRetro/NextUI"* ]]
}

@test "compares against the newest release including prereleases, excluding drafts" {
    run bash "$REPO_ROOT/scripts/check-upstream-pins.sh"
    [ "$status" -eq 0 ]
    run grep -c -- "release list --repo pvaibhav/NextUI --exclude-drafts --limit 1" "$GH_STUB_LOG"
    [ "$output" = "1" ]
    run grep -c -- "release view" "$GH_STUB_LOG"
    [ "$output" = "0" ]
}

#!/usr/bin/env bats
#
# Argument validation tests for minui-list.
#
# These exercise parse_arguments, which runs before any display is
# initialized, so they can run headless against a built binary. Point at a
# specific binary with the MINUI_LIST_BIN environment variable; it defaults
# to ./minui-list-macos.

setup() {
    BIN="${MINUI_LIST_BIN:-./minui-list-macos}"
    if [ ! -x "$BIN" ]; then
        skip "binary not found or not executable: $BIN (build with: PLATFORM=macos make)"
    fi

    TESTFILE="$(mktemp "${BATS_TEST_TMPDIR:-/tmp}/minui-list-items.XXXXXX")"
    printf 'item 1\nitem 2\nitem 3\n' > "$TESTFILE"
}

# regression: issue 82 - "Y" collided with the default enable button

@test "action button Y is accepted (no conflict with default enable button)" {
    run "$BIN" --file "$TESTFILE" --format xml --action-button Y
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"cannot be assigned to more than one button"* ]]
}

@test "action button A is accepted (no conflict with default confirm button)" {
    run "$BIN" --file "$TESTFILE" --format xml --action-button A
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"cannot be assigned to more than one button"* ]]
}

@test "action button B is accepted (no conflict with default cancel button)" {
    run "$BIN" --file "$TESTFILE" --format xml --action-button B
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"cannot be assigned to more than one button"* ]]
}

@test "explicit double assignment (action Y + enable Y) is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --action-button Y --enable-button Y
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"cannot be assigned to more than one button"* ]]
}

# issue 69 - L1/R1 alphabetic scroll navigation

@test "--alphabetic-scroll flag is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --alphabetic-scroll
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"unrecognized option"* ]]
}

@test "-S short flag is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml -S
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"unrecognized option"* ]]
}

# issue 12 - --scroll-method autoscroll

@test "--scroll-method wrap is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --scroll-method wrap
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"Invalid scroll method"* ]]
    [[ "$output" != *"unrecognized option"* ]]
}

@test "--scroll-method pong is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --scroll-method pong
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"Invalid scroll method"* ]]
}

@test "--scroll-method false is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --scroll-method false
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"Invalid scroll method"* ]]
}

@test "invalid scroll method is rejected" {
    run "$BIN" --file "$TESTFILE" --scroll-method bounce
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid scroll method provided"* ]]
}

# surviving validation

@test "invalid format is rejected" {
    run "$BIN" --file "$TESTFILE" --format xml
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
}

@test "invalid confirm button is rejected" {
    run "$BIN" --file "$TESTFILE" --confirm-button Z
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid confirm button provided"* ]]
}

@test "invalid cancel button is rejected" {
    run "$BIN" --file "$TESTFILE" --cancel-button Z
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid cancel button provided"* ]]
}

@test "missing file is rejected" {
    run "$BIN" --format text
    [ "$status" -eq 1 ]
    [[ "$output" == *"No input provided"* ]]
}

# issue 123 - segfault on JSON array of objects without --item-key
#
# invalid JSON item shapes used to reach the renderer with empty names and
# crash (EXC_BAD_ACCESS, exit 139). They must now fail gracefully instead.

@test "top-level array of objects is rejected (no segfault)" {
    JSONFILE="$(mktemp "${BATS_TEST_TMPDIR:-/tmp}/minui-list-json.XXXXXX")"
    printf '[{"name":"Apple"},{"name":"Banana"}]' > "$JSONFILE"
    run "$BIN" --file "$JSONFILE" --format json
    [ "$status" -eq 1 ]
    [ "$status" -ne 139 ]
    [[ "$output" == *"is not a string"* ]]
}

@test "empty string in a top-level array is rejected" {
    JSONFILE="$(mktemp "${BATS_TEST_TMPDIR:-/tmp}/minui-list-json.XXXXXX")"
    printf '[""]' > "$JSONFILE"
    run "$BIN" --file "$JSONFILE" --format json
    [ "$status" -eq 1 ]
    [ "$status" -ne 139 ]
    [[ "$output" == *"empty name"* ]]
}

@test "strings under --item-key are rejected (no segfault)" {
    JSONFILE="$(mktemp "${BATS_TEST_TMPDIR:-/tmp}/minui-list-json.XXXXXX")"
    printf '{"items":["Apple","Banana"]}' > "$JSONFILE"
    run "$BIN" --file "$JSONFILE" --format json --item-key items
    [ "$status" -eq 1 ]
    [ "$status" -ne 139 ]
    [[ "$output" == *"is not an object"* ]]
}

@test "nameless object under --item-key is rejected" {
    JSONFILE="$(mktemp "${BATS_TEST_TMPDIR:-/tmp}/minui-list-json.XXXXXX")"
    printf '{"items":[{}]}' > "$JSONFILE"
    run "$BIN" --file "$JSONFILE" --format json --item-key items
    [ "$status" -eq 1 ]
    [ "$status" -ne 139 ]
    [[ "$output" == *"missing a name"* ]]
}

# issue 124 - JSON object form should work without --item-key
#
# a root object is now parsed as the object form using the default "items" key,
# so it no longer needs --item-key. these inputs fail object-form validation
# (proving the object path is taken); before the fix they yielded the generic
# "No selectable items found".

@test "object form uses the default items key without --item-key" {
    JSONFILE="$(mktemp "${BATS_TEST_TMPDIR:-/tmp}/minui-list-json.XXXXXX")"
    printf '{"items":[{}]}' > "$JSONFILE"
    run "$BIN" --file "$JSONFILE" --format json
    [ "$status" -eq 1 ]
    [ "$status" -ne 139 ]
    [[ "$output" == *"missing a name"* ]]
    [[ "$output" != *"No selectable items found"* ]]
}

@test "strings under the default items key are rejected without --item-key" {
    JSONFILE="$(mktemp "${BATS_TEST_TMPDIR:-/tmp}/minui-list-json.XXXXXX")"
    printf '{"items":["Apple","Banana"]}' > "$JSONFILE"
    run "$BIN" --file "$JSONFILE" --format json
    [ "$status" -eq 1 ]
    [ "$status" -ne 139 ]
    [[ "$output" == *"is not an object"* ]]
    [[ "$output" != *"No selectable items found"* ]]
}

# issue 13 - per-item right-hand-side images
#
# images live under "features.images" (a resolution -> path map, with the single
# image expressed as features.images.default). invalid shapes must fail
# validation (which runs before display init) rather than reach the renderer.

@test "non-object features.images is rejected" {
    JSONFILE="$(mktemp "${BATS_TEST_TMPDIR:-/tmp}/minui-list-json.XXXXXX")"
    printf '{"items":[{"name":"Apple","features":{"images":"nope"}}]}' > "$JSONFILE"
    run "$BIN" --file "$JSONFILE" --format json
    [ "$status" -eq 1 ]
    [ "$status" -ne 139 ]
    [[ "$output" == *"features.images must be an object"* ]]
}

@test "non-string features.images entry is rejected" {
    JSONFILE="$(mktemp "${BATS_TEST_TMPDIR:-/tmp}/minui-list-json.XXXXXX")"
    printf '{"items":[{"name":"Apple","features":{"images":{"default":123}}}]}' > "$JSONFILE"
    run "$BIN" --file "$JSONFILE" --format json
    [ "$status" -eq 1 ]
    [ "$status" -ne 139 ]
    [[ "$output" == *"features.images entry 'default' must be a string"* ]]
}

@test "--screen-resolution is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --screen-resolution 1280x720
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"Invalid screen resolution"* ]]
    [[ "$output" != *"unrecognized option"* ]]
}

@test "invalid --screen-resolution is rejected" {
    run "$BIN" --file "$TESTFILE" --screen-resolution 1280x
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid screen resolution provided"* ]]
}

@test "--fallback-image is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --fallback-image /path/to/fallback.png
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"unrecognized option"* ]]
}

# issue 11 - inline keyboard for filtering the list

@test "--allow-filter true is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --allow-filter true
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"Invalid allow-filter"* ]]
    [[ "$output" != *"unrecognized option"* ]]
}

@test "--allow-filter false is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --allow-filter false
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"Invalid allow-filter"* ]]
}

@test "invalid --allow-filter value is rejected" {
    run "$BIN" --file "$TESTFILE" --allow-filter maybe
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid allow-filter value provided"* ]]
}

@test "--filter-button SELECT is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --filter-button SELECT
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"Invalid filter button"* ]]
}

@test "--filter-button L1 is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --filter-button L1
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"Invalid filter button"* ]]
}

@test "invalid --filter-button value is rejected" {
    run "$BIN" --file "$TESTFILE" --filter-button ZZZ
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid filter button provided"* ]]
}

@test "face-button --filter-button is rejected" {
    run "$BIN" --file "$TESTFILE" --filter-button A
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid filter button provided"* ]]
}

@test "--display-filter-keyboard true is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --display-filter-keyboard true
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"Invalid display-filter-keyboard"* ]]
}

@test "invalid --display-filter-keyboard value is rejected" {
    run "$BIN" --file "$TESTFILE" --display-filter-keyboard maybe
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid display-filter-keyboard value provided"* ]]
}

@test "--filter-input is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --filter-input hack
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"unrecognized option"* ]]
}

@test "--filter-text-file is accepted" {
    run "$BIN" --file "$TESTFILE" --format xml --filter-text-file /tmp/filter.txt
    [ "$status" -eq 1 ]
    [[ "$output" == *"Invalid format provided"* ]]
    [[ "$output" != *"unrecognized option"* ]]
}

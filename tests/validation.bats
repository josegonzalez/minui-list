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

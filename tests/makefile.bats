#!/usr/bin/env bats
#
# Build-wiring tests for the Makefile.
#
# These assert how the Makefile resolves per-platform build variables, with a
# focus on the NextUI-specific variants (issue 118). They introspect the
# Makefile with `make print-<VAR> PLATFORM=<p>`, so they need neither a cross
# toolchain nor a cloned upstream tree and run on any host with make.

setup() {
    REPO_ROOT="$(cd "$(dirname "$BATS_TEST_FILENAME")/.." && pwd)"
}

# print-<VAR> for a platform; sets $status/$output/$lines via bats `run`.
mk() { # <VAR> <PLATFORM>
    run make --no-print-directory -C "$REPO_ROOT" "print-$1" PLATFORM="$2"
    [ "$status" -eq 0 ]
}

# tg5040-nextui: NextUI build for the Trimui Brick/Smart Pro (also has a MinUI build)

@test "tg5040-nextui uses the loveRetro NextUI upstream at the pinned tag" {
    mk UPSTREAM_REPO tg5040-nextui
    [ "$output" = "UPSTREAM_REPO=https://github.com/loveRetro/NextUI" ]
    mk UPSTREAM_VERSION tg5040-nextui
    [ "$output" = "UPSTREAM_VERSION=v6.14.0" ]
}

@test "tg5040-nextui builds the bare tg5040 workspace and is flagged NextUI" {
    mk WORKSPACE tg5040-nextui
    [ "$output" = "WORKSPACE=tg5040" ]
    mk IS_NEXTUI tg5040-nextui
    [ "$output" = "IS_NEXTUI=1" ]
}

@test "tg5040-nextui bakes the device id (not the -nextui variant) into -DPLATFORM" {
    mk CFLAGS tg5040-nextui
    [[ "$output" == *'-DPLATFORM_NEXTUI'* ]]
    [[ "$output" == *'-DPLATFORM=\"tg5040\"'* ]]
    [[ "$output" != *'-DPLATFORM=\"tg5040-nextui\"'* ]]
}

@test "tg5040-nextui compiles the tg5040 platform source and NextUI config" {
    mk SOURCE tg5040-nextui
    [[ "$output" == *'minui/workspace/tg5040/platform/platform.c'* ]]
    [[ "$output" == *'minui/workspace/all/common/config.c'* ]]
}

@test "tg5040-nextui links GLESv2 only (no mali, no samplerate)" {
    mk NEXTUI_GL_LIBS tg5040-nextui
    [ "$output" = "NEXTUI_GL_LIBS=-lGLESv2" ]
}

@test "tg5040-nextui produces the -nextui artifact id" {
    mk PLATFORM tg5040-nextui
    [ "$output" = "PLATFORM=tg5040-nextui" ]
}

# my355-nextui: NextUI build for the Miyoo Flip (workspace lives on a branch)

@test "my355-nextui uses loveRetro NextUI at the my355-latest branch" {
    mk UPSTREAM_REPO my355-nextui
    [ "$output" = "UPSTREAM_REPO=https://github.com/loveRetro/NextUI" ]
    mk UPSTREAM_VERSION my355-nextui
    [ "$output" = "UPSTREAM_VERSION=my355-latest" ]
}

@test "my355-nextui builds the bare my355 workspace with the device id" {
    mk WORKSPACE my355-nextui
    [ "$output" = "WORKSPACE=my355" ]
    mk CFLAGS my355-nextui
    [[ "$output" == *'-DPLATFORM=\"my355\"'* ]]
    [[ "$output" == *'-DPLATFORM_NEXTUI'* ]]
}

@test "my355-nextui links samplerate but not mali" {
    mk NEXTUI_GL_LIBS my355-nextui
    [[ "$output" == *'-lGLESv2'* ]]
    [[ "$output" == *'-lsamplerate'* ]]
    [[ "$output" != *'-lmali'* ]]
}

# tg5050-nextui: NextUI-only device that needs the standalone mali blob

@test "tg5050-nextui uses loveRetro NextUI and the tg5050 workspace" {
    mk UPSTREAM_REPO tg5050-nextui
    [ "$output" = "UPSTREAM_REPO=https://github.com/loveRetro/NextUI" ]
    mk WORKSPACE tg5050-nextui
    [ "$output" = "WORKSPACE=tg5050" ]
}

@test "tg5050-nextui links the mali blob explicitly" {
    mk NEXTUI_GL_LIBS tg5050-nextui
    [[ "$output" == *'-lmali'* ]]
    [[ "$output" == *'-lsamplerate'* ]]
}

# h700-nextui: NextUI-only device sourced from the pvaibhav fork

@test "h700-nextui uses the pvaibhav NextUI fork at the h700 tag" {
    mk UPSTREAM_REPO h700-nextui
    [ "$output" = "UPSTREAM_REPO=https://github.com/pvaibhav/NextUI" ]
    mk UPSTREAM_VERSION h700-nextui
    [ "$output" = "UPSTREAM_VERSION=h700-rc3" ]
    mk WORKSPACE h700-nextui
    [ "$output" = "WORKSPACE=h700" ]
}

@test "h700-nextui links samplerate but not mali" {
    mk NEXTUI_GL_LIBS h700-nextui
    [[ "$output" == *'-lsamplerate'* ]]
    [[ "$output" != *'-lmali'* ]]
}

# regression: a plain MinUI platform is untouched by the NextUI wiring

@test "tg5040 (MinUI) uses the shauninman upstream and is not a NextUI build" {
    mk UPSTREAM_REPO tg5040
    [ "$output" = "UPSTREAM_REPO=https://github.com/shauninman/MinUI" ]
    mk IS_NEXTUI tg5040
    [ "$output" = "IS_NEXTUI=" ]
    mk WORKSPACE tg5040
    [ "$output" = "WORKSPACE=tg5040" ]
}

@test "tg5040 (MinUI) defines neither PLATFORM_NEXTUI nor the config source" {
    mk CFLAGS tg5040
    [[ "$output" != *'-DPLATFORM_NEXTUI'* ]]
    mk SOURCE tg5040
    [[ "$output" != *'minui/workspace/all/common/config.c'* ]]
}

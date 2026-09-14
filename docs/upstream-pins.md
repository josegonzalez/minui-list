# Upstream pins

The build clones an upstream firmware tree and compiles part of it into the binary: `api.c`,
`utils.c`, `scaler.c`, the device's `platform.c`, and, for the cross builds, `libmsettings`. Each
tree is pinned to a tag in the Makefile, so an upstream release cannot change the build until the
pin moves.

| Makefile variable | Upstream repo | Covers |
|---|---|---|
| `MINUI_VERSION` | `shauninman/MinUI` | every MinUI platform (`tg5040`, `my355`, `rg35xxplus`, `zero28`, and the rest of the CI matrix) |
| `NEXTUI_VERSION` | `loveRetro/NextUI` | `tg5040-nextui` and `tg5050-nextui` |
| `MY355_NEXTUI_VERSION` | `loveRetro/NextUI` | `my355-nextui`, which tracks the `my355-latest` branch rather than a release |
| `H700_VERSION` | `pvaibhav/NextUI` | `h700-nextui` |

The pinned values themselves live in the Makefile and are deliberately not repeated here, so there
is one less place for them to drift.

## Why a stale pin matters

The upstream trees are pinned by tag, so a firmware release that changes the SDK ABI does not reach
the build until the pin moves. That is a real failure mode: `h700-rc9` turned the whole
`libmsettings` mute and turbo API into header-only inline stubs, so a binary built against
`h700-rc3` still linked but died on device with `undefined symbol: GetMute`. `-lmsettings` is a
shared library, so those symbols are resolved against the firmware's copy at load time, and nothing
in the build can see the mismatch.

## Checking the pins

`scripts/check-upstream-pins.sh` compares each pin against the latest release of its upstream and
exits non-zero if any differs:

```bash
bash scripts/check-upstream-pins.sh
```

It covers `MINUI_VERSION`, `NEXTUI_VERSION`, and `H700_VERSION`. `MY355_NEXTUI_VERSION` is skipped
because it tracks a branch rather than a release. The upstreams do not share a version scheme and
their tags are not orderable against each other, so the script reports "differs", not "behind".

The `upstream-pins` workflow runs the same script weekly and keeps a single tracking issue up to
date, closing it once every pin matches again.

## Bumping a pin

1. Read the upstream diff between the two tags and look only for files this repo actually compiles:
   `workspace/all/common/` (`api.c`, `utils.c`, `scaler.c` and their headers), the `platform.c` and
   `platform.h` of the devices in the CI matrix, and `libmsettings`. Everything else upstream ships
   is irrelevant here.

   ```bash
   gh api repos/<owner>/<repo>/compare/<old-tag>...<new-tag> --jq '.files[].filename'
   ```

2. Watch in particular for a symbol disappearing from `libmsettings`. That is the `GetMute` break
   above, and neither the compile nor the link will catch it.
3. Bump the variable in the Makefile.
4. Update the literal tag in `tests/makefile.bats` - the pins are asserted there so a
   bump cannot happen silently.
5. If the pin is a NextUI one, update its version column in the
   [platform matrix](nextui.md#platform-matrix).
6. Run the checks:

   ```bash
   bats tests/makefile.bats
   bash scripts/check-upstream-pins.sh
   ```

7. Let CI build every platform in its toolchain container. That is the only thing that exercises the
   cross compile and link against the new tree.

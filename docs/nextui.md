# NextUI builds

Some devices run NextUI, a fork of MinUI, instead of (or in addition to) MinUI. NextUI ships a different SDK, so those devices need a binary built against a NextUI toolchain. These NextUI-specific binaries carry a `-nextui` suffix in their platform id and artifact name.

## Platform matrix

| Platform id     | Firmware | Upstream repo              | Version / ref (Makefile var)          | Workspace dir | Toolchain image                       |
|-----------------|----------|----------------------------|---------------------------------------|---------------|---------------------------------------|
| `tg5040-nextui` | NextUI   | `loveRetro/NextUI`         | `v6.14.0` (`NEXTUI_VERSION`)          | `tg5040`      | `savant/minui-toolchain:tg5040-nextui` |
| `my355-nextui`  | NextUI   | `loveRetro/NextUI`         | `my355-latest` (`MY355_NEXTUI_VERSION`) | `my355`     | `savant/minui-toolchain:my355-nextui`  |
| `tg5050-nextui` | NextUI   | `loveRetro/NextUI`         | `v6.14.0` (`NEXTUI_VERSION`)          | `tg5050`      | `savant/minui-toolchain:tg5050-nextui` |
| `h700-nextui`   | NextUI   | `pvaibhav/NextUI`          | `h700-rc3` (`H700_VERSION`)           | `h700`        | `savant/minui-toolchain:h700-nextui`   |

`tg5040` and `my355` also have MinUI builds (`minui-list-tg5040`, `minui-list-my355`) since those devices run both firmwares. `tg5050` and `h700` are NextUI-only.

The `my355` workspace only exists on the `my355-latest` branch of `loveRetro/NextUI` (no tagged release contains it), so it uses its own version variable.

## How the build is wired

The Makefile keeps the build platform id (`PLATFORM`) separate from the upstream workspace directory and the on-device id:

- `WORKSPACE` is the upstream workspace directory name and the runtime device id. It equals `PLATFORM` for every platform except the `-nextui` variants, where it is the bare device (for example `PLATFORM=tg5040-nextui` builds `WORKSPACE=tg5040`).
- `IS_NEXTUI` is set for NextUI platforms. It gates the `/opt/nextui` include and lib paths, the `-DPLATFORM_NEXTUI` define, the extra `config.c` source, and the GLES link flags.

`-DPLATFORM` is compiled into the on-device `SYSTEM_PATH` and `USERDATA_PATH` (`.system/<PLATFORM>` and `.userdata/<PLATFORM>`), so it is driven by `WORKSPACE`. A `tg5040-nextui` binary therefore reports `tg5040` at runtime and resolves the same on-card paths as the device firmware.

The only source-level NextUI difference is in `minui-list.c`: under `-DPLATFORM_NEXTUI`, `PLAT_isOnline` is mapped to `PWR_isOnline`, which NextUI's SDK uses for online detection.

### GLES and audio libraries

NextUI toolchains install `libmsettings` and the GLES stack under `/opt/nextui`. Every NextUI target links `libsamplerate`, which `api.c` uses to resample audio. The linked libraries differ per device (`NEXTUI_GL_LIBS`):

- `tg5040-nextui` and `h700-nextui`: `-lGLESv2 -lsamplerate`
- `tg5050-nextui` and `my355-nextui`: `-lGLESv2 -lmali -lsamplerate` (their `libGLESv2` is a stub backed by a standalone mali blob that must be linked explicitly)

## Building

Build a NextUI variant with its platform id inside the matching toolchain:

```bash
PLATFORM=tg5040-nextui make setup
PLATFORM=tg5040-nextui make
```

This produces `minui-list-tg5040-nextui`.

## Testing the wiring

`tests/makefile.bats` asserts the per-platform Makefile wiring (upstream repo, version, workspace, `-DPLATFORM_NEXTUI`, device id, sources, and GLES libs) by introspecting the Makefile with `make print-<VAR> PLATFORM=<p>`. It needs neither a toolchain nor a cloned upstream tree:

```bash
bats tests/makefile.bats
```

The CI matrix builds every NextUI binary in its `savant/minui-toolchain:<device>-nextui` container, which is the integration test for the full compile and link.

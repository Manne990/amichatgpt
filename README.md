# AmiChatGPT

AmiChatGPT is an early native Amiga Workbench client for the ChatGPT64 bridge.

The app does not talk directly to OpenAI. It connects to a local ChatGPT64 bridge over plain TCP:

```text
AmiChatGPT -> ChatGPT64 bridge -> OpenAI API
```

This repository currently contains the first native GUI milestones: a small m68k Amiga executable, configuration loading, TCP bridge connection, basic prompt send/receive, host-side tests, and packaging that produces emulator-friendly artifacts.

## Target

- AmigaOS / Workbench 3.0 or 3.1
- m68k / 68000-compatible executable
- no FPU requirement
- TCP stack exposing `bsdsocket.library`
- ChatGPT64 running on another computer

See [REQUIREMENTS.md](REQUIREMENTS.md) for the full product and technical requirements.

## Local Host Test

Run the host build and smoke test:

```sh
make host-test
python3 -m unittest discover -s tests
```

This does not require an Amiga compiler.

## Build an Amiga Package with Docker

The CI build uses the `amigadev/crosstools:m68k-amigaos` Docker image for the AmigaOS 3.x m68k cross-compiler.

```sh
docker run --rm \
  -v "$PWD:/work" \
  -w /work \
  -e USER="$(id -u)" \
  -e GROUP="$(id -g)" \
  amigadev/crosstools:m68k-amigaos \
  bash -lc 'bash scripts/ci/build-amiga-package.sh'
```

Outputs are written to `dist/`:

- `dist/AmiChatGPT-0.1.0/` - mountable emulator drawer
- `dist/AmiChatGPT-0.1.0.adf` - Amiga disk image if `xdftool` is available
- `dist/AmiChatGPT-0.1.0.lha` - LHA archive if `lha` is available
- `dist/AmiChatGPT-0.1.0.tar.gz` - fallback host archive when `lha` is not available

The package includes `AmiChatGPT.info`, so Workbench can show the application icon next to the executable.
It also includes `Gadgets/textfield.gadget`, a third-party BOOPSI multiline text input gadget used by the prompt editor.

## Create an ADF Locally

Install `amitools` and run:

```sh
python3 -m pip install --user amitools
make adf
```

## Load in an Emulator

The simplest path is to download the GitHub Actions artifact and use one of these:

- Attach `AmiChatGPT-0.1.0.adf` as a floppy disk.
- Mount `AmiChatGPT-0.1.0/` as a directory or hard drive in FS-UAE, WinUAE, or Amiberry.
- Copy/extract `AmiChatGPT-0.1.0.lha` inside an Amiga environment.

Then run `AmiChatGPT` from Shell or open the application icon from Workbench. The current build opens a native, resizable Workbench window with a scrollable transcript, a multiline textfield input editor with vertical scrolling, and a Send button. It reads bridge settings from built-in defaults, `AmiChatGPT.conf`, Workbench ToolTypes, and CLI arguments. It connects to the configured ChatGPT64 bridge over TCP, sends the prompt as a terminal line, wraps long transcript lines for the Workbench window, keeps the latest 160 wrapped transcript lines, and appends the bridge reply to the transcript.

Third-party notice: `textfield.gadget` 3.1 is Copyright (C) 1995 Mark Thomas. See `ThirdParty/textfield-license.txt` in the package.

## Bridge

Run ChatGPT64 on the bridge computer in ASCII mode. AmiChatGPT switches the
bridge to normal reply mode after it connects, so replies are a little longer
than the C64 terminal default:

```sh
chatgpt64 start --terminal ascii --width 72
```

`--terminal c64` also works; AmiChatGPT filters PETSCII/control bytes before displaying the transcript, but this build intentionally keeps transcript rendering plain while the GUI stabilizes.

If AmiChatGPT is running in an emulator with UAE/bsdsocket networking on the same Mac, the packaged default `HOST=127.0.0.1` is usually correct. For a real Amiga or an emulated Amiga using a separate TCP stack, set `HOST` to the bridge computer's LAN IP address.

AmiChatGPT connects directly to `chatgpt64` on port `6464`. `tcpser` is only needed for C64/CCGMS modem-style clients, not for this native Amiga client.

## Configuration

Default settings are packaged in `AmiChatGPT.conf`:

```text
HOST=127.0.0.1
PORT=6464
WIDTH=72
MODE=normal
```

Configuration is applied in this order:

1. Built-in defaults
2. `PROGDIR:AmiChatGPT.conf`
3. Workbench icon ToolTypes
4. Shell CLI arguments

Supported Shell examples:

```sh
AmiChatGPT HOST=127.0.0.1 PORT=6464 WIDTH=72
AmiChatGPT --host 127.0.0.1 --port 6464 --width 72
AmiChatGPT HOST=192.168.1.50 PORT=6464 WIDTH=72
AmiChatGPT --mode long
```

Supported Workbench ToolTypes:

```text
HOST=127.0.0.1
PORT=6464
WIDTH=72
MODE=normal
```

`MODE` controls the ChatGPT64 bridge reply length for this client. Supported
values are `short`, `normal`, and `long`. The packaged default is `normal`,
while C64/CCGMS sessions still start in the bridge's short-answer mode.

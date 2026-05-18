# AmiChatGPT

AmiChatGPT is an early native Amiga Workbench client for the ChatGPT64 bridge.

The app does not talk directly to OpenAI. It connects to a local ChatGPT64 bridge over plain TCP:

```text
AmiChatGPT -> ChatGPT64 bridge -> OpenAI API
```

This repository currently contains the first native GUI milestones: a small m68k Amiga executable, configuration loading, host-side tests, and packaging that produces emulator-friendly artifacts.

## Target

- AmigaOS / Workbench 3.0 or 3.1
- m68k / 68000-compatible executable
- no FPU requirement
- future network support through `bsdsocket.library`
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

Then run `AmiChatGPT` from Shell or open the application icon from Workbench. The current build opens a native, resizable Workbench window with a scrollable transcript, a multiline textfield input editor with vertical scrolling, and a Send button. It reads bridge settings from built-in defaults, `AmiChatGPT.conf`, Workbench ToolTypes, and CLI arguments. It is still an offline prototype; bridge networking comes next.

Third-party notice: `textfield.gadget` 3.1 is Copyright (C) 1995 Mark Thomas. See `ThirdParty/textfield-license.txt` in the package.

## Bridge

Run ChatGPT64 on the bridge computer in ASCII mode:

```sh
chatgpt64 start --terminal ascii --width 72
```

The first networked version will connect to the bridge host and port shown in the transcript at startup.

## Configuration

Default settings are packaged in `AmiChatGPT.conf`:

```text
HOST=192.168.1.50
PORT=6464
WIDTH=72
```

Configuration is applied in this order:

1. Built-in defaults
2. `PROGDIR:AmiChatGPT.conf`
3. Workbench icon ToolTypes
4. Shell CLI arguments

Supported Shell examples:

```sh
AmiChatGPT HOST=192.168.1.50 PORT=6464 WIDTH=72
AmiChatGPT --host 192.168.1.50 --port 6464 --width 72
```

Supported Workbench ToolTypes:

```text
HOST=192.168.1.50
PORT=6464
WIDTH=72
```

# AmiChatGPT

AmiChatGPT is an early native Amiga Workbench client for the ChatGPT64 bridge.

The app does not talk directly to OpenAI. It connects to a local ChatGPT64 bridge over plain TCP:

```text
AmiChatGPT -> ChatGPT64 bridge -> OpenAI API
```

This repository currently contains the first native GUI milestone: a small m68k Amiga executable, host-side tests, and packaging that produces emulator-friendly artifacts.

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

Then run `AmiChatGPT` from Shell or open the application icon from Workbench. The current build opens a native, resizable Workbench window with a scrollable transcript, a three-line input area, and a Send button. It is still an offline prototype; bridge networking comes next.

## Bridge

Run ChatGPT64 on the bridge computer in ASCII mode:

```sh
chatgpt64 start --terminal ascii --width 72
```

The first networked version will connect to the bridge host and port defined in ToolTypes or CLI arguments.

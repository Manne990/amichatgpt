# AmiChatGPT Requirements

AmiChatGPT is a native Amiga Workbench client for the ChatGPT64 bridge. It should feel like a small internet client that could plausibly have existed on a well-equipped Amiga in the late 1990s.

## Goal

Build the simplest useful Workbench chat client:

```text
+--------------------------------------+
| Scrollable chat transcript           |
| read-only                            |
|                                      |
+--------------------------------------+
| [ multiline prompt editor        ] [Send]
+--------------------------------------+
```

The Amiga app does not talk directly to OpenAI. It connects to the local ChatGPT64 bridge over plain TCP.

```text
AmiChatGPT -> ChatGPT64 bridge -> OpenAI API
```

## Target Systems

Primary target:

- AmigaOS / Workbench 3.0 or 3.1
- m68k executable
- 68000-compatible CPU target
- no FPU requirement
- bsdsocket.library-compatible TCP stack

Realistic machines:

- Amiga 1200
- Amiga 3000 / 4000
- accelerated Amiga 500 / 2000
- emulator setups such as FS-UAE, WinUAE, Amiberry
- PiStorm, Vampire, THEA500-like environments

Not a primary target for the first version:

- stock Amiga 500 with Workbench 1.3
- AmigaOS 4 / PPC
- MorphOS / AROS

## Runtime Dependencies

Required on the Amiga:

- Workbench 3.0/3.1-compatible environment
- bundled `Gadgets/textfield.gadget` for multiline prompt input
- TCP stack exposing `bsdsocket.library`
- enough RAM for a small GUI and network client

Known TCP stack options:

- Roadshow
- AmiTCP
- Miami / MiamiDX
- emulator-provided networking

Required on the bridge computer:

- ChatGPT64 installed and configured
- OpenAI API key configured through `chatgpt64 setup`
- ChatGPT64 running in ASCII mode

Recommended bridge command:

```sh
chatgpt64 start --terminal ascii --width 72
```

## Programming Language

Use C for the first version.

Reasons:

- AmigaOS APIs are C-oriented
- Intuition, GadTools, Workbench startup, and bsdsocket examples are usually C
- small binaries
- good fit for m68k/68000 targets
- can be cross-compiled on macOS
- keeps a path open for building on a real Amiga later

Style target:

- conservative C, preferably C89/C90-friendly where practical
- avoid FPU assumptions
- keep modules small and easy to port

## Build Environment

Primary development environment:

- macOS
- modern editor such as VS Code
- m68k Amiga cross-compiler
- emulator for testing

Preferred compiler:

- `m68k-amigaos-gcc`, preferably the bebbo toolchain

Possible future/native-Amiga compilers:

- VBCC
- SAS/C
- Dice C

Native Amiga editors that may be useful later:

- CygnusEd / CED
- GoldED
- TurboText
- MicroEMACS
- Annotate

## GUI Requirements

First version should use native Amiga UI concepts:

- Intuition window
- GadTools where practical
- read-only scrollback area
- multiline prompt editor using bundled `textfield.gadget`
- vertical prompt scrollbar only
- line wrapping in the prompt editor
- prompt text limited by maximum character count, not by line count
- Send button
- close gadget
- keyboard editing in the input area

Preferred baseline:

- GadTools / Intuition for Workbench 3.x

Avoid in the first version:

- MUI dependency
- ReAction dependency
- `texteditor.gadget` dependency
- custom skinning
- rich text rendering

## Network Requirements

The app connects to the ChatGPT64 bridge over plain TCP.

Default settings:

```text
HOST=192.168.1.50
PORT=6464
```

Actual host should be configurable.

Configuration sources:

- `PROGDIR:AmiChatGPT.conf`
- Workbench icon ToolTypes
- CLI arguments
- built-in defaults

Suggested ToolTypes:

```text
HOST=192.168.1.50
PORT=6464
WIDTH=72
```

The app sends one user-entered prompt followed by CR/LF and reads the bridge reply.

Protocol expectations:

- plain TCP
- no TLS
- no JSON
- no OpenAI API key on the Amiga
- no direct OpenAI API calls

## MVP Milestones

### M1: Local GUI Prototype

- open a Workbench window
- show a scrollable transcript area
- show a multiline prompt editor backed by `textfield.gadget`
- show a Send button
- append local test text to the transcript
- no networking yet

### M2: Configuration

- parse CLI arguments
- parse `PROGDIR:AmiChatGPT.conf`
- parse Workbench ToolTypes
- support `HOST`, `PORT`, and `WIDTH`
- display config errors in the transcript

### M3: TCP Connect

- open `bsdsocket.library`
- connect to the ChatGPT64 bridge
- show connected/disconnected status
- handle missing TCP stack gracefully

### M4: Send and Receive

- send a prompt from the input editor
- receive reply text from the bridge
- append incoming text to the transcript
- keep the UI responsive enough for basic use

### M5: Usability Pass

- keyboard send action
- better error messages
- scrollback limits
- reconnect command/button
- simple About text

## Non-Goals for the First Version

- direct OpenAI API integration
- OAuth or API key handling on the Amiga
- markdown rendering
- images
- streaming token UI
- full ANSI/PETSCII terminal emulation
- Workbench 1.3 support
- OS4/PPC-specific build

## Open Questions

- Exact minimum RAM target
- Best scrollback implementation: GadTools list view vs custom text area
- Whether to use blocking sockets with polling or a more asynchronous event model
- Best emulator setup for repeatable tests on macOS
- Whether to package as a single executable plus `.info` icon or a drawer with docs and examples

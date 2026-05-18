AmiChatGPT 0.1.0
================

This is an early GUI build for the AmiChatGPT Workbench client.

The current executable opens a native, resizable Workbench window with a
scrollable transcript, a multiline textfield input editor, and a Send button.
It reads bridge configuration and tries to connect to the configured
ChatGPT64 bridge over TCP. It sends prompts as terminal lines, wraps long
transcript lines for the Workbench window, and appends bridge replies to the
transcript.

Target:
- AmigaOS / Workbench 3.0 or 3.1
- m68k / 68000-compatible CPU
- no FPU requirement

This build connects to the ChatGPT64 bridge over plain TCP and reports the
connection status in the transcript. It connects directly to chatgpt64 on
port 6464; tcpser is only needed for C64/CCGMS modem-style clients.

Recommended bridge command while the GUI stabilizes:

  chatgpt64 start --terminal ascii --width 60

The C64 terminal mode also works; AmiChatGPT filters PETSCII/control bytes
before displaying the transcript.

Bundled runtime:
- Gadgets/textfield.gadget provides the multiline prompt editor.
- textfield.gadget 3.1 is Copyright (C) 1995 Mark Thomas.
- See ThirdParty/textfield-license.txt.

Run from Shell:

  AmiChatGPT

Configuration:

  AmiChatGPT.conf is read from PROGDIR first.

  Workbench ToolTypes are also supported:

    HOST=127.0.0.1
    PORT=6464
    WIDTH=72

  Shell examples:

    AmiChatGPT HOST=127.0.0.1 PORT=6464 WIDTH=72
    AmiChatGPT --host 127.0.0.1 --port 6464 --width 72
    AmiChatGPT HOST=192.168.1.50 PORT=6464 WIDTH=72

Default bridge settings:

  HOST=127.0.0.1
  PORT=6464
  WIDTH=72

Use 127.0.0.1 when AmiChatGPT runs in an emulator with UAE/bsdsocket
networking on the same computer as chatgpt64. Use the bridge computer's LAN
IP address for a real Amiga or a separate Amiga TCP stack.

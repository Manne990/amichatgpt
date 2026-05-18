AmiChatGPT 0.1.0
================

This is an early GUI build for the AmiChatGPT Workbench client.

The current executable opens a native, resizable Workbench window with a
scrollable transcript, a multiline textfield input editor, and a Send button.
It reads bridge configuration and tries to connect to the configured
ChatGPT64 bridge over TCP. Sending prompts and receiving replies come in the
next milestone.

Target:
- AmigaOS / Workbench 3.0 or 3.1
- m68k / 68000-compatible CPU
- no FPU requirement

This build connects to the ChatGPT64 bridge over plain TCP and reports the
connection status in the transcript.

Bundled runtime:
- Gadgets/textfield.gadget provides the multiline prompt editor.
- textfield.gadget 3.1 is Copyright (C) 1995 Mark Thomas.
- See ThirdParty/textfield-license.txt.

Run from Shell:

  AmiChatGPT

Configuration:

  AmiChatGPT.conf is read from PROGDIR first.

  Workbench ToolTypes are also supported:

    HOST=192.168.1.50
    PORT=6464
    WIDTH=72

  Shell examples:

    AmiChatGPT HOST=192.168.1.50 PORT=6464 WIDTH=72
    AmiChatGPT --host 192.168.1.50 --port 6464 --width 72

Default bridge settings:

  HOST=192.168.1.50
  PORT=6464
  WIDTH=72

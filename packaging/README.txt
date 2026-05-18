AmiChatGPT 0.1.0
================

This is an early GUI build for the AmiChatGPT Workbench client.

The current executable opens a native, resizable Workbench window with a
scrollable transcript, a custom three-line input editor, and a Send button. It is still offline:
bridge configuration and TCP networking come in the next milestones.

Target:
- AmigaOS / Workbench 3.0 or 3.1
- m68k / 68000-compatible CPU
- no FPU requirement

Future versions will connect to the ChatGPT64 bridge over plain TCP.

Run from Shell:

  AmiChatGPT

Default bridge settings for the future network client:

  HOST=192.168.1.50
  PORT=6464
  WIDTH=72

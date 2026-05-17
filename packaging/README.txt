AmiChatGPT 0.1.0
================

This is an early scaffold build for the AmiChatGPT Workbench client.

It is not the full GUI yet. The current executable verifies that the
Amiga m68k build, package, and emulator artifact pipeline works.

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


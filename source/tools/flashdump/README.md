# MS2000 Flash Tool (Windows)

Reads the complete 1 MB flash of a **KORG MS2000 / MS2000R** over MIDI - the `flash.bin` the emulator needs.
It is a Windows port, with a window, of the flash dump by **unknown-technologies**
(<https://github.com/unknown-technologies/ms2000>, `fwpatch` + `fwdump`, GPL-3.0): their small routine is added
to KORG's own v1.07 firmware update as a new command of the update mode, installed like a normal update, and then
answers "send me these bytes of the flash".

> **MS2000B / MS2000BR: NOT compatible.** Other hardware and other firmware. Never use this tool with them - it can
> make the unit unusable.

## What you need
- An MS2000 or MS2000R, a MIDI interface, two MIDI cables (computer OUT -> unit MIDI IN, unit MIDI OUT -> computer IN).
- KORG's MS2000 system updater v1.07 (from korg.com): the file `x811v107.sys` in it. The tool checks it by SHA-1
  (`945a7555...`) and refuses any other file.

## Steps
1. Switch the unit off. Hold **WRITE** and the arpeggiator's **TYPE** key, switch it on: the LCD shows `IPL s.p.u`
   (update mode). Let go of the keys.
2. Start the tool, tick the MS2000 / MS2000R confirmation, choose the MIDI IN and OUT ports, **1. Check the unit**
   (it shows the System / PCM / User versions).
3. **Choose KORG's x811v107.sys**, then **2. Install the flash-dump update** (about 2 minutes).
   - Do not switch the unit off and do not unplug MIDI while it runs.
   - If anything fails: do NOT switch off - press the install button again; the whole update is repeated.
4. Switch the unit off, then on again in update mode (WRITE + TYPE), **1. Check the unit** again.
5. **3. Read the whole flash** (about 10 minutes) and save it as `flash.bin`. The tool checks that the system area it
   read back is exactly the update it installed.

The flash-dump command lives only in the update mode; the synthesizer works as before. **Restore the original v1.07**
installs KORG's unchanged file again if you want it gone.

## How it was made safe
- The update mode was disassembled and documented first (`docs/ipl_protocol.md`, KORG's `vup_ms2000.exe` host code
  read too): message formats, packing, checksum, the lazy per-sector erase, and the risky moment (block 0 = the boot
  code; it is written first, as KORG does).
- Only KORG's v1.07 and exactly its flash-dump version (SHA-1 `cd85d579...`) can be sent. The patch is unknown's
  `fwpatch.asm`, assembled with his `h8asm` and checked back with a disassembler; the image's word checksum is kept 0.
- Before any real unit: the whole route ran on the emulated MS2000R (`ipl_e2e`): install -> the emulated flash equals
  the image byte for byte; a damaged block -> checksum error, then the whole update again -> OK; a normal power-on
  boots the synth; update mode again -> the 1 MB read back equals the emulated flash. And through real Windows MIDI
  (loopMIDI) with the tool itself and the bridge below.

## Trying it without a unit (loopMIDI)
`MS2000IplBridge` runs the emulated MS2000R in update mode on two MIDI ports. With loopMIDI use **two separate
ports**, one per direction (one port carries both and would feed back):

    MS2000IplBridge --image work_flash.bin --in "loopMIDI Port" --out G1

In the tool: OUT = `loopMIDI Port`, IN = `G1`. `work_flash.bin` is made from `flash.bin` (never written) and keeps
every change; starting the bridge again is the power cycle.

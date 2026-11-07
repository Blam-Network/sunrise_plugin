# Destiny launch modes

Retail Destiny (title `415608F8`) now has two launch modes. You pick one by holding LB when the game starts.

| At launch | What loads | Plugin does |
|---|---|---|
| LB held | Disc xex, TU0/day0 (`0x53B5D6BA`). The title update is skipped for this launch only. | Blam Network: version spoof, LSP hooks, spoof hooks, XHttp hooks, content aggregate hook, Live hive hook, manifest byte `0x833DB860`, and UnlockDiagnostics if it is on. |
| LB not held | Installed title update 2.3.1 (`default_ttk_231`, `0x579810CC`) | No LSP, spoof, XHttp or other Blam hooks. The game uses Bungie's release servers. Only the packet capture hooks go in, and they record nothing until you start a capture (see below). |

The `[DESTINY_OPTIONS] Spoof` ini key is gone; LB replaces it. `UnlockDiagnostics` stays. The pre-alpha (`41560907`) and the Halo titles work as before.

The XNotify shows which mode was picked:
- `Destiny: Blam Network (TU skipped)`
- `Destiny: release servers`
- `Destiny: TU skip failed - release servers`: LB was held but 2.3.1 still loaded.
- `Destiny: unknown xex - no patches`

## How LB is read

`XexLoadExecutableHook` (CoreHooks.cpp) wraps XAM's import of the kernel's `XexLoadExecutable`. It runs before the image is loaded. It reads the pads as follows:

1. It calls `XInputGetState` for users 0-3. If any pad has `XINPUT_GAMEPAD_LEFT_SHOULDER` set, LB counts as held.
2. If no pad answered at all, it tries again with `XamInputGetState(user, XINPUT_FLAG_SYSTEMAPP | XINPUT_FLAG_GAMEPAD)`. The hook runs in XAM's context, not the title's, so the system-app read is the backup.
3. If neither read works, LB counts as released, so the launch is a normal release launch.

The hook reads the xex file only when LB is held. It opens the path XAM passed with `NtOpenFile`, which resolves the same way the kernel will. It then reads the plain XEX2 optional header table and takes the title id from `XEX_HEADER_EXECUTION_ID`. Blam mode is requested (`bDestinyBlamRequested`) only when the title id is `415608F8`. Other titles never have their file read, and nothing changes for them.

## How the title update is skipped

How the console applies a title update (XDK doc "Title Updates on Xbox 360", in `xbox360sdk.chm`):
- At launch, "the system searches all predefined installation locations for the title update package with the highest version", then "applies a differential patch to the executable in memory".
- The package is mounted as a package device. PIX shows reads of `\Device\Package_<hash>\default.xexp`.
- Titles reach the package through "UPDATE:, a symbolic drive" (`xbdir update:\` checks that it loaded).
- "If an update has been removed ... the original version of the title will be run". So running the base xex with a TU installed is a supported state.

Xenia works the same way: it opens the TU content as `UPDATE`, then loads `update:\<xex path relative to game:>p` and applies it in `xeXexLoadImage` (`kernel_state.cc` `LoadTitleUpdate`, `xboxkrnl_modules.cc`).

So the plugin removes the `update:` symbolic link just for our call:
1. `LoadWithoutTitleUpdate` opens `\??\update:` and `\System??\update:` with `NtOpenSymbolicLinkObject`. It saves each target (`NtQuerySymbolicLinkObject`) and deletes the link (`ObDeleteSymbolicLink`).
2. It calls the real `XexLoadExecutable`. With the drive gone, the kernel finds no `default.xexp`, so the disc image loads as is. The minimum version argument is passed as 0 in this case, because the disc image is older on purpose.
3. It recreates every link it removed (`ObCreateSymbolicLink`) with the same target. XAM still owns the mount, and unmounts it when the title exits. Nothing is deleted from disk.
4. If the load fails with the links hidden, it retries once as a normal load, so a failed skip still launches the game (as 2.3.1).

`SetupHaloPatches` still chooses by the loaded xex's timestamp, so it acts on what actually loaded:
- `0x53B5D6BA` with LB requested: Blam Network hooks.
- `0x53B5D6BA` without LB (no TU installed): left alone.
- `0x579810CC`: no Blam hooks. The packet capture hooks are installed here (see Packet capture).
- Any other timestamp: left alone.

An LB launch also resets `LastTitleId`. Without that, launching Destiny straight from Destiny would skip setup.

Why this method:
- It is the smallest change. Nothing on disk is touched. Aurora and FSD, by contrast, enable and disable TUs by moving the TU files around (`Content\0000000000000000\<TID>\000B0000` or `Cache\TU_*`).
- The change lasts only for the load call, and it is undone right after.
- No kernel or XAM code is patched.
- TU0 has no `UPDATE:` string (checked in the day0 IDB, IDA :13340), so restoring the drive can't feed it 2.3.1 files. 2.3.1 does read `UPDATE:\` (`0x820058A4`), and a release launch leaves the drive untouched.

## Risks

- **Not verified on a console.** The kernel's own xexp lookup has not been disassembled; there is no kernel dump here. The method assumes that lookup goes through the `update:` link, as the XDK doc and Xenia suggest. If the kernel gets the package some other way, or XAM mounts the package after `XexLoadExecutable`, the TU still applies. You then get `TU skip failed - release servers` with no Blam hooks, which is safe.
- XAM might check after the load that the running version matches the TU, or it might prompt for the update on Live. This is unknown. TU0 used to run with no TU installed, which suggests it is fine.
- For the length of the load, anything else that resolves `update:` fails. Only the launching title uses that drive.
- If `ObCreateSymbolicLink` fails, `update:` stays missing until the title exits. That doesn't matter for TU0, and the log shows the status.
- The pad read runs on XAM's launch thread. If the controller isn't ready yet, LB reads as released and you get a release launch. Hold LB before pressing A and keep holding it until the notify shows.
- A direct Destiny to Destiny relaunch without LB would normally keep the previous `LastTitleId` guard. Because the capture hooks lived in the old image, `XexLoadExecutableHook` clears `LastTitleId` when they were installed, so 2.3.1 is set up again.

## Packet capture (2.3.1 only)

While 2.3.1 runs on the release servers, hold **L3 + R3 + Y** and tap **LB** to start a plaintext capture. We assume the same combo stops it. The capture covers BAP frames (which include the activity host traffic) and bdXHTTP requests and responses. Nothing is patched in the game until you start a capture, so a load with no Xbox Live sign-in is untouched. It is saved as `packets\<yyyy-mm-dd>_<hh-mm-ss>.pcap` next to the plugin. Start and end each show one XNotify: `Capture started: packets\<file>.pcap`, then `Capture saved: <file> (n sent, n received, n dropped)`, whether the capture ended by the combo, a title change or an error. `[DESTINY_OPTIONS] PacketCapture = false` removes the hooks. `CaptureHooksBap` and `CaptureHooksXHttp` switch the two hook groups for bisecting a crash. The format, the hooks and the decoder (`web-tiger/packets/tools/sunrise-capture.mjs`) are in `capture-format.md`.

Log lines:
```
[Sunrise3] Destiny retail ttk_231 (Jul 2016) detected, release servers
[Sunrise3] [capture] worker thread created (tid ........)
[Sunrise3] [capture] ready (nothing hooked yet): hold L3 + R3 + Y and tap LB to start/stop
[Sunrise3] [capture] worker started
[Sunrise3] [capture] pad 0 read with XInputGetState
[Sunrise3] [capture] ring buffer 4096 KB
[Sunrise3] [capture] started: packets\2026-09-30_18-42-07.pcap
[Sunrise3] [capture] 120 sent, 340 received, 0 dropped, 512 KB written      (every 10 s)
[Sunrise3] [capture] stopped (pad combo): packets\2026-09-30_18-42-07.pcap, 120 sent, 340 received, 0 dropped, 0 faults, ... bytes
```
The stop reason is `pad combo`, `title gone`, `hooks off` (another executable loaded) or `write failed`. If a prologue doesn't match, the log shows `[capture] <name> at <addr>: prologue ... not the expected one, not hooking`. If the send or receive hook is missing, the log shows `BAP send/receive hooks missing, capture unavailable`.

## What to look for in the plugin log

LB launch that works:
```
[Sunrise3] XexLoadExecutable \Device\...\default.xex flags 00000001 version XXXXXXXX
[Sunrise3] Launch pad 0 buttons 0100
[Sunrise3] LB held at launch of title 415608F8
[Sunrise3] Destiny: Blam Network requested, skipping the title update
[Sunrise3] \??\update: -> \Device\Package_... hidden        (one or both namespaces)
[Sunrise3] \System??\update: not mounted (C0000034)          (either may be absent)
[Sunrise3] \??\update: -> \Device\Package_... restored (00000000)
[Sunrise3] XexLoadExecutable result 00000000 module ...
[Sunrise3] Loaded title 415608F8 v ...
[Sunrise3] Destiny TU0 detected, Blam Network
```
Then the usual TU0 lines (`RegisterBungieServer`, `Destiny Live hive query`, `[diag]`) and the notify `Destiny: Blam Network (TU skipped)`.

Normal launch: `Launch pad N buttons ...` without `0100`, then `Destiny retail ttk_231 (Jul 2016) detected, release servers` and `Destiny: release servers`. There should be no `RegisterBungieServer`, `XNetStartup hook` or `Live hive` lines.

If it fails, report these lines:
- `No update: drive to hide, loading as is`: the TU wasn't mounted as `update:` before the load. Another mechanism is needed.
- Both links `hidden`, then `LB was held but the title update still loaded`: the kernel doesn't use the link.
- `Load without title update failed (XXXXXXXX), loading normally`: the kernel or XAM refused the base image. Report the status code.
- `No pad state readable at launch`: neither input read worked in this context.
- `Can't open ... for its title id`: the path XAM passed isn't openable from here. Report the path.

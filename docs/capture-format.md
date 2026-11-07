# Destiny 2.3.1 plaintext capture

The plugin can record Destiny's network traffic in the clear while you play title update 2.3.1 (`default_ttk_231`, xex timestamp `0x579810CC`, title `415608F8`) on Bungie's release servers. It records BAP frames, which also carry the activity host traffic (wire 6/7/8/9/100), and the bdXHTTP requests. 2.3.1 has no Demonware lobby, so there is nothing else to record.

Code: `Sunrise2/DestinyCapture.cpp`. Addresses and research: web-tiger-day0 `docs/ttk231-capture-hooks.md` (IDA :13341). Decoder: web-tiger `packets/tools/sunrise-capture.mjs`.

## Using it

1. Launch Destiny without holding LB, so 2.3.1 loads. The log shows `[capture] ready (nothing hooked yet)`. Nothing is patched in the game until you start a capture, so a load with no Xbox Live sign-in is safe.
2. Hold **L3 + R3 + Y** and tap **LB** to start. You get an XNotify with the file name: `Capture started: packets\2026-09-30_18-42-07.pcap`. While it runs, `capturing packets (n sent, n received)` is drawn top left and the pre-alpha watermark along the bottom (see Overlay).
3. Do the same again to stop. We assume the same combo stops it; nothing else in the spec says how. You get `Capture saved: 2026-09-30_18-42-07.pcap (n sent, n received, n dropped)`.

The game also sees these buttons, so a grenade or melee may go off. Doing it in orbit or a menu avoids that.

The file goes in a `packets` folder next to the plugin, for example `Hdd:\Plugins\packets\` or `Usb0:\Plugins\packets\`. The plugin reaches that folder through its `SR3:` mount, which `GetMountPath`/`MountPath` point at the plugin's own folder. If two captures start in the same second, the second one gets `_2` added to its name.

A capture also stops on its own when the title exits, when another executable loads, or when a write fails.

### Settings (`Sunrise3.ini`, `[DESTINY_OPTIONS]`)

| Key | Default | What it does |
|---|---|---|
| `PacketCapture` | `true` | Installs the capture hooks. `false` leaves 2.3.1 untouched, as it was before this feature. |
| `CaptureHooksBap` | `true` | Installs the four BAP hooks. `false` skips them, which also leaves capture unavailable. For bisecting a crash. |
| `CaptureHooksXHttp` | `true` | Installs the three bdXHTTP hooks. For bisecting a crash. |
| `CaptureOverlay` | `true` | Draws the counters and the pre-alpha watermark into the frame while capturing (see Overlay). `false` shows the counts as a periodic top-left toast instead. |

## Hooks

The hooks below are installed only while a capture runs. Nothing is patched at title load: on the combo the plugin builds a trampoline for each hook and writes the jump, and on stop it restores the original bytes. Before each install the hook's first four instructions are compared with the words below (checked in IDA); if they don't match, that hook is skipped and the log says so, and if a required BAP hook (send or receive) can't go in the capture doesn't start.

| Address | Function (descriptive name) | Prologue | When it records |
|---|---|---|---|
| `0x82D035D8` | `bap_frame_encode` | `mflr; bl __savegprlr_25` | Before the call, while the body is still plaintext. The GCM step encrypts it in place afterwards. Records the header from `msg+0x90`, the body from `*(msg+0x118)`, and the envelope kind (0 plain, 1 GCM, 2 secure hello). It skips calls that the encoder is about to reject (header too big for the 128-byte buffer), so a retried frame is recorded once. |
| `0x82CC56C8` | `bap_recv_deliver` (rx state 7) | `mflr; bl __savegprlr_25` | Before the call: the header from `conn+0x167C` and the body from `*(conn+0x1790)`, already decrypted. |
| `0x82CC5F90` | `bap_connection_connect` | `mflr; bl __savegprlr_29` | After the call, when the state changed: a meta record with the address. |
| `0x832750E0` | `bap_channel_set_key` | `mflr; bl __savegprlr_28` | Before the call: key, base IV, `is_server`. |
| `0x831685A0` | `bdXHTTP::httpOpStatusCallback` | `mflr; bl __savegprlr_29` | Before the call. `READ_COMPLETE` gives a response body chunk. `HEADERS_AVAILABLE`, `REQUEST_ERROR`, `SENDREQUEST_COMPLETE` and `WRITE_COMPLETE` give status events. |
| `0x83168E08` | `bdXHTTP::sendXHTTPRequest` | `mflr; stw; std; stwu` (no branch) | After the call, when it returned TRUE, so the request handle exists. |
| `0x8336AF28` | XHttpWriteData wrapper | four `mr` | Before the call: the request body chunk. |

None of these use `PatchInJump`. The trampolines are built with `Detour`'s helpers (`CopyInstruction` / `WriteFarBranch`) into a fixed, cache-line-aligned slot per hook, rebuilt on each start, so the shared `Detour` buffer is never used and nothing grows across start/stop cycles. The prologue's second word is allowed to be any `bl`: the console's `default_ttk_231.xex` is a different link of the same code than the IDA copy, with the compiler's `__savegprlr` save/restore thunks consolidated at the end of the image, so the `bl __savegprlr_N` in these prologues points at a different address (and one prologue uses the thunk where IDA's build inlined the LR store). The relative `bl` is relocated into the trampoline as an absolute far-call either way. The function bodies match, so the struct offsets hold, but each hook's read path is wrapped in SEH: a wrong offset drops the frame and counts a fault rather than faulting a game thread. Every install and removal flushes the data cache and the patched site (`dcbst` + `sync` + `isync`).

On stop the plugin marks capture inactive, waits briefly so any in-flight hook call finishes and drops the ring lock, then restores the original bytes and closes the file; the ring stays allocated. When a new executable loads, the same stop runs first (from `XexLoadExecutableHook`) so the bytes are restored while the outgoing image is still mapped, and `LastTitleId` is cleared so the worker re-arms for the next title.

### Threads

- While a capture runs, the hooks run on the game's threads. BAP runs on Tiger job workers (`network_send` / `network_update`), and bdXHTTP runs on its bd thread. A hook checks a flag, validates its pointers (`MmIsAddressValid` on the first and last byte), copies the record into the ring under a critical section, and returns. It does no file I/O and no CRT formatting. If a game buffer disappears during the copy, an SEH guard drops that record and counts a fault.
- The ring is 4 MB, or 2/1/0.5 MB if the allocation fails. It is allocated the first time a capture starts and kept for the life of the plugin. When the ring is full, records are dropped and counted, and a `dropped` meta record says how many.
- One plugin worker thread (the `ExCreateThread` pattern from `DestinyDiag.cpp`) is started at title load and does the rest. It is the only thing that runs before a capture: every 16 ms it reads pads 0-3 with `XInputGetState`, falling back to `XamInputGetState` as a system app, which is safe offline. It detects the combo on the LB press edge while L3, R3 and Y are held on the same pad, with 750 ms between toggles, and on the combo it installs the hooks, opens the file and starts, then on the next combo (or a title change or a write error) removes the hooks and closes the file. It drains the ring (up to 1 MB each tick) and calls `FlushFileBuffers` once a second so a USB stick shows the data.

## File format

Classic pcap, little-endian, microsecond timestamps: magic `0xA1B2C3D4`, version 2.4, snaplen 262144, link type `LINKTYPE_USER0` (147). Wireshark opens it as "USER0" data.

Each packet record holds one capture record:

```
pcap record header (16 bytes)   ts_sec, ts_usec, incl_len, orig_len
capture record header (40 bytes)
header bytes                    header length bytes
body bytes                      body length bytes
```

`orig_len` is the size the record would have had without truncation.

### Capture record header (all fields little-endian)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `SRCP` (ASCII) |
| 4 | 2 | version, 1 |
| 6 | 2 | size of this header, 40. Header bytes start here. |
| 8 | 8 | timestamp in microseconds since 1970-01-01 UTC. The pcap record has the same value. The console clock is read once at start, and the CPU timebase (`mftb`) is used after that. |
| 16 | 1 | direction: 0 send (console to remote), 1 receive, 2 local event |
| 17 | 1 | channel: 0 meta, 1 BAP, 2 XHttp |
| 18 | 2 | flags, see below |
| 20 | 4 | connection. BAP: the connection index (`conn+0x10F4`, as in "BAP connection %d"). XHttp: the request handle. Meta: the BAP connection index for key and connect records, otherwise 0. |
| 24 | 4 | remote IPv4 as a.b.c.d bytes, or 0.0.0.0 if unknown |
| 28 | 2 | remote port, or 0 if unknown |
| 30 | 2 | type. BAP: the wire type (`msg+0xC` on send, `conn+0x1700` on receive). XHttp and meta: the kind, see below. |
| 32 | 4 | header length |
| 36 | 4 | body length |

Flags:
- bits 0-1: BAP envelope kind (0 plain, 1 GCM, 2 secure hello). Only valid when bit 2 is set.
- bit 2: the envelope kind is known. Only BAP sends set it.
- bit 3: truncated. The bytes stored are shorter than the frame; `orig_len` has the real size. Headers are cut at 4 KB, and whole records at the 256 KB snaplen.
- bit 4: unreadable. A pointer failed validation, so no bytes were stored.

### BAP (channel 1)

The header bytes followed by the body bytes are the frame plaintext. This is the same buffer that `decode.mjs` gets after GCM: a u16 big-endian wire type, then the wire's own header, then the body. There is no 6-byte envelope and no GCM tag.

The secure hello frames (wire 25/26, envelope kind 2) are the exception. Their body is still the hello cipher text (AES-CBC and HMAC with the signon ticket keys). The plaintext is only available in `hello_encrypt` / `hello_decrypt` (`0x82CCA570` / `0x82CCA450`), which the plugin doesn't hook.

### XHttp (channel 2)

| Type | Direction | Header | Body |
|---|---|---|---|
| 1 request | send | `VERB path`, `Host: host:port` and `X-Connect-Flags: 0x...` (the XHttpConnect flags, `op+0x650`), CRLF separated | the extra headers string the game passed to XHttpSendRequest (`op+0x7D4`) |
| 2 request body | send | none | one XHttpWriteData chunk |
| 3 response body | receive | none | one READ_COMPLETE chunk |
| 4 status | local | none | u32 status, then up to 16 bytes of the callback's status info |

HTTPS is handled inside XAM, so these bytes are plaintext. Response status lines and headers are not recorded; that would need an `XHttpQueryHeaders` call, which the hook doesn't make. The connection field is the request handle, which ties a request to its body and responses.

### Meta (channel 0)

| Type | Body |
|---|---|
| 1 start | text: title, xex timestamp, plugin version, file name, timebase frequency |
| 2 stop | text: sent, received, dropped, faults, reason |
| 3 BAP key | key[16], base IV[12], is_server u8, 3 pad bytes, channel pointer u32 LE. Connection, IP and port are filled in. |
| 4 dropped | text: the dropped count so far |
| 5 BAP connect | header text `a.b.c.d:port state N` (2 connecting, 9 failed). Connection, IP and port are filled in. |

The key record is there so a capture can still be checked against a wire capture with the old `decode.mjs`. `sunrise-capture.mjs --keys` writes `destiny_bap_keys.txt` lines from it. To build the IVs it follows `bap_channel_set_key`: the send IV (client) or receive IV (server) gets its last byte flipped, and each GCM frame adds one to its IV as a little-endian counter from byte 0 (`sub_83274D28`). When `byte_837CF418` is set, the game calls the hash-only path `sub_82D03578` instead, and no key record appears.

## Decoding

```
node packets/tools/sunrise-capture.mjs <file.pcap>                  summary and AH timeline
node packets/tools/sunrise-capture.mjs <file.pcap> --out <dir>      dump.txt ah_timeline.txt decode.txt xhttp.txt rsat/
node packets/tools/sunrise-capture.mjs <file.pcap> --keys <file>    destiny_bap_keys.txt for decode.mjs
node packets/tools/sunrise-capture.mjs --self-test                  synthetic round trip
```

It uses the `WIRE`/`AH`/`TYPE` tables from `venusstrike/decode.mjs`. The AH timeline covers every BAP connection that carried wire 6/7/8/9/100.

## Overlay

While a capture runs the plugin draws into the game's frame, in Bungie's fixedsys-9 fallback font (`CaptureFont.cpp`, ported from the MonkeyNuts Ares `font_fallback.cpp`):
- Top left, inside the title-safe area: `capturing packets (n sent, n received)`, updated live.
- The pre-alpha watermark in the pre-alpha layout: the "PRE-ALPHA BUILD (...)" lines at (0.2, 0.8) and the two id hashes at (0.8, 0.8), in normalized screen units, white at alpha 0xB0 with a drop shadow.

No thread and no allocation. The drawing runs synchronously inside the `D3DDevice_Swap` hook (`0x831AAF18`) on the render thread, and every buffer is static:
- `D3DDevice_Swap` increments `device+0x4248` for every swap it queues; the swap interrupt increments `device+0x424C` when the GPU reaches that swap, after the resolve. The hook records the buffer it was handed and its swap number, and draws into the *previous* recorded buffer once `device+0x424C` shows the GPU has passed it (resolved, on screen). Anything unexpected skips the frame.
- The write uses `XGAddress2DTiledOffset` for tiled buffers and plain pitch addressing for linear ones, blends toward white (or black for the shadow) at alpha 0xB0 for 8_8_8_8, writes solid colours for 2_10_10_10, and uses `dcbf` around each cache line.
- Validation: the device and texture pointers are readable, the swap counters are sane, the fetch constant is a 2D 8_8_8_8 or 2_10_10_10 texture of a sane size, and the front buffer's physical base has a CPU mapping `MmGetPhysicalAddress` confirms. After 16 bad frames in a row the overlay turns itself off for the session with one log line and the counts fall back to a toast.

The watermark text is the game's own. `sub_82474AA8` still formats "PRE-ALPHA BUILD ..." but its draw calls are gone; it is gated by `byte_83709C6E`, which nothing in the image writes. While capturing (and only when its hook installed) the plugin sets that byte to 1, and a `Detour` on the function copies the two formatted strings out of its stack frame as it returns. If none arrives within 3 seconds the overlay formats the same lines itself (with `?` for the fields only the game knows).

The Swap and watermark hooks go in on capture start, alongside the BAP/XHttp hooks, and stay as passthrough after a capture stops until the title exits (`SwapHook` returns straight to the original once drawing is off). `CaptureOverlay = false` leaves them out entirely.

## Notifications

The plugin shows one XNotify when a capture starts and one when it ends:
- Start: `Capture started: packets\<file>.pcap`.
- End: `Capture saved: <file> (n sent, n received, n dropped)`. This appears however the capture ended: the combo, the title exiting or another executable loading, or a failed write. The same counts and the reason are in the log line `[capture] stopped (<reason>): ...` and in the stop meta record.

When the overlay is off (`CaptureOverlay = false`) or has given up, the counts are shown as a top-left `capturing packets (n sent, n received)` toast every 6 seconds instead. There is also a one-time `out of packet space tell codie` toast the first time the ring fills.

`sent` and `received` count BAP and XHttp records. `dropped` counts records lost to a full ring.

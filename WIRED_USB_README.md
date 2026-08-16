# AirGradient ONE / Open Air — Wired USB-C firmware (Path A + Path B)

Stock AirGradient firmware **v3.6.5** extended so the ESP32-C3-based monitors
talk over their **USB-C port** — the same single cable carries **5 V power and
CDC data**, since the monitor is a USB device powered and read by the host.

Two modes are included:

- **Path B (default build) — bidirectional link to a Sensor Hub.** A full
  transport that implements AirGradient's client interface over USB-CDC:
  handshake, config fetch, and measurement posting with ACK/retry. This is the
  mode for wiring a monitor to a Sensor Hub host.
- **Path A — one-way measurement stream.** A dependency-light emitter that
  writes framed JSON out the port every 10 s. Useful for a simple logger, or in
  Wi-Fi/cellular builds as an extra local feed.

> The two git submodules that GitHub's "Download ZIP" omits
> (`airgradient-client`, `airgradient-ota`) are **already included** here at the
> commits firmware v3.6.5 pins — no `git clone --recursive` needed.

---

## 1. Which mode am I building?

A single compile-time switch near the top of `OneOpenAir.ino`:

```cpp
#define AG_WIRED_CLIENT 1   // 1 = Path B wired-to-Sensor-Hub (default)
                            // 0 = stock Wi-Fi/cellular (+ Path A emitter still available)
```

With `AG_WIRED_CLIENT 1` the sketch skips Wi-Fi/cellular auto-detect, selects the
wired client, and (in wired mode) disables the Path A emitter so measurements go
out exactly once, via the acknowledged transport.

---

## 2. Path B — the wire protocol

Typed frames, one line each, on the shared USB-CDC port:

```
$AG<T>,<seq>,<len>|<payload>*<CRC16>\r\n
```

`<T>` is a single-letter type; `<len>` is a byte length prefix (so the JSON may
contain `,` `:` `*` `|` or a stray newline safely); `<CRC16>` is CRC-16/XMODEM
over the payload. Debug-log lines never start with `$AG`, so the host filters
them for free.

| Dir | Type | Meaning | Payload |
|-----|------|---------|---------|
| device to hub | `H` | HELLO (handshake) | `{"sn","proto",...}` |
| hub to device | `Y` | READY (handshake reply) | `{"proto":1}` |
| device to hub | `M` | MEASURES (post) | measurement JSON |
| hub to device | `K` | ACK (of an `M`) | acked seq (decimal) |
| hub to device | `N` | NAK (resend an `M`) | seq (decimal) |
| device to hub | `G` | GETCONFIG | `{"sn":...}` |
| hub to device | `C` | CONFIG (reply) | config JSON body |
| device to hub | `D` | DATA (Path A one-way) | measurement JSON |

Reliability: `M` waits for a matching `K` (default 3 s); on `N` or timeout it
retransmits, up to 3 tries. `G` waits for `C` (5 s). `begin()` sends `H` and
waits for `Y` (2 s x 3), but never hard-fails — if the hub comes up late, the
networking task re-handshakes via `ensureClientConnection()`. Firmware updates
in wired mode are done by USB flashing (cloud OTA is skipped).

### Firmware stack

```
OneOpenAir.ino
  -> AirgradientSerialClient   implements AirgradientClient (begin/fetchConfig/postMeasures)
       -> AgWireLink           framing, sequence numbers, ACK/NAK wait, retry
            -> IByteIO          <-- dependency-injection seam
                 -> StreamByteIO   the only Arduino-specific file (wraps Serial + millis + delay)
```

Everything above `IByteIO` is portable C++ — which is how the host verification
runs the *real* client code against the *real* hub agent (see section 6).

---

## 3. The Sensor Hub side

`host/ag_wired_hub.py` is a reference host agent — the counterpart that runs on
your Sensor Hub. Its `HubProtocol` class is transport-agnostic and importable, so
you can drop it into your own hub software; the CLI wraps it over a serial port
(or stdin/stdout). It answers `H`->`Y`, `M`->`K`, `G`->`C`, and records `D`.

```bash
pip install pyserial
python3 host/ag_wired_hub.py --port /dev/ttyACM0     # power+read the monitor over USB-C
```

Pick the port: Linux `/dev/ttyACM0`, macOS `/dev/cu.usbmodem*`, Windows `COMx`.
The `CONFIG` body it serves uses AirGradient's config schema (the same shape the
device's `Configuration::parse()` consumes from the cloud), so the hub can push
LED mode, units, transmit cadence, etc. Edit `DEFAULT_CONFIG` to suit.

---

## 4. Path A — one-way reader

For a stock (`AG_WIRED_CLIENT 0`) or logger use, read the 10 s stream directly:

```bash
python3 host/ag_wired_reader.py --port <PORT> --baud 115200
```

Flags: `--json-only` (one bare JSON object per line, for piping), `--stdin`
(replay a raw capture). Only one program can own the port at a time — close the
Arduino Serial Monitor first.

---

## 5. Build & flash

1. Extract; optionally rename the top folder to `AirGradient_Air_Quality_Sensor`.
2. Move it into your Arduino libraries folder (Windows/macOS
   `Documents/Arduino/libraries/`, Linux `~/Arduino/libraries/`).
3. Install **esp32 by Espressif, version `2.0.17`** (Boards Manager) — not 3.x.
4. Install via Library Manager: `NimBLE-Arduino` (^2.3.7), `Sensirion Core`
   (^0.7.3), `Sensirion UART SPS30` (^1.0.0), `ArduinoJson` (^7.4.3). All other
   libraries are vendored under `src/Libraries/`.
5. Open **File -> Examples -> AirGradient Air Quality Sensor -> OneOpenAir**.
6. Tools menu:

   ```
   Board            -> ESP32C3 Dev Module
   USB CDC On Boot  -> Enabled          <-- required: puts data on USB-C
   CPU Frequency    -> 160MHz (WiFi)
   Flash Size       -> 4MB (32Mb)
   Partition Scheme -> Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)
   Upload Speed     -> 921600
   ```
7. Select the port and Upload. Then run the Sensor Hub agent (section 3).

---

## 6. Offline verification (no hardware)

Everything except the on-target compile and a physical USB read was verified on a
host toolchain — including the **real firmware protocol code driven against the
real Python hub** over a pipe. One command:

```bash
cd verify && ./run_all.sh
```

It runs: C++ Path A framing, C++ typed `FrameReader` (device RX parse), the
Python Path A reader, the Python hub protocol, the **cross-language integration
test** (compiles `airgradientClient` + `AgWireLink` + `AirgradientSerialClient`
and exercises handshake -> config fetch -> post/ACK -> NAK-forced retransmit
against `HubProtocol`), and the Path A end-to-end pipe. Expected tail:
`All verification suites passed.`

What still requires the physical monitor: (1) the on-target ESP32-C3 compile and
flash, (2) confirming the host sees frames over real USB-C. The ESP32 toolchain
can't be installed in this environment, so those two are yours to run.

---

## 7. Tunables

- Path A emit interval: `#define WIRED_EMIT_INTERVAL 10000` (ms).
- Path B timeouts/retries: `AirgradientSerialClient::set{Ack,Config,Hello}TimeoutMs()`
  and `setMaxRetries()`.
- Debug + data share the port; the framing separates them. If you want a
  perfectly clean data channel, moving ESP-IDF logs to UART0 is possible but more
  invasive — not needed for correctness.

---

## 8. File map

```
examples/OneOpenAir/
  OneOpenAir.ino            # wired mode wired into init/loop/networkingTask
  AgWireFrame.h             # framing + CRC + typed FrameReader (shared, portable)
  AgWireLink.h/.cpp         # link layer: seq, ACK/NAK, retry (portable)
  AirgradientSerialClient.h/.cpp   # AirgradientClient over the wire (portable)
  AgWireArduinoIO.h         # the only Arduino-specific piece (Serial adapter)
  WiredEmitter.h/.cpp       # Path A one-way emitter
  LocalServer.* OpenMetrics.*      # (stock)

src/Libraries/
  airgradient-client/       # submodule — INCLUDED (empty in GitHub ZIP)
  airgradient-ota/          # submodule — INCLUDED (empty in GitHub ZIP)

host/
  ag_wire.py                # shared framing (matches AgWireFrame.h)
  ag_wired_hub.py           # Path B reference Sensor Hub agent (HubProtocol)
  ag_wired_reader.py        # Path A one-way reader

verify/                     # run_all.sh + all tests (see section 6)
```

Base firmware (c) AirGradient, CC BY-SA 4.0. Additions follow the same license.

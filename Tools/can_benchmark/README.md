# CAN / CAN-FD Throughput & Loss Benchmark

Benchmarks **two Candlelight (gs_usb) adapters wired on the same bus**. One
adapter floods dummy frames carrying a 32-bit sequence number; the other
receives them. The tool then reports delivered goodput, frame rate, estimated
bus utilization and **loss rate**.

- Classic CAN  → 1 Mbit/s
- CAN-FD       → 1 Mbit/s arbitration + 5 Mbit/s data (BRS on)

Both adapters run the **Candlelight** firmware (gs_usb protocol).

---

## 1. Wiring

Connect the two adapters on one bus: `CAN_H ↔ CAN_H`, `CAN_L ↔ CAN_L`, with
**120 Ω termination at both ends** (you said termination is already present).
No other node is required.

## 2. Install

```bash
pip install -r requirements.txt
```

### Driver setup

- **Linux (recommended for CAN-FD @ 5 Mbit/s):** the kernel `gs_usb` driver
  exposes the adapters as SocketCAN interfaces (`can0`, `can1`). Use
  `--interface socketcan`. Bit rates are set with `ip`:
  ```bash
  sudo ip link set can0 up type can bitrate 1000000 dbitrate 5000000 fd on
  sudo ip link set can1 up type can bitrate 1000000 dbitrate 5000000 fd on
  ```
  Then run the tool with `--no-config-rate` style: with SocketCAN the rate is
  set by `ip`, so the tool's bitrate args are informational for the report only.

- **Windows:** the gs_usb adapters need **two** things:
  1. A **libusb backend** for PyUSB — installed automatically by
     `pip install -r requirements.txt` (`libusb-package`). Without it PyUSB
     raises `usb.core.NoBackendError: No backend available`.
  2. The **WinUSB** (or libusbK) driver bound to each device with
     [Zadig](https://zadig.akeo.ie/): select each Candlelight device → install
     *WinUSB*. Without this the device is found but cannot be opened.

  Then use `--interface gs_usb` (the default). python-can sets the bit rate over USB.

> **CAN-FD on Windows is not supported by the gs_usb Python library.** The
> PyPI `gs_usb` package only packs classic (8-byte) frames, so `--mode canfd`
> fails on Windows with `struct.error: pack expected 15 items`. Classic CAN
> (1 Mbit/s) works fine on Windows. **For CAN-FD @ 5 Mbit/s use Linux
> SocketCAN**, where the kernel `gs_usb` driver has full FD support.

> **PCAN does not work with Candlelight.** python-can's `pcan` backend talks to
> PEAK-System's `PCANBasic.dll`, which only drives genuine PEAK PCAN-USB
> hardware. A Candlelight / CANable adapter is a **gs_usb** device and is
> invisible to the PCAN driver. Use `gs_usb` (Windows/Linux) or `socketcan`
> (Linux). If you want a backend that needs no libusb/Zadig on Windows, flash
> the **Slcan** firmware instead and use `--interface slcan --tx COMx`.

## 3. Find the adapters

```bash
python can_benchmark.py --list-gs-usb
```
Note the two indexes (e.g. `0` and `1`).

## 4. Run

**Classic CAN @ 1 Mbit/s** (TX = index 0, RX = index 1):
```bash
python can_benchmark.py --interface gs_usb --tx 0 --rx 1 \
    --mode can --nominal-bitrate 1000000 --count 50000
```

**CAN-FD, 64-byte payload, 1 M / 5 Mbit/s:**
```bash
python can_benchmark.py --interface gs_usb --tx 0 --rx 1 \
    --mode canfd --nominal-bitrate 1000000 --data-bitrate 5000000 \
    --payload 64 --count 50000
```

**Bidirectional (both adapters transmit at once — aggregate bus load):**
```bash
python can_benchmark.py --tx 0 --rx 1 --mode canfd --payload 64 \
    --bidirectional --count 50000
```

**Linux / SocketCAN:**
```bash
python can_benchmark.py --interface socketcan --tx can0 --rx can1 \
    --mode canfd --payload 64 --count 50000
```

### Useful options
| option | meaning |
|--------|---------|
| `--count N`        | frames to send per direction |
| `--payload B`      | payload bytes (classic ≤8; FD 0..8,12,16,20,24,32,48,64) |
| `--gap S`          | inter-frame gap in seconds (0 = flood) |
| `--bidirectional`  | both adapters send simultaneously |
| `--no-brs`         | CAN-FD without bit-rate switching |
| `--tx-id / --rx-id`| arbitration IDs of the two streams |
| `--drain S`        | extra receive time after the last send |

## 5. Reading the report

- **received / LOST / loss rate** — frames the RX node got vs. missing sequence
  numbers. This is the headline reliability metric.
- **frame rate** and **goodput** — *measured* delivered rate (payload only).
- **% of theoretical** — efficiency vs. the model's maximum for that frame.
- **bus util (est)** — model-based estimate of how busy the link was. For
  CAN-FD it is referenced to the **data-phase** rate.

> Notes
> - The bus-utilization / theoretical figures are **model based** (typical, no
>   bit-stuffing) — treat them as estimates. The measured goodput, frame rate
>   and loss are authoritative.
> - At very high rates the Python send loop or the USB link (not the CAN bus)
>   can be the bottleneck; that shows up as `frame rate` below theoretical with
>   **zero loss**. Genuine bus/buffer overrun shows up as **loss > 0** or
>   `send errors`. Use `--gap` to find the loss-free sustainable rate.
> - With only two nodes and 120 Ω, every frame must be ACKed by the peer; if one
>   adapter is not actually on the bus you will see 100 % loss and/or send errors.

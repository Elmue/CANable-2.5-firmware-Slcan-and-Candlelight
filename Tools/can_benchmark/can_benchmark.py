#!/usr/bin/env python3
"""
CAN / CAN-FD throughput & loss benchmark for two Candlelight (gs_usb) adapters.

Two adapters are wired together on the same CAN bus (TX node -> RX node, or
bidirectional). The tool floods dummy frames carrying a 32-bit sequence number,
the receiver counts them, and the tool reports:

  * delivered goodput (payload bits/s and bytes/s)
  * frame rate (frames/s)
  * estimated bus utilization vs the configured bit rate (model based)
  * theoretical maximum for the configured frame, and efficiency
  * loss rate (missing sequence numbers), duplicates, out-of-order, error frames

Backends (selected with --interface):
  gs_usb     Candlelight / CANable gs_usb adapters (Windows + Linux, via PyUSB)
  socketcan  Linux native SocketCAN (e.g. can0/can1) - very robust for gs_usb FD

See README.md for driver setup (Windows: WinUSB via Zadig).
"""

import argparse
import struct
import sys
import threading
import time
from dataclasses import dataclass, field

try:
    import can
except ImportError:
    sys.exit("ERROR: python-can is not installed.  ->  pip install -r requirements.txt")

SEQ_FMT = "<I"          # 32-bit little-endian sequence number in the first 4 payload bytes
SEQ_SIZE = 4


# --------------------------------------------------------------------------- #
#  On-wire timing model (used only for the *estimated* bus-utilization figure)
# --------------------------------------------------------------------------- #
def frame_on_wire_seconds(n_data, fd, brs, nominal_bitrate, data_bitrate):
    """
    Approximate time a single frame (11-bit ID) occupies the bus, in seconds.

    The figure is model based (no/typical bit-stuffing) and is meant for a
    utilization *estimate*; the authoritative numbers are the measured goodput,
    frame rate and loss. Includes the 3-bit inter-frame space.
    """
    if not fd:
        # Classic CAN 2.0A standard frame:
        #   SOF1 + ID11 + RTR1 + IDE1 + r0 1 + DLC4 + DATA(8N) + CRC15 + CRCdel1
        #   + ACK1 + ACKdel1 + EOF7 + IFS3  = 47 + 8N  (no stuffing)
        bits = 47 + 8 * n_data
        return bits / nominal_bitrate

    # CAN-FD (ISO), 11-bit ID, BRS as requested.
    # Arbitration phase (nominal bit rate): SOF..BRS = 17 bits, tail
    #   (CRCdel + ACK + ACKdel + EOF + IFS) = 13 bits  -> 30 bits @ nominal.
    nominal_bits = 30
    # Data phase (data bit rate when brs=True, else nominal):
    #   ESI1 + DLC4 + DATA(8N) + StuffCount/parity 4 + CRC(17 if N<=16 else 21)
    crc = 17 if n_data <= 16 else 21
    data_bits = 1 + 4 + 8 * n_data + 4 + crc
    data_rate = data_bitrate if brs else nominal_bitrate
    return nominal_bits / nominal_bitrate + data_bits / data_rate


def dlc_is_valid_fd(n):
    return n in (0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64)


# --------------------------------------------------------------------------- #
#  Receiver
# --------------------------------------------------------------------------- #
class RxCollector(threading.Thread):
    """Continuously drains one bus, recording (recv_time, id, seq, dlc)."""

    def __init__(self, bus, name):
        super().__init__(daemon=True)
        self.bus = bus
        self.name = name
        self._stop_evt = threading.Event()   # NB: not '_stop' (collides with Thread._stop)
        self.records = []            # list of (t, arb_id, seq)
        self.error_frames = 0
        self.last_rx_time = None

    def run(self):
        while not self._stop_evt.is_set():
            try:
                msg = self.bus.recv(timeout=0.2)
            except can.CanError:
                continue
            if msg is None:
                continue
            t = time.perf_counter()
            self.last_rx_time = t
            if msg.is_error_frame:
                self.error_frames += 1
                continue
            seq = -1
            if len(msg.data) >= SEQ_SIZE:
                seq = struct.unpack_from(SEQ_FMT, msg.data)[0]
            self.records.append((t, msg.arbitration_id, seq))

    def stop(self):
        self._stop_evt.set()


# --------------------------------------------------------------------------- #
#  Sender
# --------------------------------------------------------------------------- #
@dataclass
class TxResult:
    sent: int = 0
    send_errors: int = 0
    fatal: str = ""           # backend/library error that aborted the stream
    t_start: float = 0.0
    t_end: float = 0.0


def sender(bus, can_id, count, n_data, fd, brs, gap, result: TxResult):
    payload = bytearray(n_data)
    # Fill the bytes after the sequence number with a recognizable pattern.
    for i in range(SEQ_SIZE, n_data):
        payload[i] = i & 0xFF

    result.t_start = time.perf_counter()
    for seq in range(count):
        struct.pack_into(SEQ_FMT, payload, 0, seq)
        msg = can.Message(
            arbitration_id=can_id,
            data=bytes(payload),
            is_extended_id=False,
            is_fd=fd,
            bitrate_switch=brs if fd else False,
        )
        try:
            bus.send(msg, timeout=1.0)
            result.sent += 1
        except can.CanError:
            result.send_errors += 1
        except Exception as e:
            # e.g. the Windows gs_usb library cannot pack CAN-FD frames.
            result.fatal = f"{type(e).__name__}: {e}"
            break
        if gap > 0:
            time.sleep(gap)
    result.t_end = time.perf_counter()


# --------------------------------------------------------------------------- #
#  Stats
# --------------------------------------------------------------------------- #
@dataclass
class StreamStats:
    label: str
    can_id: int
    sent: int
    send_errors: int
    received: int = 0
    unique: int = 0
    duplicates: int = 0
    out_of_order: int = 0
    lost: int = 0
    rx_span: float = 0.0          # seconds between first and last received frame
    tx_span: float = 0.0


def analyse_stream(label, can_id, tx: TxResult, rx_records):
    st = StreamStats(label=label, can_id=can_id, sent=tx.sent,
                     send_errors=tx.send_errors, tx_span=tx.t_end - tx.t_start)
    mine = [(t, seq) for (t, cid, seq) in rx_records if cid == can_id]
    st.received = len(mine)
    seqs = [seq for (_, seq) in mine]
    unique = set(seqs)
    st.unique = len(unique)
    st.duplicates = st.received - st.unique
    # Out-of-order: a received seq smaller than a previously seen max.
    seen_max = -1
    for seq in seqs:
        if seq < seen_max:
            st.out_of_order += 1
        else:
            seen_max = seq
    # Loss measured against what we actually managed to send.
    expected = set(range(tx.sent))
    st.lost = len(expected - unique)
    if mine:
        st.rx_span = mine[-1][0] - mine[0][0]
    return st


def print_report(args, n_data, streams, total_error_frames):
    fd = args.mode == "canfd"
    brs = fd and not args.no_brs
    nominal = args.nominal_bitrate
    data_rate = args.data_bitrate if fd else nominal
    t_frame = frame_on_wire_seconds(n_data, fd, brs, nominal, data_rate)
    theo_fps = 1.0 / t_frame
    theo_goodput = n_data * 8 * theo_fps

    line = "=" * 72
    print("\n" + line)
    print(f" CAN BENCHMARK REPORT   mode={args.mode}  payload={n_data} B"
          f"  ID(s)=" + ", ".join(f"0x{s.can_id:X}" for s in streams))
    if fd:
        print(f" bitrate: nominal {nominal/1e6:.3f} Mbit/s , data {data_rate/1e6:.3f} Mbit/s"
              f"  (BRS {'on' if brs else 'off'})")
    else:
        print(f" bitrate: {nominal/1e6:.3f} Mbit/s (classic)")
    print(f" model: ~{t_frame*1e6:.1f} us/frame  ->  theoretical max"
          f" {theo_fps:,.0f} frame/s , goodput {theo_goodput/1e6:.3f} Mbit/s")
    print(line)

    agg_recv = agg_sent = 0
    agg_span = 0.0
    for s in streams:
        agg_recv += s.received
        agg_sent += s.sent
        agg_span = max(agg_span, s.rx_span)
        fps = s.received / s.rx_span if s.rx_span > 0 else 0.0
        goodput = n_data * 8 * fps
        util = (s.received * t_frame) / s.rx_span * 100 if s.rx_span > 0 else 0.0
        eff = fps / theo_fps * 100 if theo_fps else 0.0
        loss_pct = (s.lost / s.sent * 100) if s.sent else 0.0
        print(f"\n [{s.label}]  ID 0x{s.can_id:X}")
        print(f"   sent           : {s.sent:,}"
              + (f"   (send errors: {s.send_errors:,})" if s.send_errors else ""))
        print(f"   received       : {s.received:,}   unique {s.unique:,}"
              f"   dup {s.duplicates:,}   out-of-order {s.out_of_order:,}")
        print(f"   LOST           : {s.lost:,}   ->  loss rate {loss_pct:.3f} %")
        print(f"   rx duration    : {s.rx_span*1e3:.1f} ms")
        print(f"   frame rate     : {fps:,.0f} frame/s   ({eff:.1f}% of theoretical)")
        print(f"   goodput        : {goodput/1e6:.3f} Mbit/s  ({goodput/8e6:.3f} MB/s)")
        print(f"   bus util (est) : {util:.1f} %  of the {('data ' if fd else '')}link")

    if len(streams) > 1 and agg_span > 0:
        agg_fps = agg_recv / agg_span
        agg_goodput = n_data * 8 * agg_fps
        print("\n " + "-" * 70)
        print(f" AGGREGATE (both directions on the shared bus)")
        print(f"   total received : {agg_recv:,} / {agg_sent:,} sent")
        print(f"   frame rate     : {agg_fps:,.0f} frame/s")
        print(f"   goodput        : {agg_goodput/1e6:.3f} Mbit/s")
        print(f"   bus util (est) : {(agg_recv*t_frame)/agg_span*100:.1f} %")

    if total_error_frames:
        print(f"\n  CAN error frames observed: {total_error_frames}")
    print(line + "\n")


# --------------------------------------------------------------------------- #
#  Bus creation
# --------------------------------------------------------------------------- #
_usb_backend_ready = False


def setup_usb_backend():
    """
    Provide a libusb backend for PyUSB.

    On Windows PyUSB ships no libusb DLL, so usb.core.find() raises
    NoBackendError. The 'libusb-package' wheel bundles libusb-1.0.dll; we wrap
    usb.core.find() so it always passes that backend, which makes gs_usb's
    internal calls work. (The device itself still needs a WinUSB/libusbK driver
    bound via Zadig - see README.)
    """
    global _usb_backend_ready
    if _usb_backend_ready:
        return True
    try:
        import functools
        import usb.core
        import libusb_package
    except ImportError:
        return False
    backend = libusb_package.get_libusb1_backend()
    if backend is None:
        return False
    # Wrap the ORIGINAL find (do not assign libusb_package.find here - it calls
    # usb.core.find internally and would recurse). Inject the bundled backend.
    _orig_find = usb.core.find
    usb.core.find = functools.partial(_orig_find, backend=backend)
    _usb_backend_ready = True
    return True


def list_gs_usb():
    try:
        from gs_usb.gs_usb import GsUsb
    except ImportError:
        sys.exit("ERROR: gs_usb not installed.  ->  pip install gs_usb pyusb libusb-package")
    if not setup_usb_backend():
        print("WARNING: 'libusb-package' not installed; PyUSB may raise NoBackendError.\n"
              "         ->  pip install libusb-package", file=sys.stderr)
    devs = GsUsb.scan()
    if not devs:
        print("No gs_usb / Candlelight devices found.")
        return
    print(f"Found {len(devs)} gs_usb device(s):")
    for i, d in enumerate(devs):
        print(f"  index {i}: {d}")


def make_bus(args, index_or_channel, fd):
    kwargs = dict(bitrate=args.nominal_bitrate)
    if fd:
        kwargs["fd"] = True
        kwargs["data_bitrate"] = args.data_bitrate

    if args.interface == "gs_usb":
        setup_usb_backend()   # make libusb-package's backend available to PyUSB
        # python-can selects the Nth scanned gs_usb device via 'index'.
        kwargs.update(interface="gs_usb", channel=index_or_channel,
                      index=index_or_channel)
    else:
        kwargs.update(interface=args.interface, channel=index_or_channel)

    try:
        return can.Bus(**kwargs)
    except TypeError as e:
        # Older backends may not accept fd/data_bitrate kwargs.
        sys.exit(f"ERROR: backend '{args.interface}' rejected options ({e}).\n"
                 f"       Your python-can / backend may not support CAN-FD here.")
    except Exception as e:
        sys.exit(f"ERROR: could not open {args.interface} '{index_or_channel}': {e}")


# --------------------------------------------------------------------------- #
#  Main
# --------------------------------------------------------------------------- #
def main():
    p = argparse.ArgumentParser(
        description="CAN / CAN-FD throughput & loss benchmark for two Candlelight (gs_usb) adapters.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    p.add_argument("--list-gs-usb", action="store_true",
                   help="list connected gs_usb / Candlelight devices and exit")
    p.add_argument("--interface", default="gs_usb",
                   help="python-can interface: gs_usb | socketcan | ...")
    p.add_argument("--tx", default="0",
                   help="TX adapter: gs_usb index (0,1,..) or socketcan channel (can0)")
    p.add_argument("--rx", default="1",
                   help="RX adapter: gs_usb index (0,1,..) or socketcan channel (can1)")
    p.add_argument("--mode", choices=["can", "canfd"], default="can",
                   help="classic CAN or CAN-FD")
    p.add_argument("--nominal-bitrate", type=int, default=1_000_000,
                   help="arbitration / classic bit rate (bit/s)")
    p.add_argument("--data-bitrate", type=int, default=5_000_000,
                   help="CAN-FD data-phase bit rate (bit/s)")
    p.add_argument("--no-brs", action="store_true",
                   help="CAN-FD without bit-rate switching (data phase at nominal)")
    p.add_argument("--count", type=int, default=20000,
                   help="number of frames to send per direction")
    p.add_argument("--payload", type=int, default=None,
                   help="payload bytes (default 8 for can, 64 for canfd; min 4)")
    p.add_argument("--gap", type=float, default=0.0,
                   help="inter-frame gap in seconds (0 = flood as fast as possible)")
    p.add_argument("--tx-id", type=lambda x: int(x, 0), default=0x100,
                   help="arbitration ID for the TX->RX stream")
    p.add_argument("--rx-id", type=lambda x: int(x, 0), default=0x101,
                   help="arbitration ID for the reverse stream (--bidirectional)")
    p.add_argument("--bidirectional", action="store_true",
                   help="both adapters send simultaneously (aggregate bus load)")
    p.add_argument("--drain", type=float, default=1.0,
                   help="seconds to keep receiving after the last send")
    args = p.parse_args()

    if args.list_gs_usb:
        list_gs_usb()
        return

    fd = args.mode == "canfd"
    brs = fd and not args.no_brs
    n_data = args.payload if args.payload is not None else (64 if fd else 8)

    if n_data < SEQ_SIZE:
        sys.exit(f"ERROR: --payload must be >= {SEQ_SIZE} (needs room for the sequence number).")
    if not fd and n_data > 8:
        sys.exit("ERROR: classic CAN payload cannot exceed 8 bytes (use --mode canfd).")
    if fd and not dlc_is_valid_fd(n_data):
        sys.exit("ERROR: CAN-FD payload must be one of 0..8,12,16,20,24,32,48,64.")

    # gs_usb indexes are ints; socketcan channels are strings.
    def chan(v):
        return int(v) if args.interface == "gs_usb" else v

    tx_chan = chan(args.tx)
    rx_chan = chan(args.rx)

    if fd and args.interface == "gs_usb":
        print("WARNING: the Windows 'gs_usb' Python library only packs classic CAN\n"
              "         frames - CAN-FD will likely fail here. For CAN-FD @ 5 Mbit/s\n"
              "         use Linux SocketCAN (--interface socketcan --tx can0 --rx can1).\n",
              file=sys.stderr)

    print(f"Opening TX adapter ({args.interface} {args.tx}) ...")
    bus_tx = make_bus(args, tx_chan, fd)
    print(f"Opening RX adapter ({args.interface} {args.rx}) ...")
    bus_rx = make_bus(args, rx_chan, fd)

    rx_on_rxbus = RxCollector(bus_rx, "rxbus")
    rx_on_txbus = RxCollector(bus_tx, "txbus")   # used only for the reverse stream
    rx_on_rxbus.start()
    if args.bidirectional:
        rx_on_txbus.start()

    # Senders
    fwd = TxResult()
    rev = TxResult()
    threads = []
    t_fwd = threading.Thread(
        target=sender,
        args=(bus_tx, args.tx_id, args.count, n_data, fd, brs, args.gap, fwd))
    threads.append(t_fwd)
    if args.bidirectional:
        t_rev = threading.Thread(
            target=sender,
            args=(bus_rx, args.rx_id, args.count, n_data, fd, brs, args.gap, rev))
        threads.append(t_rev)

    print(f"Sending {args.count:,} frames"
          + (" per direction (bidirectional)" if args.bidirectional else "")
          + f" , payload {n_data} B , {'flood' if args.gap == 0 else f'gap {args.gap*1e3:.2f} ms'} ...")
    t0 = time.perf_counter()
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    print(f"Send phase done in {time.perf_counter()-t0:.2f} s , draining receivers ...")

    # Drain: wait until no new frame arrives for `drain` seconds.
    deadline = time.perf_counter() + args.drain
    while time.perf_counter() < deadline:
        time.sleep(0.05)
        last = rx_on_rxbus.last_rx_time
        if args.bidirectional and rx_on_txbus.last_rx_time:
            last = max(last or 0, rx_on_txbus.last_rx_time)
        if last and (time.perf_counter() - last) < args.drain:
            deadline = last + args.drain

    # Stop only the collectors that were actually started, and fully join them
    # BEFORE touching the buses (libusb is not safe for concurrent access).
    rx_threads = [rx_on_rxbus] + ([rx_on_txbus] if args.bidirectional else [])
    for rxt in rx_threads:
        rxt.stop()
    for rxt in rx_threads:
        rxt.join(timeout=2.0)

    # Analyse
    streams = [analyse_stream("TX -> RX", args.tx_id, fwd, rx_on_rxbus.records)]
    if args.bidirectional:
        streams.append(analyse_stream("RX -> TX", args.rx_id, rev, rx_on_txbus.records))

    total_err = rx_on_rxbus.error_frames + (rx_on_txbus.error_frames if args.bidirectional else 0)
    if fwd.fatal or rev.fatal:
        print(f"\n*** TRANSMIT ABORTED: {fwd.fatal or rev.fatal}", file=sys.stderr)
        if fd and args.interface == "gs_usb":
            print("*** This is the gs_usb library's lack of CAN-FD support on Windows,\n"
                  "*** not a tool bug. Run CAN-FD on Linux SocketCAN instead.", file=sys.stderr)
    print_report(args, n_data, streams, total_err)

    for b in (bus_tx, bus_rx):
        try:
            b.shutdown()
        except Exception:
            pass   # gs_usb teardown on Windows can be noisy; data already collected


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\ninterrupted.")

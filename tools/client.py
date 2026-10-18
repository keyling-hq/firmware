#!/usr/bin/env python3
"""Bridge protocol test client (Windows/macOS/Linux via bleak).

One command per connection:
    tools/.venv/Scripts/python tools/client.py type "hello world"
    tools/.venv/Scripts/python tools/client.py type --file notes.txt
    tools/.venv/Scripts/python tools/client.py combo ctrl+alt+t
    tools/.venv/Scripts/python tools/client.py media volume_up
    tools/.venv/Scripts/python tools/client.py config layout=es-419 press=5
    tools/.venv/Scripts/python tools/client.py stress 20000
    tools/.venv/Scripts/python tools/client.py stress 20000 --abort-after 5
    tools/.venv/Scripts/python tools/client.py ota .pio/build/hid/firmware.bin

Interactive, many commands over one connection (commands run in the background, so
`abort` works while a `stress` is still going):
    tools/.venv/Scripts/python tools/client.py shell

The PC must be bonded with the dongle. The dongle has a single owner: pair it with
"Pair new phone" on the dongle and `--pair`, which replaces the phone's bond.
"""

import argparse
import asyncio
import hashlib
import shlex
import struct
import sys
import time
from pathlib import Path

from bleak import BleakClient, BleakScanner

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bridge_pb2 as pb  # noqa: E402

BRIDGE_SVC = "7a1f0001-3c4e-4d8a-9b2e-5f6a7b8c9d0e"
CMD = "7a1f0002-3c4e-4d8a-9b2e-5f6a7b8c9d0e"
EVT = "7a1f0003-3c4e-4d8a-9b2e-5f6a7b8c9d0e"
CTRL = "7a1f0004-3c4e-4d8a-9b2e-5f6a7b8c9d0e"

TEXT_PIECE = 1000       # bytes of UTF-8 per Text request, below the 1023 the schema allows
ATT_MAX_VALUE = 512     # a GATT write can never carry more than this, whatever the MTU

MODIFIERS = {
    "ctrl": 0x01, "shift": 0x02, "alt": 0x04, "win": 0x08, "gui": 0x08, "cmd": 0x08,
    "rctrl": 0x10, "rshift": 0x20, "ralt": 0x40, "altgr": 0x40, "rwin": 0x80,
}

KEYS = {
    "enter": 0x28, "esc": 0x29, "escape": 0x29, "backspace": 0x2A, "tab": 0x2B, "space": 0x2C,
    "capslock": 0x39, "printscreen": 0x46, "scrolllock": 0x47, "pause": 0x48, "insert": 0x49,
    "home": 0x4A, "pageup": 0x4B, "delete": 0x4C, "del": 0x4C, "end": 0x4D, "pagedown": 0x4E,
    "right": 0x4F, "left": 0x50, "down": 0x51, "up": 0x52, "menu": 0x65,
}
KEYS.update({chr(ord("a") + i): 0x04 + i for i in range(26)})
KEYS.update({str(d): 0x1E + (d - 1) for d in range(1, 10)})
KEYS["0"] = 0x27
KEYS.update({f"f{n}": 0x3A + n - 1 for n in range(1, 13)})

STROKE_CAPS = 0x01
SHIFT = 0x02

# en-US only: enough to exercise phone-side translation. char -> (modifiers, keycode, flags)
US_STROKES = {" ": (0, 0x2C, 0), "\n": (0, 0x28, 0), "\t": (0, 0x2B, 0)}
US_STROKES.update({chr(ord("a") + i): (0, 0x04 + i, STROKE_CAPS) for i in range(26)})
US_STROKES.update({chr(ord("A") + i): (SHIFT, 0x04 + i, STROKE_CAPS) for i in range(26)})
US_STROKES.update({str(d): (0, 0x1E + d - 1, 0) for d in range(1, 10)})
US_STROKES["0"] = (0, 0x27, 0)
for plain, shifted, key in zip("-=[]\\;'`,./", "_+{}|:\"~<>?",
                               (0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38)):
    US_STROKES[plain] = (0, key, 0)
    US_STROKES[shifted] = (SHIFT, key, 0)
for ch, key in zip("!@#$%^&*()", range(0x1E, 0x28)):
    US_STROKES[ch] = (SHIFT, key, 0)

MEDIA = {
    "play_pause": 0xCD, "next": 0xB5, "prev": 0xB6, "stop": 0xB7,
    "mute": 0xE2, "volume_up": 0xE9, "volume_down": 0xEA,
}


class CommandError(Exception):
    pass


def parse_combo(spec):
    modifiers, keys = 0, []
    for part in spec.lower().split("+"):
        if part in MODIFIERS:
            modifiers |= MODIFIERS[part]
        elif part in KEYS:
            keys.append(KEYS[part])
        else:
            raise CommandError(f"unknown key '{part}'")
    if len(keys) > 6:
        raise CommandError("at most 6 keys")
    return modifiers, bytes(keys)


def split_utf8(text, limit):
    """Split text into pieces of at most limit UTF-8 bytes without breaking characters."""
    piece, size = [], 0
    for ch in text:
        n = len(ch.encode())
        if size + n > limit:
            yield "".join(piece)
            piece, size = [], 0
        piece.append(ch)
        size += n
    if piece:
        yield "".join(piece)


class Bridge:
    def __init__(self, client, verbose):
        self.client = client
        self.verbose = verbose
        self.rx = bytearray()
        self.credits = 0
        self.credits_changed = asyncio.Event()
        self.hello = asyncio.get_running_loop().create_future()
        self.pending = {}
        self.next_id = 1
        self.status = None
        self.idle = asyncio.Event()
        self.send_lock = asyncio.Lock()
        self.abort_generation = 0
        self.awaiting_reset = False

    def log(self, msg):
        if self.verbose:
            print(f"  [{time.strftime('%H:%M:%S')}] {msg}")

    async def start(self):
        await self.client.start_notify(EVT, self._on_notify)
        hello = await asyncio.wait_for(self.hello, 5)
        while self.credits == 0:
            await self._wait_credits()
        print(f"connected: firmware {hello.firmware_version}, protocol v{hello.protocol_version}, "
              f"max frame {hello.max_frame} B, mtu {self.client.mtu_size}, credits {self.credits}")
        print(f"capabilities: {', '.join(hello.capabilities)}")

    def _on_notify(self, _char, data):
        self.rx += data
        while len(self.rx) >= 2:
            (n,) = struct.unpack_from("<H", self.rx)
            if len(self.rx) < 2 + n:
                break
            event = pb.Event()
            event.ParseFromString(bytes(self.rx[2:2 + n]))
            del self.rx[:2 + n]
            self._dispatch(event)

    def _dispatch(self, event):
        kind = event.WhichOneof("msg")
        if kind == "hello":
            if not self.hello.done():
                self.hello.set_result(event.hello)
        elif kind == "credits":
            c = event.credits
            if c.reset:
                self.awaiting_reset = False
            elif self.awaiting_reset:
                self.log(f"credits +{c.bytes} ignored, waiting for the abort's reset")
                return
            self.credits = c.bytes if c.reset else self.credits + c.bytes
            self.log(f"credits {'=' if c.reset else '+'}{c.bytes} -> {self.credits}")
            self.credits_changed.set()
        elif kind == "ack":
            fut = self.pending.pop(event.ack.id, None)
            if fut and not fut.done():
                fut.set_result(event.ack.result)
        elif kind == "status":
            self.status = event.status
            self.log(f"status typing={event.status.typing} usb={event.status.usb_mounted} "
                     f"layout={event.status.layout}")
            if event.status.typing:
                self.idle.clear()
            else:
                self.idle.set()
        elif kind == "config":
            c = event.config
            print(f"config: layout={c.layout} press={c.key_press_ms}ms release={c.key_release_ms}ms "
                  f"available={list(c.available_layouts)}")

    async def _wait_credits(self):
        self.credits_changed.clear()
        await self.credits_changed.wait()

    async def request(self, req):
        """Send a request and wait for its Ack. Returns the Result."""
        generation = self.abort_generation
        async with self.send_lock:
            req.id = self.next_id
            self.next_id += 1
            payload = req.SerializeToString()
            frame = struct.pack("<H", len(payload)) + payload
            while self.credits < len(frame) and generation == self.abort_generation:
                self.log(f"waiting for credits ({self.credits} < {len(frame)})")
                await self._wait_credits()
            if generation != self.abort_generation:
                return pb.RESULT_ABORTED
            self.credits -= len(frame)

            fut = asyncio.get_running_loop().create_future()
            self.pending[req.id] = fut
            chunk = min(self.client.mtu_size - 3, ATT_MAX_VALUE)
            for i in range(0, len(frame), chunk):
                await self.client.write_gatt_char(CMD, frame[i:i + chunk], response=False)
        result = await fut
        if result != pb.RESULT_OK:
            print(f"request {req.id} ({req.WhichOneof('msg')}): {pb.Result.Name(result)}")
        return result

    def expect_typing(self):
        """Call before queueing input so wait_idle() waits for the Status that ends it."""
        self.idle.clear()

    async def wait_idle(self, timeout):
        await asyncio.wait_for(self.idle.wait(), timeout)

    async def abort(self):
        self.abort_generation += 1
        for fut in self.pending.values():
            if not fut.done():
                fut.set_result(pb.RESULT_ABORTED)
        self.pending.clear()
        self.credits = 0
        self.awaiting_reset = True
        await self.client.write_gatt_char(CTRL, b"\x01", response=True)
        while self.awaiting_reset:
            await self._wait_credits()


# ---- commands ----------------------------------------------------------------

async def type_text(bridge, text, label):
    t0 = time.monotonic()
    sent = 0
    bridge.expect_typing()
    for piece in split_utf8(text, TEXT_PIECE):
        if await bridge.request(pb.Request(text=pb.Text(text=piece))) == pb.RESULT_ABORTED:
            print(f"{label}: aborted with {sent}/{len(text)} chars queued")
            return
        sent += len(piece)
        bridge.log(f"queued {sent}/{len(text)} chars, credits {bridge.credits}")
    await bridge.wait_idle(timeout=max(30, len(text) / 20))
    dt = time.monotonic() - t0
    print(f"{label}: typed {len(text)} chars in {dt:.1f} s ({len(text) / dt:.0f} chars/s)")


OTA_CHUNK = 990
OTA_IN_FLIGHT = 8


async def ota(bridge, path):
    image = path.read_bytes()
    digest = hashlib.sha256(image).digest()
    print(f"{path.name}: {len(image)} B, sha256 {digest.hex()[:16]}...")
    t0 = time.monotonic()

    print("erasing the update slot...")
    result = await bridge.request(pb.Request(ota_begin=pb.OtaBegin(size=len(image), sha256=digest)))
    if result != pb.RESULT_OK:
        raise CommandError("dongle refused the update")

    in_flight = []
    for offset in range(0, len(image), OTA_CHUNK):
        req = pb.Request(ota_data=pb.OtaData(offset=offset, data=image[offset:offset + OTA_CHUNK]))
        in_flight.append(asyncio.create_task(bridge.request(req)))
        if len(in_flight) >= OTA_IN_FLIGHT:
            if await in_flight.pop(0) != pb.RESULT_OK:
                raise CommandError("\nupdate failed while sending data")
        sent = min(offset + OTA_CHUNK, len(image))
        print(f"\r  {sent * 100 // len(image)}% ({sent}/{len(image)} B)", end="")
    if any(result != pb.RESULT_OK for result in await asyncio.gather(*in_flight)):
        raise CommandError("\nupdate failed while sending data")

    result = await bridge.request(pb.Request(ota_end=pb.OtaEnd()))
    if result != pb.RESULT_OK:
        raise CommandError("\nimage rejected by the dongle")
    dt = time.monotonic() - t0
    print(f"\nupdate written in {dt:.1f} s ({len(image) / dt / 1024:.1f} KB/s); the dongle is restarting")


async def execute(bridge, args):
    if args.command == "type":
        text = Path(args.file).read_text(encoding="utf-8") if args.file else " ".join(args.text)
        await type_text(bridge, text, "type")

    elif args.command == "stress":
        words = "the quick brown fox jumps over the lazy dog "
        text = (words * (args.bytes // len(words) + 1))[:args.bytes]
        if args.abort_after:
            typing = asyncio.create_task(type_text(bridge, text, "stress"))
            await asyncio.sleep(args.abort_after)
            await bridge.abort()
            await typing
        else:
            await type_text(bridge, text, "stress")

    elif args.command == "strokes":
        text = " ".join(args.text)
        missing = sorted({ch for ch in text if ch not in US_STROKES})
        if missing:
            print(f"no en-US mapping for {missing}, skipped")
        packed = bytes(b for ch in text if ch in US_STROKES for b in US_STROKES[ch])
        bridge.expect_typing()
        for i in range(0, len(packed), 990):
            await bridge.request(pb.Request(strokes=pb.Strokes(strokes=packed[i:i + 990])))
        await bridge.wait_idle(timeout=max(30, len(packed) / 60))

    elif args.command == "ota":
        await ota(bridge, Path(args.image))

    elif args.command == "combo":
        modifiers, keys = parse_combo(args.spec)
        await bridge.request(pb.Request(key_combo=pb.KeyCombo(modifiers=modifiers, keys=keys)))

    elif args.command == "media":
        if args.key not in MEDIA:
            raise CommandError(f"unknown media key, one of: {', '.join(MEDIA)}")
        await bridge.request(pb.Request(consumer=pb.Consumer(usage=MEDIA[args.key])))

    elif args.command == "config":
        if not args.settings:
            await bridge.request(pb.Request(get_config=pb.GetConfig()))
            return
        config = pb.Config()
        for item in args.settings:
            key, _, value = item.partition("=")
            if key == "layout":
                config.layout = value
            elif key == "press":
                config.key_press_ms = int(value)
            elif key == "release":
                config.key_release_ms = int(value)
            else:
                raise CommandError(f"unknown setting '{key}' (layout, press, release)")
        await bridge.request(pb.Request(set_config=pb.SetConfig(config=config)))

    elif args.command == "ping":
        t0 = time.monotonic()
        await bridge.request(pb.Request(ping=pb.Ping()))
        print(f"ping {1000 * (time.monotonic() - t0):.0f} ms")

    elif args.command == "abort":
        await bridge.abort()
        print(f"aborted, credits {bridge.credits}")

    elif args.command == "status":
        s = bridge.status
        print(f"typing={s.typing} usb_mounted={s.usb_mounted} layout={s.layout}" if s else "no status yet")

    elif args.command == "verbose":
        bridge.verbose = not bridge.verbose
        print(f"verbose {'on' if bridge.verbose else 'off'}")


class ShellParser(argparse.ArgumentParser):
    def error(self, message):
        raise CommandError(message)

    def exit(self, status=0, message=None):
        if message:
            print(message, end="")
        raise CommandError("")


def add_commands(sub):
    p = sub.add_parser("type", help="type text")
    p.add_argument("text", nargs="*")
    p.add_argument("--file", help="type the contents of a UTF-8 file")
    p = sub.add_parser("strokes", help="translate text to en-US keystrokes here and send them as Strokes")
    p.add_argument("text", nargs="*")
    p = sub.add_parser("stress", help="type N bytes to exercise flow control")
    p.add_argument("bytes", type=int)
    p.add_argument("--abort-after", type=float, metavar="S", help="abort through CTRL after S seconds")
    p = sub.add_parser("combo", help="key combination, e.g. ctrl+alt+t, win+r, f5")
    p.add_argument("spec")
    p = sub.add_parser("media", help=f"media key: {', '.join(MEDIA)}")
    p.add_argument("key")
    p = sub.add_parser("config", help="show config, or set layout=, press=, release=")
    p.add_argument("settings", nargs="*")
    p = sub.add_parser("ota", help="update the firmware, e.g. .pio/build/hid/firmware.bin")
    p.add_argument("image")
    sub.add_parser("ping")
    sub.add_parser("abort", help="stop typing and drop everything pending")
    sub.add_parser("status", help="last Status reported by the dongle")


def split_line(line):
    """shlex-style splitting with quotes, but backslashes kept so Windows paths survive."""
    lexer = shlex.shlex(line, posix=True)
    lexer.whitespace_split = True
    lexer.escape = ""
    return list(lexer)


async def shell(bridge):
    parser = ShellParser(prog="", add_help=False)
    sub = parser.add_subparsers(dest="command", required=True, parser_class=ShellParser)
    add_commands(sub)
    sub.add_parser("verbose", help="toggle logging of credits and status events")
    sub.add_parser("help")
    sub.add_parser("quit")

    tasks = set()
    loop = asyncio.get_running_loop()
    print("commands: " + ", ".join(sub.choices) + "  (help for details)")
    while True:
        line = await loop.run_in_executor(None, input, "> ")
        if not line.strip():
            continue
        try:
            args = parser.parse_args(split_line(line))
        except (CommandError, ValueError) as e:
            if str(e):
                print(e)
            continue
        if args.command == "quit":
            break
        if args.command == "help":
            parser.print_help()
            continue
        if args.command == "abort":
            await execute(bridge, args)
            continue

        async def run(a=args):
            try:
                await execute(bridge, a)
            except CommandError as e:
                print(e)
            except asyncio.TimeoutError:
                print(f"{a.command}: timed out")
            except Exception as e:
                print(f"{a.command}: {type(e).__name__}: {e}")

        task = asyncio.create_task(run())
        tasks.add(task)
        task.add_done_callback(tasks.discard)

    for task in tasks:
        task.cancel()


async def find_device(args):
    if args.address:
        return args.address
    print(f"scanning for {repr(args.name) if args.name else 'a Keyling dongle'}...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: d.name == args.name if args.name else BRIDGE_SVC in adv.service_uuids,
        timeout=10)
    if not device:
        raise SystemExit("dongle not found")
    return device


async def run(args):
    target = await find_device(args)
    async with BleakClient(target) as client:
        if args.pair:
            print("pairing: enter the code shown on the dongle when Windows asks")
            await client.pair(protection_level=2)
        bridge = Bridge(client, args.verbose)
        await bridge.start()
        if args.command == "shell":
            await shell(bridge)
        else:
            try:
                await execute(bridge, args)
            except CommandError as e:
                raise SystemExit(str(e))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", help="advertised name to look for (default: any dongle with the bridge service)")
    ap.add_argument("--address", help="connect to this address instead of scanning")
    ap.add_argument("--pair", action="store_true", help="pair before running the command")
    ap.add_argument("-v", "--verbose", action="store_true", help="log credits and status events")
    sub = ap.add_subparsers(dest="command", required=True)
    add_commands(sub)
    sub.add_parser("shell", help="interactive session over a single connection")

    try:
        asyncio.run(run(ap.parse_args()))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()

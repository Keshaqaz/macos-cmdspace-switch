#!/usr/bin/env python3
"""kbdtest.py — post synthetic ⌘Space chords with controlled release timing and count how many
input-source changes each chord causes (0 = ignored, 1 = switched, 2 = switched and reverted).

Switches are counted from the unified log: TextInputMenuAgent reloads its preferences once per
input-source change (verified against real key presses earlier).

Safety: a listen-only event tap watches for real (non-synthetic) key events; the run waits while
the user is typing and aborts if the user types in the middle of it. The default layout is put
back at the end. Needs Accessibility trust (returns an error otherwise)."""
import argparse
import ctypes
import datetime
import random
import re
import subprocess
import threading
import time
from ctypes import CFUNCTYPE, c_bool, c_char_p, c_double, c_int, c_int32, c_long, c_ubyte, c_uint16, c_uint32, c_uint64, c_void_p

CF = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
CG = ctypes.CDLL("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics")
AX = ctypes.CDLL("/System/Library/Frameworks/ApplicationServices.framework/ApplicationServices")


def proto(fn, res, args):
    fn.restype, fn.argtypes = res, args
    return fn


proto(CF.CFRunLoopGetCurrent, c_void_p, [])
proto(CF.CFRunLoopAddSource, None, [c_void_p, c_void_p, c_void_p])
proto(CF.CFRunLoopRun, c_int, [])
proto(CF.CFRelease, None, [c_void_p])
COMMON_MODES = c_void_p.in_dll(CF, "kCFRunLoopCommonModes").value


class CGPoint(ctypes.Structure):
    _fields_ = [("x", c_double), ("y", c_double)]


TAPCB = CFUNCTYPE(c_void_p, c_void_p, c_uint32, c_void_p, c_void_p)
proto(CG.CGEventSourceCreate, c_void_p, [c_int32])
proto(CG.CGEventCreateKeyboardEvent, c_void_p, [c_void_p, c_uint16, c_bool])
proto(CG.CGEventSetFlags, None, [c_void_p, c_uint64])
proto(CG.CGEventSetIntegerValueField, None, [c_void_p, c_uint32, c_int])
proto(CG.CGEventGetIntegerValueField, c_int, [c_void_p, c_uint32])
proto(CG.CGEventPost, None, [c_uint32, c_void_p])
proto(CG.CGEventGetTimestamp, c_uint64, [c_void_p])
proto(CG.CGEventSetTimestamp, None, [c_void_p, c_uint64])
proto(CG.CFMachPortCreateRunLoopSource, c_void_p, [c_void_p, c_void_p, c_long])
proto(CG.CGEventTapCreate, c_void_p, [c_uint32, c_uint32, c_uint32, c_uint64, TAPCB, c_void_p])
proto(AX.AXIsProcessTrusted, c_bool, [])

MAGIC = 0x4B424454
KC_SPACE = 49
MODS = {"cmd": (55, 0x00100000), "ctrl": (59, 0x00040000), "opt": (58, 0x00080000)}
foreign = []          # times of real key events seen by the tap


def last_foreign_age():
    return time.time() - foreign[-1] if foreign else 1e9


def on_event(proxy, etype, event, refcon):
    try:
        if CG.CGEventGetIntegerValueField(event, 42) != MAGIC:
            foreign.append(time.time())
    except Exception:
        foreign.append(time.time())
    return event


def default_layout():
    out = subprocess.check_output(["defaults", "read", "com.apple.HIToolbox", "AppleSelectedInputSources"], text=True)
    m = re.search(r'"KeyboardLayout Name" = ([^;]+);', out)
    return m.group(1).strip() if m else None


RAND = {}        # name -> (min, max) in ms, drawn again for every chord
RAND_LOG = []    # (pre, hold, gap) actually used, per chord

ap = argparse.ArgumentParser()
ap.add_argument("--n", type=int, default=0)
ap.add_argument("--gap", type=int, default=40, help="ms between Space up and Cmd up (0 = back to back)")
ap.add_argument("--pre", type=int, default=25, help="ms between Cmd down and Space down")
ap.add_argument("--hold", type=int, default=30, help="ms Space is held")
ap.add_argument("--settle", type=float, default=0.8, help="s to wait after each chord")
ap.add_argument("--label", default="")
ap.add_argument("--mods", default="cmd", help="modifiers to hold: any of cmd,ctrl,opt joined by +")
ap.add_argument("--fn", action="store_true", help="press the globe/Fn key alone instead of a chord")
ap.add_argument("--set-layout", help="instead of a test: toggle until the default layout has this name")
ap.add_argument("--fake-hold", type=int, help="ms: stamp Space up as if Space had been held this long (delivery timing unchanged)")
ap.add_argument("--fake-gap", type=int, help="ms: stamp Cmd up this long after Space up (delivery timing unchanged)")
ap.add_argument("--order", default="space-first", choices=["space-first", "cmd-first"],
                help="which of the two keys is released first (cmd-first = negative gap)")
ap.add_argument("--rand", default="", help='per-chord random timing, e.g. "pre:0:30,hold:10:120,gap:-5:5" (ms)')
ap.add_argument("--seed", type=int, default=0)
args = ap.parse_args()
if args.seed:
    random.seed(args.seed)
for part in filter(None, args.rand.split(",")):
    name, lo, hi = part.split(":")
    RAND[name] = (float(lo), float(hi))

if not AX.AXIsProcessTrusted():
    raise SystemExit("not trusted for Accessibility: cannot post or observe key events")
held = [MODS[m] for m in args.mods.split("+") if m]
all_flags = sum(f for _, f in held)

layout_before = default_layout()
print("default layout before:", layout_before, flush=True)
CB = TAPCB(on_event)
tap_port = CG.CGEventTapCreate(1, 0, 1, (1 << 10) | (1 << 11) | (1 << 12), CB, None)
print("safety tap:", "ok" if tap_port else "FAILED", flush=True)


def tap_thread(port):
    rl = CF.CFRunLoopGetCurrent()
    CF.CFRunLoopAddSource(rl, CG.CFMachPortCreateRunLoopSource(None, port, 0), COMMON_MODES)
    CF.CFRunLoopRun()


if tap_port:
    threading.Thread(target=tap_thread, args=(tap_port,), daemon=True).start()
    time.sleep(0.3)
    if last_foreign_age() < 2.5:
        raise SystemExit("user is typing right now, not running")

src = CG.CGEventSourceCreate(1)


def post(keycode, down, flags, ts=None):
    """post one key event; ts (ns) overrides the event's own timestamp"""
    e = CG.CGEventCreateKeyboardEvent(src, keycode, down)
    CG.CGEventSetFlags(e, flags)
    CG.CGEventSetIntegerValueField(e, 42, MAGIC)
    if ts is not None:
        CG.CGEventSetTimestamp(e, ts)
    t = CG.CGEventGetTimestamp(e)
    CG.CGEventPost(0, e)
    CF.CFRelease(e)
    return t


def timing(name, fixed):
    if name not in RAND:
        return fixed / 1000.0
    lo, hi = RAND[name]
    return random.uniform(lo, hi) / 1000.0


def chord(gap_ms):
    if args.fn:  # the 🌐 key alone (keycode 63); "Change Input Source" is its configured action
        post(63, True, 0x800000)
        time.sleep(args.hold / 1000.0)
        post(63, False, 0)
        return
    if args.order == "cmd-first":
        gap_ms = -gap_ms
    pre, hold, gap = timing("pre", args.pre), timing("hold", args.hold), timing("gap", gap_ms)
    RAND_LOG.append((round(pre * 1000, 1), round(hold * 1000, 1), round(gap * 1000, 1)))
    for keycode, flags in held:
        post(keycode, True, flags)
        time.sleep(pre)
    ts_down = post(KC_SPACE, True, all_flags)
    time.sleep(hold)

    def space_up():
        return post(KC_SPACE, False, all_flags, None if args.fake_hold is None else ts_down + args.fake_hold * 10**6)

    def mods_up(ts_up):
        for keycode, _ in reversed(held):
            post(keycode, False, 0, None if args.fake_gap is None else ts_up + args.fake_gap * 10**6)
            time.sleep(0.02)

    if gap >= 0:      # Space released first, ⌘ `gap` ms later
        ts_up = space_up()
        time.sleep(gap)
        mods_up(ts_up)
    else:             # ⌘ released first, Space `gap` ms later
        mods_up(ts_down)
        time.sleep(-gap)
        space_up()


def switches_between(t_from, t_to):
    fmt = "%Y-%m-%d %H:%M:%S"
    cmd = ["/usr/bin/log", "show", "--style", "compact", "--start",
           datetime.datetime.fromtimestamp(t_from - 1).strftime(fmt), "--end",
           datetime.datetime.fromtimestamp(t_to + 1).strftime(fmt), "--predicate",
           'process == "TextInputMenuAgent" AND eventMessage CONTAINS "Loading Preferences"']
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=300).stdout
    ts = []
    for line in out.splitlines():
        m = re.match(r"\d{4}-\d\d-\d\d (\d\d):(\d\d):(\d\d)\.(\d{3}) ", line)
        if m:
            h, mi, s, ms = m.groups()
            base = datetime.datetime.fromtimestamp(t_from).replace(hour=0, minute=0, second=0, microsecond=0)
            ts.append(base.timestamp() + int(h) * 3600 + int(mi) * 60 + int(s) + int(ms) / 1000.0)
    return ts


def run_chord(gap_ms):
    deadline = time.time() + 20
    while last_foreign_age() < 2.5:
        if time.time() > deadline:
            raise SystemExit("aborted: the user is typing")
        time.sleep(0.2)
    t0 = time.time()
    chord(gap_ms)
    time.sleep(args.settle)
    return t0


def count_in(starts):
    # a chord's own switch lands within ~150 ms of its start; the next chord starts >= 0.8 s later
    time.sleep(1.5)  # let the last log lines reach the store
    t_from, t_to = starts[0] - 0.05, starts[-1] + 0.5
    ts = switches_between(t_from, t_to)
    return [sum(1 for s in ts if t0 - 0.05 <= s <= t0 + 0.5) for t0 in starts]


if args.n == 0 and not args.set_layout:
    raise SystemExit(0)

if args.set_layout:
    tries = 0
    while default_layout() != args.set_layout and tries < 3:
        run_chord(40)
        tries += 1
    raise SystemExit("default layout now: %s (wanted %s, %d chords)" % (default_layout(), args.set_layout, tries))

starts = [run_chord(args.gap) for _ in range(args.n)]
results = count_in(starts)
for i, (t0, n) in enumerate(zip(starts, results)):
    timing_used = " pre=%.1f hold=%.1f gap=%.1f" % RAND_LOG[i] if RAND_LOG else ""
    print("%s #%02d %s changes=%d%s" % (args.label, i + 1, datetime.datetime.fromtimestamp(t0).strftime("%H:%M:%S.%f")[:-3], n, timing_used), flush=True)
print("SUMMARY %s mods=%s gap=%dms order=%s ts(hold=%s gap=%s): n=%d  ignored=%d  switched=%d  reverted=%d" % (
    args.label, "fn" if args.fn else args.mods, args.gap, args.order, args.fake_hold, args.fake_gap, len(results), results.count(0), results.count(1), results.count(2)), flush=True)

layout_after = default_layout()
tries = 0
while default_layout() != layout_before and tries < 4:
    run_chord(40)  # a reverted chord changes nothing, so retry until the original layout is back
    tries += 1
if layout_after != layout_before:
    print("restored default layout: %s -> %s (%d extra chords)" % (layout_after, default_layout(), tries))

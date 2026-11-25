#!/usr/bin/env python3
"""Read or set the input-source symbolic hotkeys the same way System Settings does
(SkyLight CGSSetSymbolicHotKeyValue / CGSSetSymbolicHotKeyEnabled), then show live state vs prefs.

  hotkeys.py                                   show live state vs prefs
  hotkeys.py set <id> <char> <keyCode> <mods>  e.g. set 61 65535 49 0x100000  (⌘Space)
  hotkeys.py enable <id> <0|1>                 e.g. enable 60 1
"""
import ctypes
import plistlib
import subprocess
import sys
from ctypes import POINTER, c_bool, c_int, c_uint16, c_uint32, c_ubyte

sl = ctypes.CDLL("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight")
sl.CGSSetSymbolicHotKeyValue.restype = c_int
sl.CGSSetSymbolicHotKeyValue.argtypes = [c_int, c_uint16, c_uint16, c_uint32]   # (id, char, keyCode, mods)
sl.CGSSetSymbolicHotKeyEnabled.restype = c_int
sl.CGSSetSymbolicHotKeyEnabled.argtypes = [c_int, c_ubyte]                       # (id, enabled)
htb = sl
sl.CGSIsSymbolicHotKeyEnabled.restype, sl.CGSIsSymbolicHotKeyEnabled.argtypes = c_int, [c_int]
sl.CGSGetSymbolicHotKeyValue.restype = c_int
sl.CGSGetSymbolicHotKeyValue.argtypes = [c_int, POINTER(c_uint16), POINTER(c_uint16), POINTER(c_uint32)]

NAMES = {60: "previous input source", 61: "next source in Input menu", 64: "Spotlight", 65: "Finder search"}


def describe(char, key, mods):
    m = "".join(n for bit, n in ((0x100000, "⌘"), (0x80000, "⌥"), (0x40000, "⌃"), (0x20000, "⇧")) if mods & bit)
    return "%s%s" % (m or "(no modifier)", chr(char) if 32 < char < 0xF700 else "<key %d>" % key)


def show():
    prefs = plistlib.loads(subprocess.check_output(["defaults", "export", "com.apple.symbolichotkeys", "-"]))["AppleSymbolicHotKeys"]
    for hid in (60, 61):
        ch, kc, md = c_uint16(), c_uint16(), c_uint32()
        sl.CGSGetSymbolicHotKeyValue(hid, ctypes.byref(ch), ctypes.byref(kc), ctypes.byref(md))
        p = prefs.get(str(hid), {})
        print("%d %-26s live: enabled=%d %-16s | prefs: enabled=%s %s" % (
            hid, NAMES[hid], sl.CGSIsSymbolicHotKeyEnabled(hid), describe(ch.value, kc.value, md.value),
            p.get("enabled"), p.get("value", {}).get("parameters")))


argv = sys.argv[1:]
if argv and argv[0] == "set":
    print("CGSSetSymbolicHotKeyValue ->", sl.CGSSetSymbolicHotKeyValue(int(argv[1]), int(argv[2], 0), int(argv[3], 0), int(argv[4], 0)))
elif argv and argv[0] == "enable":
    print("CGSSetSymbolicHotKeyEnabled ->", sl.CGSSetSymbolicHotKeyEnabled(int(argv[1]), c_ubyte(int(argv[2]))))
elif argv:
    raise SystemExit(__doc__)
show()

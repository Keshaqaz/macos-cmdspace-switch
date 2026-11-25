# ⌘Space layout switch on the user's MacBook Air (Mac16,12, macOS 27.2.1)

Status 2026-11-25: resolved via `cmdspace-switch` (`local.bibs.cmdspace-switch` LaunchAgent, compiled from `cmdspace-switch.c`). It disables symbolic hotkey 60 (`CGSSetSymbolicHotKeyEnabled(60, 0)`), registers `⌘Space` with Carbon `RegisterEventHotKey`, and cycles `TISSelectInputSource` on `kEventHotKeyPressed` (zero TCC permissions). Manage or revert via `./cmdspace-switch-ctl.sh {install|uninstall|status}` (`uninstall` restores hotkey 60). Note: never disable 60 without this helper running ("next source" 61 alone on ⌘Space does not work on 27.2.1).

## Symptom
⌘Space sometimes "does nothing": the layout switches and reverts ~10–13 ms later (two input-source changes in the log). Real pressing: 9 of 46 presses in one evening.

## Cause
macOS bug in symbolic hotkey 60 ("Select the previous input source"): when Space-up and ⌘-up land in the same event batch the hotkey fires twice. Same as FB24297598 (Apple Developer Forums thread 841799).
Measured with synthetic chords (`tools/kbdtest.py`): release gap 0 ms → 22/22 reverted; gap ≥ 3 ms (3, 6, 10, 20, 30 ms) → 0/66 reverted.

## What does not help
- Duplicate binding (61 "next source" also on ⌘Space) is irrelevant: identical results with and without it. Current config: 60 = ⌘Space, 61 = ⌥⌃Space (default).
- Hotkey 61 alone on ⌘Space does NOT work on 27.2.1: Space is delivered to the app as a plain key, no switch, even when set through the same API System Settings uses. Disabling 60 kills switching entirely. (Advised this once by mistake from a macOS 26.1 report; the user had to be rescued.)
- Third-party apps are not involved: Logi Options+ and Mac Mouse Fix keyboard event taps are disabled; Orca's `orca-keyboard-layout` helper only reacts to switches.

## Untested
The 🌐 key is set to "Change Input source" (AppleFnUsageType = 1), but synthetic Fn events are ignored by the system, so it could not be tested.

## Diagnostics that worked
- Unified log: WindowServer category `com.apple.SkyLight:KeyboardEvent` gives event types (`kCGSEventKeyDown` / `KeyUp` / `FlagsChanged`; Space consumed by the hotkey is not logged). `TextInputMenuAgent` "Loading Preferences From User CFPrefsD" = one input-source change.
- `defaults write com.apple.symbolichotkeys` does NOT apply live. System Settings uses SkyLight `CGSSetSymbolicHotKeyEnabled(id, enabled)` and `CGSSetSymbolicHotKeyValue(id, char, keyCode, mods)`; use those (also updates prefs). `CGSGetSymbolicHotKeyValue(id, &char, &key, &mods)` and `CGSIsSymbolicHotKeyEnabled(id)` show the live state (`tools/hotkeys.py`).
- `TISCopyCurrentKeyboardInputSource` in an unrelated process does not reflect switches (selection is per context); `com.apple.HIToolbox AppleSelectedInputSources` does.
- A wrong call into PreferencePanes `SetSymbolicHotKeyValue` segfaults (first argument is a CFDictionary).

## Possible real fix, if the user asks
Own hotkey tool: Carbon `RegisterEventHotKey` on ⌘Space + `TISSelectInputSource`, with system hotkey 60 disabled (otherwise both fire).

## Scripts
`tools/kbdtest.py` (posts synthetic chords, counts switches from the log, waits while the user types, restores the default layout) and `tools/hotkeys.py` (live view / set / enable).

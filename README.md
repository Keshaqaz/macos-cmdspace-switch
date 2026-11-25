# macos-cmdspace-switch

A minimal macOS `LaunchAgent` helper written in C that fixes the macOS 26/27 `⌘Space` input-source double-switch bug (**FB24297598**) using Carbon `RegisterEventHotKey` and `TISSelectInputSource` — with **zero** Accessibility or Input Monitoring permissions.

## The Problem (FB24297598)

On macOS 26 and 27, the built-in **"Select the previous input source"** shortcut (symbolic hotkey `60`, typically `⌘Space` or `⌃Space`) has a timing bug when both keys are released close together:

- If `Space`-up and `⌘`-up (`⌃`-up) land in the same event batch, macOS triggers symbolic hotkey `60` **twice**.
- The input source switches and immediately reverts ~10–500 ms later (~10–21 ms observed locally on macOS 27.2.1 with `⌘Space`; 104–588 ms reported in Apple Developer Forums thread 841799 with `⌃Space` on macOS 26.5.2), making the shortcut appear to do nothing.
- Synthetic chord testing (`kbdtest.py`) confirmed **22/22** double-switch reverts at a `0 ms` release gap vs **0/66** at `>= 3 ms` release gaps.

## How `cmdspace-switch` Fixes It

Instead of trying to delay or rewrite key-up events in the HID stream, `cmdspace-switch` replaces symbolic hotkey `60` cleanly at the Carbon hotkey layer:

1. **Disables symbolic hotkey 60 live** via SkyLight (`CGSSetSymbolicHotKeyEnabled(60, 0)`) while active, preventing macOS's buggy release-path handler from firing alongside it.
2. **Registers `⌘Space` (`kVK_Space`, `cmdKey`)** via Carbon `RegisterEventHotKey` (requesting `kEventHotKeyExclusive`, with fallback to `0` on `eventHotKeyExistsErr`) on the main Carbon event loop (`ReceiveNextEvent`).
3. **Switches on key-down (`kEventHotKeyPressed`)** by cycling through enabled, select-capable keyboard input sources (`kTISCategoryKeyboardInputSource`) via `TISCopyCurrentKeyboardInputSource` and `TISSelectInputSource`, automatically refreshing the list when `kTISNotifyEnabledKeyboardInputSourcesChanged` fires.

Because switching happens once on `kEventHotKeyPressed` (key-down), key-up order and simultaneous release timing no longer matter.

## Why This Architecture?

Compared to `CGEventTap` workarounds or heavy remapping suites (Karabiner-Elements, Hammerspoon, etc.):

- **Zero TCC permissions**: Carbon `RegisterEventHotKey` and `TISSelectInputSource` require no Accessibility, Input Monitoring, or DriverKit permissions — and rebuilds never invalidate TCC trust.
- **Zero input-path stall / no `FB24619068` freeze risk**: No active `CGEventTap` sits inline in WindowServer's event stream, eliminating `kCGEventTapDisabledByTimeout` dropouts and avoiding the system-wide keyboard/mouse hang (`FB24619068`) triggered if Accessibility is revoked while an active session tap exists.
- **Works under Secure Input**: Carbon `RegisterEventHotKey` continues to deliver `⌘Space` even when Secure Input is active (password fields, terminal Secure Keyboard Entry, lock/auth prompts), whereas `CGEventTap` stops receiving events.
- **Immune to `⌘W` / `⌘Q` phantom shortcuts during fast rolling typing**: Because no tap delays, holds back, or re-synthesizes `⌘` or `Space` events in flight, fast rolling chords after `⌘Space` cannot suffer modifier-state desync or leaked keystrokes.
- **`flock` single-instance guard**: Uses an exclusive non-blocking `flock` lock (`~/Library/Caches/local.bibs.cmdspace-switch.lock`, falling back to `/tmp/local.bibs.cmdspace-switch.<uid>.lock`) so duplicate instances cannot race.

## Quick Start

```bash
git clone https://github.com/Keshaqaz/macos-cmdspace-switch.git
cd macos-cmdspace-switch

make           # compile cmdspace-switch
make install   # substitute paths, lint plist, install & start LaunchAgent
make status    # inspect LaunchAgent, running process, and live symbolic hotkeys
```

### Rollback & Management

To stop the helper and restore native macOS `⌘Space` handling:

```bash
make uninstall
```

`make uninstall` boots out `local.bibs.cmdspace-switch`, removes `~/Library/LaunchAgents/local.bibs.cmdspace-switch.plist`, and re-enables symbolic hotkey `60` live via `hotkeys.py enable 60 1`.

- **Logs**: `~/Library/Logs/cmdspace-switch.log`
- **CLI options**:
  - `--list` — print enabled select-capable keyboard input sources and exit
  - `--keep-hotkey60-disabled-on-exit` (or `--no-restore-on-exit`) — keep hotkey `60` disabled on exit (used by the `launchd` `KeepAlive` plist so a restart window never double-fires)
  - `-v`, `--verbose` — log every input-source switch
  - `-h`, `--help` — show usage

## Trade-offs & Caveats

- **Hold-to-cycle chooser popup**: The native press-and-hold input source switcher popup belongs to symbolic hotkey `60` and does not appear while `cmdspace-switch` is active; switching occurs immediately on key-down.
- **Plain keyboard layouts vs CJKV input methods**: Designed and verified for plain keyboard layouts (`com.apple.keylayout.*`, e.g., U.S. ↔ Russian). Calling `TISSelectInputSource` directly on certain CJKV input methods has known macOS quirks where the frontmost app may not reflect the new IME until focus changes.
- **Per-document auto-switch**: If **System Settings → Keyboard → Text Input → Edit… → "Automatically switch to a document's input source"** is enabled, macOS's per-document memory can override programmatic switches when changing focus or documents.

## Verification & Repository Layout

- `cmdspace-switch.c` — C implementation (Carbon `RegisterEventHotKey`, `TISSelectInputSource`, `flock` lock, SkyLight hotkey `60` control, `SIGINT`/`SIGTERM` dispatch handlers).
- `Makefile` — build (`clang -O2 -Wall -Wextra -Werror`) and `install` / `uninstall` / `status` / `clean` targets.
- `local.bibs.cmdspace-switch.plist` — `launchd` LaunchAgent template (`RunAtLoad`, `KeepAlive`, `LimitLoadToSessionType = Aqua`) with `__BIN_DIR__` and `__LOG_DIR__` placeholders.
- `cmdspace-switch-ctl.sh` — lifecycle script (`install`, `uninstall`, `status`) that substitutes placeholders, validates with `plutil -lint`, and manages `launchctl`.
- `hotkeys.py` — diagnostic script to inspect live SkyLight symbolic hotkey state vs `com.apple.symbolichotkeys` prefs, or toggle/set hotkeys live.
- `kbdtest.py` — synthetic `⌘Space` chord timing test harness that varies key-up gaps and counts resulting input-source changes in the unified log (`TextInputMenuAgent`). Requires Accessibility permission when run manually for testing.
- `docs/` — local investigation notes, empirical measurements, web/bug-report survey, and architecture comparison (`keyboard-layout-switch.md`, `keyboard-layout-switch-reports.md`, `keyboard-layout-switch-tools.md`).

## License

[MIT](LICENSE)

# ⌘Space double-switch: technical options (research notes, 2026-11-25)

Goal: a small LaunchAgent helper that stops symbolic hotkey 60 ("Select the previous input source") from firing twice when Space and ⌘ are released together. Layouts on this Mac: U.S. and RussianWin (both `com.apple.keylayout.*`, no input methods).

Legend: **[src]** read in a cloned repo or in the SDK header on this Mac (`MacOSX27.2.sdk`, line numbers given) · **[local]** measured on this Mac (macOS 27.2.1) with probes that never change the input source or post events (one registered an unused Carbon hotkey for one second) · **[reported]** third-party statement (Apple DTS post, issue, maintainer) that I did not reproduce · **[inference]** my conclusion.
Apple Developer Forums pages are behind a bot check and could not be fetched directly; quotes from them come from search-engine renderings of the page.

## 0. Facts measured on this Mac [local]

- Symbolic hotkeys: 60 = enabled, parameters `[32, 49, 1048576]` (⌘Space); 61 = enabled, `[65535, 49, 786432]` (⌥⌃Space); Spotlight 64 = disabled; Finder search 65 = ⌥⌘Space. `AppleFnUsageType` is present in `com.apple.HIToolbox`; `AppleGlobalTextInputProperties` (per-document switch setting) is absent, i.e. at its default — not read.
- Carbon `InstallEventHandler(GetEventDispatcherTarget(), …)`, `RegisterEventHotKey` (exclusive, ⌃⌥⇧⌘F20) and `UnregisterEventHotKey` all return `noErr` from a plain, non-bundled CLI process, with no prompt.
- Preflights for this shell (responsible process = the Orca app hosting it): `AXIsProcessTrusted`=1, `CGPreflightListenEventAccess`=1, `CGPreflightPostEventAccess`=1, `IOHIDCheckAccess(ListenEvent)`=Granted, `IOHIDCheckAccess(PostEvent)`=Granted. So on 27.2.1 an Accessibility-trusted process also passes the "listen" check. A LaunchAgent binary is its own responsible process, so this does not carry over.
- Reading the current input source from an unrelated process:
  - long-running CLI **without a run loop** (bare sleep/poll): `TISCopyCurrentKeyboardInputSource()` stayed `com.apple.keylayout.US` for 150 s while `com.apple.HIToolbox AppleSelectedInputSources` flipped about 100 times → stale. This confirms the earlier note in `keyboard-layout-switch.md`.
  - long-running CLI **with `RunLoop.main.run()`**: TIS followed every switch within 1–2 ms. Fresh processes are always correct.
  - `AppleSelectedInputSources` is rewritten in two steps: for about 8 ms the array contains only `com.apple.PressAndHold` and no keyboard layout.
- The bug was visible live in that data: in failing trials the layout switched and reverted 15–21 ms later. The switches came in regular 0.83 s batches, which looks like the parent session running synthetic-chord tests [inference]; I ran no test that changes the input source or posts events.

## A. Active CGEventTap that delays one key-up

### A1. Permissions
- [src] `CoreGraphics/CGEvent.h:269-274`: session/HID/annotated taps "may only receive key up and down events if access for assistive devices is enabled … or the caller is enabled for assistive device access"; otherwise the key bits are cleared from the mask and `CGEventTapCreate` returns NULL. `CGEvent.h:398-408` adds `CGPreflightListenEventAccess` / `CGRequestListenEventAccess` ("event listening access") and `CGPreflightPostEventAccess` / `CGRequestPostEventAccess` ("event synthesizing access"), both macOS 10.15+.
- [reported] Apple DTS (K. Elliott, forum 828052, macOS 15): "Accessibility" basically grants event posting and listening, "Input Monitoring" only listening; after Accessibility is granted the tap succeeds "as Accessibility specifically grants both post and listen".
- [reported] Apple DTS (Quinn “The Eskimo!”, forum 744440): with CGEventTap alone "you only need the Accessibility privilege if you're doing other stuff with Accessibility APIs"; (forum 811443) a listen-only CGEventTap "relies on the Input Monitoring privilege, rather than the Accessibility privilege".
- [src] Karabiner-Elements `permission_checker.hpp:67-70`: "On macOS 26, Accessibility permission may also cover Input Monitoring permission. (Note that on macOS 14, Input Monitoring permission is still required separately even if Accessibility is granted.)". Karabiner docs say the same.
- [reported] forum 844416: for an active `.defaultTap` WindowServer checks the process's `kTCCServicePostEvent` / `kTCCServiceListenEvent`.
- What projects require: skhd only Accessibility (`src/skhd.c:356-372`, README); Hammerspoon only Accessibility (`libeventtap.m:229` "Is Accessibility enabled?"); Rectangle ("Only a trusted process can create a tap", `EventMonitor.swift:80-85`); InputSourcePro requires **both**, even for its listen-only tap (`ShortcutTrigger.swift:367-371`).
- [reported] TCC for a bare binary keys on its path; a grant is tied to the code signature, so every rebuild silently invalidates it (remove the entry and add it again, or sign with a stable identity). A LaunchAgent must be granted on the exact binary in `ProgramArguments`. Same symptom in Stack Overflow 79518299: after a rebuild posting events failed although `AXIsProcessTrusted()` still returned true, fixed by removing and re-adding the app.

Conclusion [inference]: an active tap needs Accessibility; ask for Input Monitoring as well on macOS 14, and treat `CGEventTapCreate != NULL` as the only real test.

### A2. Re-injecting and delaying inside the callback
- [src] `CGEvent.h:335-341`: `CGEventTapPostEvent` "post[s] a new event at the same point to which an event returned from an event tap callback function would be posted. The new event enters the system before the event returned by the callback enters the system. Events posted into the system will be seen by all taps placed after the tap posting the event."
- [src] `CGEventTypes.h:437-453`: an active filter returns the (modified) event, a new event, or NULL to delete; "The event passed to the callback is retained by the calling code, and is released after the callback returns and the data is passed back to the event system" — i.e. that event (and the stream behind it) waits for the callback. A listen-only tap cannot divert events.
- [reported] forum 734237: a listen-only handler that sleeps 2 s does not block other handlers.
- Sleeping: no project I read sleeps in an active tap as a feature. The tap timeout is not documented by Apple; third parties report about 1 s (262_KeyLens #376 "within its ~1 s timeout"; Keyhac docs "disabled after a callback stalls 1.0–1.5 s"). 8 ms is far below that, but the whole input stream is stalled for those 8 ms.
- The proxy is only used inside the callback by every project; nothing documents that it stays valid afterwards.
- `CGEvent.h:349-350`: `CGEventPost` enters "immediately before any event taps instantiated for that location, and the event passes through any such taps" — a re-posted event reaches the own tap again and needs a marker (`kCGEventSourceUserData`, as InputSourcePro does, `InputSourceSwitcher.swift:805-807`).

### A3. How open-source projects do it
| Project | Tap | Replace / re-inject |
|---|---|---|
| skhd | `kCGSessionEventTap`, head, active (`src/event_tap.c:11-16`) | swallows by returning NULL (`src/skhd.c:171-180`); re-enables on DisabledBy* (`src/skhd.c:135-140,165-170`); synthesizes with `CGEventPost(kCGAnnotatedSessionEventTap)` + `usleep(1000)` (`src/synthesize.c:80-83`), after zeroing `CGSetLocalEventsSuppressionInterval` (`:54-55`) |
| Hammerspoon `hs.eventtap` | session, head, active (`extensions/eventtap/libeventtap.m:217-222`) | callback returns `true` → event deleted, or a table of events → `CGEventTapPostEvent(proxy, newEvent)` (`:59-78`); `event:post()` = `CGEventPost(kCGSessionEventTap)` + `usleep(1000)` (`libeventtap_event.m:688-695`) |
| yabai | `kCGHIDEventTap` as root (`src/mouse_handler.c:278`) | **hold-and-reinject pattern**: mouse-down deleted and retained (`:36-39`), on mouse-up both events re-injected with `CGEventTapPostEvent` (`:46-55`) |
| Rectangle | session, head, active, on its own `userInteractive` thread (`EventMonitor.swift:83-89`) | only filters (returns nil, `:137`); re-enables on DisabledBy* (`:112-121`); `CFMachPortInvalidate` on stop because stale taps "degrad[e] system-wide input latency" (`:102-108`) |
| Karabiner-Elements | `kCGHIDEventTap` as root, listen-only by default, active only in the CGEventTap fallback (`event_tap_monitor.hpp:121-148`) | does **not** re-post through CGEvent; output goes to a DriverKit virtual HID device (`DEVELOPMENT.md:152-180`, `docs/cgeventtap-fallback.md:13-34`) |
| InputSourcePro | session, head, **listen-only** (`ShortcutTrigger.swift:401-426`) | does not modify events; triggers the system hotkey by posting synthetic ⌘Space to `.cghidEventTap` (`InputSourceSwitcher.swift:765-803`) |

### A4. Pitfalls
- `kCGEventTapDisabledByTimeout` / `kCGEventTapDisabledByUserInput` are "out of band" events (`CGEventTypes.h:128-131`); the remedy is `CGEventTapEnable(tap, true)` (`CGEvent.h:320-326`), done by skhd, Hammerspoon, Rectangle, Karabiner, InputSourcePro. [reported] DisabledByUserInput arrives when the display turns off / the lock screen engages Secure Input (deskflow #10296) and when `CGEventTapEnable(false)` is called (SO 65514367). The recovery code is easy to get wrong: glazewm #1419 (callback ignored the sentinel types), FluidVoice #921 (the handler must not run on a run loop that the stall itself starves), Cursr #129 (disabled by timeout several times a day on Tahoe, re-enable did not always succeed).
- Secure Input: "no events are delivered to CGEventTap" (Karabiner `docs/cgeventtap-fallback.md:36-51`); the tap stays enabled but silent (ghostty #14232).
- [reported] Revoking Accessibility while an active session tap exists makes keyboard and mouse input unresponsive system-wide until a forced restart; reproduced on Sequoia, Tahoe and 27, filed as FB24619068 (forum 844416; earlier forum 735204). A wedged filter tap also wedges everything it filters (FluidVoice #921: clicks dead system-wide).
- Timestamps: a retained event keeps its original timestamp ("roughly, nanoseconds since startup", `CGEventTypes.h:134`); `CGEventSetTimestamp` exists (`CGEvent.h:153`). Nothing says whether the hotkey handler keys on delivery time or on timestamps — the earlier kbdtest.py experiment changed both.
- [reported] `CGEventPost` output differs from hardware for fn/Globe (Karabiner `DEVELOPMENT.md:170-180`); `kCGEventSourceUnixProcessID` and `kCGEventSourceUserData` are what tools use to recognise their own events.
- Session taps sit before the system hotkeys: [reported] a Hammerspoon active tap that swallowed ⌘Space also stopped Spotlight and `hs.hotkey` (Hammerspoon #3058). [inference] so a tap that only delays events leaves hotkey 60 working.

### A5. Assessment [inference]
Plausible and small, but it is unverified that delaying *delivery* (not timestamps) is enough, and it puts an always-on active filter into the input path with the failure modes in A4 (permission revoked → input hang; timeout/user-input disable; Secure Input). Needs an experiment: active tap, sleep 8 ms on the first release of the ⌘Space chord, run the kbdtest.py matrix; if it still fails, also bump the timestamp of the delayed event.

## B. Take the shortcut over: `RegisterEventHotKey` + `TISSelectInputSource`

### B1. Works from a background process, without permissions
- [src] `HIToolbox/TextInputSources.h:952-985`: `TISSelectInputSource` "makes the specified input source the new current keyboard input source"; it needs `kTISPropertyInputSourceIsSelectCapable` and the source to be enabled. No process or permission requirement is stated.
- [src] Plain CLI tools do exactly this: im-select `main.m:17-21` (one call, no GUI, no permission); macism `InputSourceManager.swift:33-56` — for non-CJKV sources it is a bare `TISSelectInputSource` ("fcitx and non-CJKV don't need special treat", `:46-51`).
- [reported] The TIS path is not affected by the bug: 25 selections via `TISSelectInputSource`, 0 reverted (forum 841799); InputSourcePro's maintainer says simulating the system shortcut misbehaves on macOS 26 and recommends a shortcut bound to a switch group, which is plain `TISSelectInputSource` (InputSourcePro #79, `IndicatorVM.swift:443-455`).
- [reported] Carbon `RegisterEventHotKey` "is not affected by Secure Input, and it requires no Accessibility permission at all" (ghostty #14232). [src] `CarbonEvents.h:15404-15490`: hotkeys are normally non-exclusive ("notification events are sent to all registrants"); `eventHotKeyExistsErr` only for exclusive-vs-exclusive.
- [local] Registration works from a non-bundled CLI (section 0). Running it as a LaunchAgent is [inference]: no open-source CLI LaunchAgent using `RegisterEventHotKey` was found (Hammerspoon and `sindresorhus/KeyboardShortcuts` are app bundles), but nothing in the API depends on a bundle.
- Known limits of `TISSelectInputSource` concern input **methods**, not layouts: Apple forum 748791 (Korean/Japanese not applied in the frontmost app until focus changes), rime/squirrel #1140 and #1162 (macOS 26), Karabiner docs (CJKV). karabiner-cjk-helper reports "Colemak ↔ Russian 100%".

### B2. What tools use
| Tool | Hotkey | Switch |
|---|---|---|
| Hammerspoon | `RegisterEventHotKey(…, GetEventDispatcherTarget(), kEventHotKeyExclusive, …)` (`extensions/hotkey/libhotkey.m:258`, handler `:442-452`) | `TISCopyCurrentKeyboardInputSource` / `TISSelectInputSource` (`extensions/keycodes/libkeycodes.m:343-368`) |
| InputSourcePro | `KeyboardShortcuts` package → `RegisterEventHotKey(…, GetEventDispatcherTarget(), 0, …)` (`HotKey.swift:269-288`); a listen-only tap only to invalidate modifier shortcuts | `TISSelectInputSource` (`InputSourceSwitcher.swift:510-518`) |
| Karabiner-Elements | HID grabber + virtual device | sends `select_input_source` to the per-user ConsoleUserServer (`console_user_server_peer.hpp:83-85`), which calls `TISSelectInputSource` (`pqrs/osx/input_source/input_source.hpp:63-69`) |
| im-select, macism, Karabiner `shell_command` users | none (CLI) | `TISSelectInputSource` |

### B3. What the user loses or changes
- The hold-to-cycle chooser popup belongs to the system hotkey and is gone by design; key-up timing no longer matters because the Carbon hotkey fires on key-down [inference].
- The small caret-adjacent indicator (Sonoma and later; `defaults write kCFPreferencesAnyApplication TSMLanguageIndicatorEnabled 0` hides it): no source says whether it is shown after a `TISSelectInputSource` switch. InputSourcePro draws its own indicator. Needs a one-minute visual test.
- Per-document behaviour: Apple describes "Automatically switch to a document's input source" as remembering a source per document. [reported] with it ON, third-party switching is overridden by the app/document setting (autokbisw #33). [reported, code comment] macism `TSMUtils.swift:4-15`: `TISSelectInputSource` from a process without keyboard focus "commits the new source system-wide before sending the focused app this message". [inference] with the option off and two layouts, B is equivalent to hotkey 60.
- "Previous input source" history is replaced by "the other one of the two" — identical while only U.S. and RussianWin are enabled [inference].
- Hotkey 60 must be disabled, otherwise it is either unassignable or fires together with the helper (double switch again) [inference]. Hammerspoon docs: the most common reason a combination cannot be assigned is that macOS uses it, and only some of those can be overridden; its code warns that `eventHotKeyExistsErr` may mean "registered by macOS" (`libhotkey.m:265-266`).
- Safety order, because 60 was disabled once before with no replacement and switching died: start the helper and confirm its hotkey fires **before** disabling 60; keep a one-command uninstall that re-enables 60 (`tools/hotkeys.py` / `CGSSetSymbolicHotKeyEnabled`). If the helper dies, ⌘Space does nothing until launchd restarts it; ⌥⌃Space (61) and the 🌐 key remain as fallbacks [local].

### B4. Reading the current source reliably
[local, section 0] read TIS on the run-loop thread inside the hotkey handler (the process has a run loop anyway); never from a bare polling thread. As a cross-check read `com.apple.HIToolbox AppleSelectedInputSources` and skip the moment when it has no `KeyboardLayout ID` entry. InputSourcePro combines `TISCopyCurrentKeyboardInputSource` (`InputSource.swift:92-95`), the `kTISNotifySelectedKeyboardInputSourceChanged` distributed notification (`InputSourceVM.swift:63-72`) and a 1 s heartbeat (`:47-54`); im-select and macism read TIS in a fresh process.

## C. Ready-made tools
- **Input Source Pro** 2.14.0, GPL-3.0, macOS 12+ (`brew install --cask input-source-pro`). Needs Accessibility + Input Monitoring. Maintainer's workaround for this bug: create a switch group (U.S. + RussianWin) and bind ⌘Space to it; its "Shortcut Simulation" fix is labelled "works well before macOS 26, but on macOS 26 it may cause unexpected system behavior" (`Resources/en.lproj/Localizable.strings:59`).
- **Karabiner-Elements**: `to.select_input_source` with `input_source_if` conditions (official docs). Heavy: Accessibility, Input Monitoring and a DriverKit virtual device. Docs warn only about CJKV sources. One report (Tahoe 26.2, #4390, closed as not planned): a Karabiner-emitted F19 did not trigger the input-source shortcut it was bound to, so prefer `select_input_source` over "remap to the system shortcut".
- **BetterTouchTool** (closed source): predefined action "Change Input Source" (`BTTPredefinedActionType` 420, `BTTActionChangeInputSource` = input source ID). Toggling needs BTT's own logic.
- **Hammerspoon**: `hs.hotkey.bind({'cmd'}, 'space', …)` + `hs.keycodes.currentSourceID(next)` is a known snippet (Hammerspoon #1723). Needs Accessibility and hotkey 60 disabled.
- **CLI backends** for a glue script: `im-select <id>`, `macism <id> 0`.
- **Dedicated fix for this exact bug: none found.** Only a reproducer (huiyeruzhou/macos-ctrl-space-inputsource-repro, "<7 ms per key event"), the Apple report FB24297598 (forum 841799), and Apple Community threads: 256147156 (Tahoe, ⌘Space, top reply suggests moving ⌃Space to hotkey 61 — the OP later reports it recurred; 61 does not work on this Mac), 256254361 (⌃Space intermittent, 🌐 key fine), 256144071 (a custom ⌘Space hotkey suspected). Reddit has only "first press ignored / lag" threads.

## Comparison

Cells are [inference] unless a section above marks them otherwise.

| | A: tap delays a key-up | B: own hotkey + TIS | C: Input Source Pro | C: Karabiner |
|---|---|---|---|---|
| Permissions | Accessibility (+ Input Monitoring on 14) | none | Accessibility + Input Monitoring | Accessibility, Input Monitoring, DriverKit |
| In the input path | yes, always-on active tap | no | listen-only tap | HID grab + virtual device |
| Removes the double trigger | probably, unverified | yes (no key-up involved) | yes | yes |
| Keeps system hotkey 60 | yes | no (must be disabled) | no (must be disabled) | not required |
| Caret indicator | kept | unknown, test | its own | unknown |
| CJKV caveat | n/a | no (layouts only) | no | no |
| Worst failure | input hang if permission revoked (FB24619068) | no ⌘Space until relaunch | same class as B | remapping broken |

Recommendation [inference]: build B first (about 100 lines of C or Swift, LaunchAgent with `KeepAlive`); try A only if losing hotkey 60's side effects matters.

## Experiments still needed
1. B prototype + `kbdtest.py` matrix; check visually whether the caret indicator appears; read System Settings → Keyboard → Text Input → Edit for the per-document option.
2. Only if B is rejected: A prototype with an 8 ms hold, then with a bumped timestamp.

## Sources

Apple docs and SDK headers (read locally from `MacOSX27.2.sdk`)
- https://developer.apple.com/documentation/coregraphics/cgevent/tapcreate(tap:place:options:eventsOfInterest:callback:userInfo:)
- https://developer.apple.com/documentation/coregraphics/cgeventtapcallback
- https://developer.apple.com/documentation/coregraphics/cgevent/tappostevent(_:)
- https://developer.apple.com/documentation/coregraphics/cgeventtype/tapdisabledbytimeout
- https://developer.apple.com/documentation/coregraphics/cgeventtype/tapdisabledbyuserinput
- https://developer.apple.com/documentation/coregraphics/cgevent/tapenable(tap:enable:)
- https://developer.apple.com/documentation/coregraphics/cgpreflightlisteneventaccess()/ · https://developer.apple.com/documentation/coregraphics/cgpreflightposteventaccess()
- https://developer.apple.com/documentation/coregraphics/cgeventtimestamp
- https://developer.apple.com/library/archive/releasenotes/Carbon/HIToolbox.html (lists the TIS API; the current DocC page for `TISSelectInputSource` was not found)
- Headers: `CoreGraphics.framework/.../Headers/CGEvent.h`, `CGEventTypes.h`, `CGEventSource.h`; `Carbon.framework/.../HIToolbox.framework/.../Headers/TextInputSources.h`, `CarbonEvents.h`; `IOKit.framework/.../Headers/hidsystem/IOHIDLib.h`
- https://support.apple.com/guide/mac-help/change-input-sources-settings-mchl84525d76/mac · https://support.apple.com/guide/mac-help/control-access-to-input-monitoring-on-mac-mchl4cedafb6/mac

Apple Developer Forums (not directly fetchable)
- https://developer.apple.com/forums/thread/828052 · https://developer.apple.com/forums/thread/744440 · https://developer.apple.com/forums/thread/811443 (permissions)
- https://developer.apple.com/forums/thread/844416 · https://developer.apple.com/forums/thread/735204 (tap + Accessibility revoked)
- https://developer.apple.com/forums/thread/734237 (listen-only tap does not block)
- https://developer.apple.com/forums/thread/841799 (the bug, FB24297598) · https://developer.apple.com/forums/thread/748791 (TISSelectInputSource and input methods)

Source code (line numbers at these commits)
- skhd a7105d5: https://github.com/koekeishiya/skhd/blob/a7105d5b3db6b25d01c0ded79a25536ab284b34f/src/event_tap.c#L11-L16 · `src/skhd.c` L135-190, L356-372 · `src/synthesize.c` L54-62, L80-83
- yabai 477e859: https://github.com/koekeishiya/yabai/blob/477e859d26dd4489095c55057692abfe809e7502/src/mouse_handler.c#L36-L56
- Rectangle adbc8a0: https://github.com/rxhanson/Rectangle/blob/adbc8a0560367a6c0a56b2e8ae6f39a24740ab14/Rectangle/Rectangle/Utilities/EventMonitor.swift#L80-L138
- Hammerspoon f392895: https://github.com/Hammerspoon/hammerspoon/blob/f3928953b584a91ed74d0402ddd6dfb2f9b97629/extensions/eventtap/libeventtap.m#L35-L78 (tap creation L217-222) · `extensions/eventtap/libeventtap_event.m` L688-695 · `extensions/hotkey/libhotkey.m` L258-266, L442-452 · `extensions/keycodes/libkeycodes.m` L343-368
- Karabiner-Elements 9bc311d: https://github.com/pqrs-org/Karabiner-Elements/blob/9bc311d5a4277eed74873ab22038e4340cc0fdb9/src/share/monitor/event_tap_monitor.hpp#L121-L148 (L349-363) · `src/apps/CoreService/include/core_service/agent/permission_checker.hpp` L67-77 · `vendor/vendor/include/pqrs/osx/input_source/input_source.hpp` L63-69 · `DEVELOPMENT.md` L152-180 · `docs/cgeventtap-fallback.md` L13-51
- Input Source Pro b9cd70f: https://github.com/runjuu/InputSourcePro/blob/b9cd70febe07316c21ff64e958ec6fbab9ce5838/Input%20Source%20Pro/Utilities/ShortcutTrigger.swift#L367-L426 · `Utilities/InputSource/InputSourceSwitcher.swift` L510-518, L723-807 · `Utilities/InputSource/InputSource.swift` L92-95 · `Models/InputSourceVM.swift` L47-72 · `Models/IndicatorVM.swift` L443-455 · `Resources/en.lproj/Localizable.strings` L59-62
- KeyboardShortcuts 772133d: https://github.com/sindresorhus/KeyboardShortcuts/blob/772133d9dbe800fdac0473226822994c5c162c58/Sources/KeyboardShortcuts/HotKey.swift#L269-L331
- im-select 9cd5278: https://github.com/daipeihust/im-select/blob/9cd5278b185a9d6daa12ba35471ec2cc1a2e3012/macOS/im-select/im-select/main.m#L12-L34
- macism 37cd457: https://github.com/laishulu/macism/blob/37cd4573ab11d722eeb52d2f018d50375ba21d04/TSMUtils.swift#L4-L15 · `InputSourceManager.swift` L33-56, L77-83

Docs, issues, discussions
- https://karabiner-elements.pqrs.org/docs/json/complex-modifications-manipulator-definition/to/select-input-source/ · https://karabiner-elements.pqrs.org/docs/json/complex-modifications-manipulator-definition/conditions/input-source/ · https://karabiner-elements.pqrs.org/docs/getting-started/installation/
- https://github.com/pqrs-org/Karabiner-Elements/issues/4390 · /issues/1602 · /discussions/4095
- https://github.com/runjuu/InputSourcePro/issues/79 · https://github.com/runjuu/InputSourcePro/issues/68 · https://inputsource.pro/ · https://inputsource.pro/changelog
- https://www.hammerspoon.org/docs/hs.hotkey.html · https://www.hammerspoon.org/docs/hs.keycodes.html · https://github.com/Hammerspoon/hammerspoon/issues/1723 · /issues/3058 · /issues/1429 · /issues/1499
- https://github.com/ghostty-org/ghostty/discussions/14232 · https://github.com/deskflow/deskflow/issues/10296 · https://github.com/altic-dev/FluidVoice/issues/921 · https://github.com/glzr-io/glazewm/issues/1419 · https://github.com/bitgapp/Cursr/issues/129
- https://github.com/ohueter/autokbisw/issues/33 · https://github.com/rime/squirrel/issues/1140 · https://github.com/rime/squirrel/issues/1162 · https://github.com/widgetii/karabiner-cjk-helper
- https://docs.folivora.ai/docs/actions/action-definitions (BetterTouchTool "Change Input Source")
- https://github.com/huiyeruzhou/macos-ctrl-space-inputsource-repro (reproducer only)
- https://discussions.apple.com/thread/256254361 · https://discussions.apple.com/thread/256144071 · https://discussionsjapan.apple.com/thread/256147156 · https://discussions.apple.com/docs/DOC-250007827 (caret indicator switch) · https://apple.stackexchange.com/questions/476038
- https://dev.to/autenai/why-your-computer-use-agent-silently-does-nothing-on-macos-4jee · https://figment.so/blog/mac-agent-tcc-permissions (TCC and bare LaunchAgent binaries)
- https://stackoverflow.com/questions/65514367 (DisabledByUserInput) · https://stackoverflow.com/questions/79518299 (posted events, stale TCC grant after rebuild) · https://github.com/etalli/262_KeyLens/issues/376 · https://crftwr.github.io/keyhac/doc/dev/platform-layer.html (reported tap timeout)

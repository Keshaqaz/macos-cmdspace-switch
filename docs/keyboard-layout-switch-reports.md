# ⌃/⌘Space "Select the previous input source" switch-then-revert bug — web reports (researched 2026-11-25)

Companion notes: `keyboard-layout-switch.md` (facts measured on the user's Mac, macOS 27.2.1) and `keyboard-layout-switch-tools.md` (fix options).

**Legend.** **[read]** = I read the page myself (live page, Wayback snapshot, first-party feed or API). **[snippet]** = only a search-engine snippet was available. **[not fetched]** = page blocked or gone; the claim is unverified. **[inference]** = my conclusion, not a source's.

## Access notes (they bound what follows)

- The built-in `websearch` tool answered "No search results found" to all 10 queries I tried (English and Russian, including trivial ones) — it was unusable. I used Brave Search over HTTP instead (about 8 queries succeeded, then HTTP 429), plus the GitHub API, Stack Exchange API, Hacker News Algolia API, Wayback Machine, Apple's docs JSON and `webfetch`.
- `developer.apple.com/forums/thread/841799` is behind Apple's bot shield: 7 fetch attempts failed. The original post was read in full from Apple's own tag pages and RSS feed. **The single reply could not be read.**
- Apple Support Communities **closed on 2026-11-20** ("The Apple Support Community will permanently close on November 20, 2026… available in a read-only state", banner in the archived pages). `discussions.apple.com/thread/*` now 301-redirects to `support.apple.com`. Threads below were read from Wayback snapshots of 2026-11-16/18.
- reddit.com and forums.macrumors.com return 403 to fetches. One Reddit thread (2023) was read via Wayback.
- Russian-language search: see "Not done" at the end.

## 1. Apple Developer Forums thread 841799

Facts **[read]** from https://developer.apple.com/forums/tags/inputmethodkit, https://developer.apple.com/forums/tags/core-graphics and https://developer.apple.com/forums/tags/rssFeed/inputmethodkit:

- Title: **"Control+Space input source switch reverts when both keys are released simultaneously (FB24297598)"**. Author **hota911**. Created **12 Aug 2026** (RSS `pubDate` Wed, 12 Aug 2026 21:57:08 GMT). Topic UI Frameworks › General, tags InputMethodKit and Core Graphics. Counters: **1 reply**, 3 boosts, 1.1k views, last activity Sep ’26.
- It reports the **default ⌃Space** on **macOS 26.5.2 (25F84)** — not ⌘Space, not 26.1.

Key statements, quoted **[read]**:

- "On macOS 26.5.2 (25F84), the built-in "Select the previous input source" shortcut (Control+Space) switches the input source and then reverts it roughly 100-500 ms later. The net effect is that the shortcut appears to do nothing. The trigger is the timing of the two key-up events, not their order."
- Method: synthetic CGEvents, 15 trials per condition. "Simultaneous" means "the two key-up events are posted back to back with no delay between them, so they land in the same event batch".
- Results (Space held → persisted / reverted / no response): 30 ms → 14/1/0 · 60 ms → 7/8/0 · 100 ms → 3/11/1 · 150 ms → 1/14/0 · 200 ms → 0/15/0 · 250 ms → 0/15/0 · 300 ms → 2/12/1 · 600 ms → 1/13/1. With the key-ups 20 ms apart (Space first or Control first; 60 ms and 300 ms holds): 15/0/0 in all four conditions.
- "A 20 ms gap between the two key-up events makes it completely reliable: 60/60 trials across four conditions. Which key is released first makes no difference."
- "A separate monitor process polling TISCopyCurrentKeyboardInputSource recorded two input source changes per failing trial - the switch, then a revert 104-588 ms later - and exactly one change per trial in the four 20 ms-gap conditions."
- Ruled out by the author: "Not the input source machinery. Selecting the same two sources 25 times via TISSelectInputSource, with no keyboard involved, never reverted (0/25). The fault is in the hotkey path." / "Not key auto-repeat." / "No duplicate binding. AppleSymbolicHotKeys ID 60 is the only enabled system hotkey bound to keycode 49 with Control alone." / "Not specific to one IME. I see it with ABC and a third-party Japanese input method".
- Real-world: "In a week of normal use a background monitor recorded 733 input source changes, of which 6.5-13.4% were a switch immediately followed by a revert … Real-world reversal intervals were 135-433 ms, median 247 ms".
- Author's caveat: the 600 ms condition varies between runs (14/15 persisted in an earlier run, 13/15 reverted in the one shown); "The 150-300 ms band failed in both runs."
- "Filed as FBxxxxxxxxx with a self-contained reproducer (single Swift file, ~170 lines…)". The number is redacted in the body and given in the title: **FB24297598**. Feedback Assistant items are not public, so its status cannot be checked.
- Questions asked: "Is the simultaneous-release behaviour intentional in any way, or is this simply a race in the hotkey handler? Is there a supported way for a user to make Control+Space reliable, short of moving to the Globe/Fn key or a third-party remapper? For anyone hitting this: does the 20 ms release gap also fix it on your machine?"

**Apple engineer reply: unknown.** I could not see who replied or whether it was Apple staff/DTS. Do not assume Apple responded.

**Follow-up threads by hota911: none found.** Checked: InputMethodKit tag (listing + RSS), Core Graphics tag listing, RSS of the "UI Frameworks › General" topic and of the `macos` tag (20 newest threads each, covering Jun – 24 Nov 2026), and hota911's public GitHub (10 repos, one 2019 gist; the reproducer is not published).

**Against the local repro** (`keyboard-layout-switch.md`) **[inference]**: same trigger (both key-ups in one batch) and same conclusion (order irrelevant, a small gap fixes it). The numbers differ: hota911 tested a 20 ms gap and saw the revert 104–588 ms later; locally a ≥3 ms gap was enough and the revert came ~10–21 ms later. Both fit "the hotkey path triggers twice per chord". The threshold may be machine- or keyboard-dependent, which is exactly what hota911 asked others to test.

## 2. Acknowledged or fixed by Apple?

**No public acknowledgment found.**

- **Developer release notes [read]** (full text from Apple's docs JSON, fetched 2026-11-25): macOS Tahoe 26, 26.1, 26.2, 26.3, 26.4, 26.5, 26.6; macOS 27 Golden Gate, 27.2; 27.3 Beta 2. None mentions input sources, keyboard-layout switching, symbolic hotkeys or TIS. Caveat: the point-release notes are short and API-focused (26.1 ≈1.7k characters, 26.2 ≈1.5k, 26.3 ≈0.6k, 26.6 ≈1.1k, 27.2 ≈1.7k), so silence is weak evidence.
- There are no developer release notes for 26.7.x, 27.0.1 and 27.2.1; on Apple's security-releases page they are security-only entries.
- **Release dates [read]** (https://support.apple.com/en-us/100100): 26.0 15 Sep 2025 · 26.0.1 29 Sep 2025 · 26.1 3 Nov 2025 · 26.2 12 Dec 2025 · 26.3 11 Feb 2026 · 26.3.1 4 Mar 2026 · 26.4 24 Mar 2026 · 26.4.1 9 Apr 2026 · 26.5 11 May 2026 · 26.5.1 1 Jun 2026 · 26.5.2 29 Jun 2026 · 26.6 27 Jul 2026 · 26.6.1 6 Aug 2026 · 26.6.2 17 Aug 2026 · 26.7 14 Sep 2026 · 26.7.1 28 Sep 2026 · 26.7.2 26 Oct 2026 · 26.7.3 9 Nov 2026 · 27 14 Sep 2026 · 27.0.1 28 Sep 2026 · 27.2 26 Oct 2026 · 27.2.1 9 Nov 2026 ("The latest version of macOS is 27.2.1"). 27.3 is in beta.
- **Reports exist across the whole 26.x line**, so no 26.x update visibly fixed it: 26.0/26.0.1 (Sep–Oct 2025: Apple Community 256144071, CommandShift #25, keyameleon #195) → 26.1 (Nov 2025) → 26.2 (Dec 2025) → "macOS 26", 1 Mar 2026, when 26.3 was current (Apple Community 256254361) → 26.4 (13 May 2026, InputSourcePro #102) → 26.5.2 (12 Aug 2026, thread 841799) → 26.7.1 (19 Oct 2026, risu729/dotfiles #4585). Caveat: 256144071 and #102 mix two mechanisms (revert on focus change and revert right after the shortcut), so not every post there is the key-up race.
- **macOS 27.x: no report found that tests this ⌃/⌘Space race on 27.0.x, 27.2, 27.2.1 or the 27.3 beta.** The only 27.x data points are adjacent: the Caps Lock variant inside the Claude Desktop app ("Still happening on macOS 27", 27.0.1, 23 Oct 2026, claude-code #81377) and an unrelated CJKV bug fix tested on 27.2.1 (InputSourcePro #85). The user's own repro on 27.2.1 is the only 27.x evidence I know of.

## 3. Workarounds and whether they were confirmed

| # | Workaround | Source | Confirmed by others? |
|---|---|---|---|
| 1 | Put a gap between the two key-up events (either order). hota999 tested 20 ms. For a hand: release one key clearly before the other. | hota911, thread 841799 [read] | **No independent confirmation found** (the OP asks for it). Agrees with the local repro (≥3 ms). |
| 2 | Kanata: keep Control held for as long as Caps Lock is held, tap Space after a short delay | risu729/dotfiles #4585, macOS 26.7.1, open since 19 Oct 2026 [read] | No: 0 comments, still open |
| 3 | Switch with the 🌐/Fn (Globe) key instead of the hotkey | ohnooooooo, Apple Community 256254361: "Switching the language to "fn" works ok." [read]; repeated by hota911 | Not independently confirmed. Counter-report: InputSourcePro #102 (macOS 26.4): after switching with Fn, the first character comes out in the new layout and the rest in the old one; one user confirmed ("我也遇到相同问题") [read] |
| 4 | Switch to another window/app once; the shortcut then works on the first press | ohnooooooo, 256254361 [read] | No. Tom Gewecke could not reproduce the bug at all ("I cannot duplicate the issue on my machine in any Apple apps"). |
| 5 | Wait about 1 s after switching before typing | Fanoid, InputSourcePro #102 [read] | Reporter only |
| 6 | Move the shortcut to a single key: `hidutil` remaps Caps Lock to F18, which is bound to "Select the previous input source" | ha-kim, claude-code #81377, 18 Nov 2026 [read] | Reporter only: "0 failures in 32 presses". Context is the Caps Lock bug inside Claude Desktop, not the ⌃/⌘Space chord. |
| 7 | Use hotkey 61 "Select next source in Input menu" (⌃;) instead of 60 | creasty/dotfiles #171, merged 4 Oct 2026 [read]; Ask Different 461574 answer, Ventura [read]; kraykov, 26.0 [read] | **Untested for this bug.** creasty: "Whether ID 61 on Ctrl-; does the same is untested." ⚠️ Local note: 61 alone on ⌘Space does not work on 27.2.1. |
| 8 | Reset a custom ⌘Space binding of "Select the previous input source" to the default ⌃Space, or disable it | cheverda, 256144071, 9 Jan 2026 [read] | No. kostiantyn-s has no custom hotkey and still sees it. Context is the focus-change variant on 26.0–26.2. |
| 9 | Toggle "Automatically switch to a document's input source" on → Done → off | VasylBaran, CommandShift #25, 11 Oct 2025 [read]; keyameleon #195 [read] | **Mixed**: worked for VasylBaran; failed for dvavatsioulas, alex-berezin and sfareg. Targets the focus-change variant, not the key-up race. |
| 10 | CommandShift v1.05 | https://github.com/VasylBaran/CommandShift/releases/tag/1.05 (release page not fetched; discussion in #25 [read]) | Yes, 4 users in #25 (maks1mov, alex-berezin, sfareg, dvavatsioulas) — for the focus-change variant |
| 11 | Karabiner-Elements for switching instead of Fn/Caps Lock | zhanghm1995, InputSourcePro #102 ("暂时没有出现问题") [read]; dvavatsioulas, #25 (community rule "Cmd + Shift Switch Languages" by s4wet) [read] | zhanghm1995 only; dvavatsioulas reports no result |
| 12 | Turn "Select the previous input source" off | VasylBaran, #25: "It seems to have helped me on Tahoe 26.0.1" [read] | sfareg: then cannot switch at all. ⚠️ Never disable 60 locally (see `keyboard-layout-switch.md`). |
| 13 | Old trick (2021, different bug — the Globe key stops working): enable "Select the previous input source" (⌃Space), press it once, disable it again | soraweep, Apple Community 252165525 [read] | Yes: Angelov89, "control+space helped me as well as a workaround" |
| 14 | Restart | TheDCT, 252266965 (2021) [read]; msilbermann, 252165525 (2021) [read] | Old and for different bugs |
| 15 | InputSourcePro | runjuu/InputSourcePro #53, #68, #85, #102, #113 [read] | Not a clean workaround: it has many macOS 26 switching complaints of its own |
| 16 | Hammerspoon | creasty/dotfiles #171 [read]: he removed his Hammerspoon switcher in favour of the system hotkey | n/a |
| 17 | BetterTouchTool | — | **No report found** |

## 4. Root-cause evidence

- **Key-up timing / double trigger — confirmed** by hota911 [read] (§1). Also ha-kim, for the Claude Desktop Caps Lock variant [read]: "Each press is exactly one key-down and one key-up at the HID level, so this is not key chatter. In all 99 reverted presses the first switch came after the key-up, and the second followed typically 20–30 ms later when starting from ABC, 50–210 ms later when starting from Korean." Routed through the hotkey path (F18) the same key worked 32/32.
- **Keyboards — mixed.** kraykov, Tahoe 26.0 [read]: "this only happens with external keyboard when using the "Select previous input source" shortcut. Does not happen when i use the "Select next input source" shortcut, when i use the built-in keyboard, or the mouse"; the keyboard runs ZMK firmware, and "I pulled my old Logitech and it seems to be working as expected". mcsymiv [read]: with and without an external keyboard (ZSA Voyager). ohnooooooo [read]: built-in and Bluetooth keyboards both. livingssal, Caps Lock variant [read]: Logitech MX Mechanical fails, Apple Magic Keyboard and the built-in keyboard do not. ha-kim [read]: the same keyboard on a second Mac gave 0/91, "So the keyboard alone does not decide it."
- **Per-document input source**: explains the *focus-change* variant for some users. cheverda [read]: setting is FALSE and it still reverts. mcsymiv [read]: disabled and still reverts. keyameleon #195 [read, the issue author's claim]: macOS 26.0/26.0.1 keeps the option active while Settings shows it off. It is not the cause of the key-up race, which happens with no focus change.
- **Ruled out** by hota911: the TIS API (0/25 reverts), key auto-repeat, duplicate bindings, IME-specificity. Ruled out by ESAD416 for the Caps Lock variant on 27.0.1 [read]: Karabiner/`hidutil` remapping, input-source utilities, the per-document setting, Secure Event Input, default shortcuts.
- **A different macOS bug that looks similar**: `TISSelectInputSource` to CJKV input sources may not take effect until the focus changes. Karabiner docs [read]: "Switching to input sources which have input_mode_id (Chinese, Japanese, Korean, Vietnamese) may be failed due to an macOS issue. For CJKV input sources, sending the input source switch shortcut (e.g., control-space) is better than using select_input_source." InputSourcePro #85 [read]: zcbenz wrote on 31 Mar 2026 that he had found someone inside Apple willing to fix it ("我打入了Apple内部找到了有人愿意修这个bug但是需要一个minimal repro"); runjuu published https://github.com/runjuu/TISSelectInputSourceDemo; harukawu/MacIMEKit is a private-API fix, integrated in InputSourcePro Beta 1218 and tested on macOS 27.2.1 (jing2si, 25 Nov 2026). It does not apply to two plain keyboard layouts switched by the hotkey.
- **"Use the F1 and F2 keys" setting: no report found** that links it to this bug.
- **Older history [read]**: Ask Different 461574 (Ventura 13.4.1, Jun 2023): the first press after a long time in one layout is sometimes ignored, with default ⌃Space and custom ⌘Space; the answer recommends "Select next source". Reddit r/MacOS 1618m7v (2023): "my first 2-3 attempt do not work". Ask Different 263882 (Sierra, 2016): switching stops entirely; fixes were disabling Siri, `sudo killall -9 PAH_Extension TextInputMenuAgent TextInputSwitcher`, or killing `localAuthenticationRemoteService`. So a flaky "previous input source" predates Tahoe; the switch-then-revert timing characterization dates from 2026.

## 5. Open-source projects or scripts published as a fix

- **physwkim/ctrlspace-switch** — **[snippet]** Brave's description: "Instant Korean/English input source toggle on Ctrl+Space for macOS, bypassing the system hotkey race"; "releasing both keys together makes it revert the switch 100–500 ms later (FB24297598)". **[not fetched]** GitHub returns 404 for the repository (web and API) and it is not among the owner's 71 public repos, so it was removed or made private. Content unverified.
- **risu729/dotfiles #4585 [read]** — Kanata `deflayer` change that keeps Control held; an open issue with a proposed config, not merged.
- **creasty/dotfiles #171 [read]** — merged 2026-10-04: system hotkey 61 on ⌃; replaces a Hammerspoon switcher; applies shortcuts without re-login by running `activateSettings -u` after writing them; a test reads live values through the private `CGSGetSymbolicHotKeyValue`.
- **VasylBaran/CommandShift v1.05 [read via #25]** — workaround for the focus-change variant.
- **harukawu/MacIMEKit and runjuu/TISSelectInputSourceDemo [read via InputSourcePro #85]** — for the CJKV TIS bug, not this one.
- **ha-kim's `hidutil` one-liner** (Caps Lock → F18) [read] — in the comment of 18 Nov 2026 on claude-code #81377.
- **keyameleon #195 [read]** — open feature request to detect the per-document conflict and guide the user.
- **No public standalone reproducer or fix for the key-up race was found.** hota911's ~170-line Swift reproducer went to Feedback only.

## Discrepancies with the task brief

1. Thread 841799 is about **⌃Space on macOS 26.5.2**. ⌘Space on hotkey 60 appears only in passing: creasty's PR text ("Ctrl-Space, Cmd-Space") and cheverda's custom binding in 256144071.
2. "First reported around 26.1": the earliest related public report I found is from 22–23 Sep 2025 on 26.0 (Apple Community 256144071), but that thread mixes several symptoms. The timing analysis is from Aug 2026.
3. Revert delay: 104–588 ms in hota911's measurements, ~10–21 ms locally.
4. Apple Support Communities is closed, so no new user reports will appear there.

## Not done

- The reply in thread 841799 and any Apple response to it.
- Reddit, MacRumors forums and Russian-language sites ( Habr search is client-side; other engines blocked or rate-limited). No Russian-language report was found, and none can be ruled out.

## Sources

Apple
- https://developer.apple.com/forums/thread/841799 (not fetched; OP read via the three URLs below)
- https://developer.apple.com/forums/tags/inputmethodkit
- https://developer.apple.com/forums/tags/core-graphics
- https://developer.apple.com/forums/tags/rssFeed/inputmethodkit
- https://developer.apple.com/forums/topics/rssFeed/ui-frameworks-topic/ui-frameworks-topic-general
- https://developer.apple.com/forums/tags/rssFeed/macos
- https://developer.apple.com/documentation/macos-release-notes (and `macos-26_1-release-notes` … `macos-27_3-release-notes` under the same path)
- https://support.apple.com/en-us/100100 (security releases and dates)
- https://discussions.apple.com/thread/256254361 — "macOS 26: Control + Space input switch fails intermittently" (ohnooooooo, 1 Mar 2026; 25 Me too; read via Wayback 20261118025844)
- https://discussions.apple.com/thread/256144071 — "Keyboard keeps changing language on input" (mcsymiv, 22 Sep 2025; 72–73 Me too; read via Wayback 20261116204208, pages 1–2)
- https://discussions.apple.com/thread/252165525 — Globe button stops switching (Dec 2020; read via Wayback 20261116200523)
- https://discussions.apple.com/thread/252266965 — switches back to default layout (Jan 2021; read via Wayback 20261118082931id_)
- https://discussions.apple.com/thread/256138313 — cited by keyameleon #195; no Wayback snapshot, not read

GitHub
- https://github.com/risu729/dotfiles/issues/4585
- https://github.com/creasty/dotfiles/pull/171
- https://github.com/VasylBaran/CommandShift/issues/25 and https://github.com/VasylBaran/CommandShift/releases/tag/1.05
- https://github.com/mastro993/keyameleon/issues/195
- https://github.com/anthropics/claude-code/issues/81377
- https://github.com/runjuu/InputSourcePro/issues/68, /85, /102, /113 (also seen in listings: #53, #79, #112)
- https://github.com/harukawu/MacIMEKit and https://github.com/runjuu/TISSelectInputSourceDemo (links taken from #85; repos not opened)
- https://github.com/physwkim/ctrlspace-switch (404 at check time)
- https://github.com/hota911 (reporter's profile)

Other
- https://karabiner-elements.pqrs.org/docs/json/complex-modifications-manipulator-definition/to/select-input-source/
- https://apple.stackexchange.com/questions/461574/lag-where-macos-sometimes-skips-the-first-key-down-event-when-changing-keyboard
- https://apple.stackexchange.com/questions/263882/how-to-fix-macos-sierra-language-switching-bug
- https://www.reddit.com/r/MacOS/comments/1618m7v/changing_language_not_working_the_first_13_times/ (2023; read via Wayback 20230912223649)
- https://typetab.app/blog/change-keyboard-language-mac (generic guide, checked on macOS Tahoe 26.7; nothing specific to this bug)

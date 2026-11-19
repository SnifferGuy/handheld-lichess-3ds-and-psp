# Lichess Handheld test report

## Build and desktop checks

- Nintendo 3DS package built with devkitARM r68 / GCC 16.1.0, libctru 2.7.1, Citro2D 1.7.0, and Citro3D 1.7.1.
- PSP package built for base-model memory with portable PSPDEV GCC 15.2.0 and PSPSDK commit `9de3ef274702e2f4975bb68569fb3ec4e4a8efe3`. WSL was unavailable in this Windows session.
- Desktop suites passed with Zig 0.15.2. They cover reference move counts, castling, pinned en passant, all promotions, repetition and move-count draws, AI budgets and cancellation, match framing, and both interface input paths.
- Windows TCP loopback passed with the host as White and Black. Both peers reconnected before the first move. The guest also disconnected after the host accepted its move but before receiving the reply, then resumed with the move applied exactly once. After resignation and mutual rematch, both peers played a full 15+10 game to checkmate and agreed on the move history, final position, clocks, and result.
- The desktop renderer exercises both appearances at 480×272 (PSP), 400×240 (3DS top) and 320×240 (3DS bottom). Run `test.ps1 -Capture` to save the frames under `build/desktop/screens`. Tests cover settings round-trip and fallback, render bounds, and moving through the game, results, and analysis in both appearances.
- Live-clock desktop fixtures cover fragmented game events, clock snapshots/increments, counting down only the active player, zero saturation without local adjudication, stopped first-move clocks, takeback restoration, fresh reconnect times, frozen server results, and rejection of invalid times/statuses/winners. Clock-panel captures exercise both appearances and player colors at native resolutions, including long names and low-time warnings. These are component captures using fixtures, not authenticated Lichess sessions.
- Online move fixtures pass for White and Black, all four promotions, accepted/rejected moves, uncertain replies with and without server acceptance, input blocking until a fresh snapshot, no automatic resubmission, and rate-limit backoff. Tests use the production interface and worker with an injected simulated service; no real account or match is contacted. Native captures include the actual online board, player panels, promotion choices, sending/synchronized states, and menus in both appearances.
- Online action fixtures pass for both player colors: offer/accept/decline draws and takebacks, restoring the server position and clock values, resignation with Cancel initially focused, touchscreen confirmation, and a lost resignation acknowledgment without resubmission. A draw offer arriving while Resign is focused preserves that action and defers a press on the update frame. Offer decoding rejects invalid Boolean fields and clears omitted flags. Moves and match actions share a bounded single-request slot and require a fresh server snapshot.
- Both PSP and 3DS packages now build with HTTPS enabled. PSP dependencies are installed from SHA-256-pinned official PSPDEV packages; `DEPENDENCIES.md` records versions and the CA bundle hash.
- Expanded release fixtures pass: player-only chat, repeated identical messages without collapsing them, 100-message/140-character limits, UTF-8 fallback, QWERTY navigation, phrases requiring explicit Send, draft preservation on uncertain delivery and game completion, incoming moves while typing, postgame refresh, casual search, username challenges, incoming accept/decline, rematch challenges with swapped colors, and isolated postgame analysis with engine cancellation and repetition history. Requests use injected simulated services and never contact a real account.
- Native-resolution captures include chat/keyboard/phrases, lobby/challenges/search, results and online analysis in both appearances. Desktop rendering checks cover both screen sizes and board orientations. Visual review corrected bottom-edge text clipping and verified the common three-dot chat bubble.
- PKCE/callback/cancellation helper self-tests pass. Real browser OAuth, authenticated Lichess games/chat, sustained console HTTPS, latency behavior, sleep/resume and physical clock/result/memory checks remain pending.
- Emulator smoke tests booted the release binaries in PPSSPP 1.20.4 and Azahar 2126.2. The PSP build displayed the home menu. The 3DS build accepted touch input and opened the online lobby. These checks used no Lichess sign-in.
- The Friends update passed all 16 desktop suites, including fragmented follows and the 1,000-account cap, presence decoding, controller and touch navigation, selection through refresh, follow reconciliation after a lost reply, unavailable usernames, unfollow confirmation, permission and rate-limit handling, and challenges in either direction. Friends tests use an injected service. Both HTTPS-enabled console packages were rebuilt.
- Public matchmaking fixtures passed in both handheld layouts. A 15+10 seek canceled before pairing, a seek closed without a game, and a later seek opened a game through a simulated game-start event. **Find opponent** retained focus for retry, and a rate-limited seek blocked immediate resubmission. Separately, a read-only HTTPS request from Windows to Lichess's public player-status endpoint returned HTTP 200 without an account.

## Physical consoles

Physical PSP-1000 and Old 3DS playtesting is pending. Before the first match, record the PSP firmware version, Old 3DS firmware version, access point band and security mode, and whether both consoles obtain IPv4 addresses. Confirm both builds launch from their normal homebrew menus.

| Check | Expected result |
| --- | --- |
| Play an offline game on each console | The board is readable, each promotion option is selectable, and controls stay responsive during Hard search. |
| Switch appearance in a match and restart | Select switches immediately, preserves the position, and restores the saved appearance on restart. Check font and piece textures on the actual 3DS GPU. |
| PSP hosts, then 3DS hosts | Each device can join by IPv4 address, submit moves, and see the same position and result through checkmate or resignation. |
| Network drops before sending a move | After reconnect, the host's previous position is restored and the player can choose the move again. |
| Network drops just after a move is accepted | After reconnect, the move appears once, with the correct turn. |
| Network drops while the guest chooses a promotion | Reconnect restores the last accepted position. Complete promotion and confirm both boards show the chosen piece. |
| Sleep and wake either console | The game resumes if the original host stays open and Wi-Fi returns at the same address. |
| Play an existing Lichess game in an HTTPS-enabled build | Both colors and every promotion work. Confirm the board changes only after the server update, clocks continue during requests, and interrupted submissions synchronize without an automatic resend. |
| Find a public opponent from each handheld | Open **Online lobby → Find opponent** at 15+10, cancel once, then search again and complete a casual game. Confirm the game appears in the player's Lichess history. |
| Exercise online match actions | Cancel and confirm resignation; offer, accept and decline draws/takebacks. Check the server's position, history and clocks after acceptance. Deliver offers while navigating menus and interrupt requests around acceptance. |
| Play a PC browser opponent on each handheld | Use two Lichess accounts. Send and accept a timed casual standard challenge in both directions. Complete a game with each handheld and compare the move history and result with the browser. |
| Sync Lichess follows between PC and both handhelds | Reauthorize existing card credentials with the current helper. Follow and unfollow from each handheld, check the website, then change a follow on the website and refresh each console's list. |
| Claim a draw after reconnecting and request rematch | Draw eligibility survives reconnect. Both players must accept a rematch and then play opposite colors. |

For a short checkmate test, use 1. f3 e5 2. g4 Qh4#. For repetition, play 1. Nf3 Nf6 2. Ng1 Ng8 twice and test **Claim draw**.

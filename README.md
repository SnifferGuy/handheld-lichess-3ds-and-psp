# Lichess Handheld

Lichess Handheld is a homebrew chess game. Play Lichess online, the computer offline at three strengths, or host and join a two-player match on the same Wi-Fi network. See [RELEASE.md](RELEASE.md) for packaged installation and [PROGRESS.md](PROGRESS.md) for acceptance status.

The original project code is licensed under GPL-2.0-or-later. See [license notices](LICENSE-NOTICE.md) for the code, bundled artwork, and libraries.

## Install and play

- **Nintendo 3DS** — Copy `dist/HandheldChess.3dsx` to `/3ds/HandheldChess/HandheldChess.3dsx` on the SD card. Launch it from the Homebrew Launcher.
- **PSP** — Copy `dist/EBOOT.PBP` to `/PSP/GAME/HandheldChess/EBOOT.PBP` on the Memory Stick. Launch it from Game → Memory Stick on a homebrew-ready PSP.

Pick **Play computer**, choose a difficulty and color, then select **Start game**. On PSP, use the D-pad to move the cursor, Cross to select, Square to cancel, and Triangle or Start to open match actions. On 3DS, the board is on the top screen; use the D-pad and A/B. Tap **Actions** or press Start to open the match menu. Promotion presents four piece choices. Rematches switch colors.

Choose **Appearance** on the home screen for Dark or Light. Press Select on either console to switch instantly during play or analysis. The choice is saved in `appearance.txt` beside the game. Use L/R during a match to scroll recent moves. Both appearances use the same green-and-cream board and distinguish the running and waiting clocks with green and red text. See [artwork sources and licenses](assets/README.md) for how to rebuild the font and piece atlas.

## Play across the two consoles

1. Configure a compatible shared Wi-Fi network in the consoles' system settings. Enable the PSP's WLAN switch. The game uses the PSP's first saved infrastructure profile. The access point needs to support the PSP-1000's 2.4 GHz 802.11b mode and the PSP firmware's supported security. Both devices must be able to reach each other, so use a network without client isolation.
2. On one console, pick **Host LAN match**, choose a color, and wait for its IPv4 address to appear.
3. On the other console, pick **Join LAN match**. Highlight the address, press A or Cross to cycle through the four numbers, and use Left/Right to edit the highlighted number. Hold Start for steps of ten. Pick **Connect**. The TCP port is fixed at 49373.

On a dropped connection, keep the host game open at the same address. The guest retries for two minutes and then offers **Retry** and **Leave**. The host restores the last accepted position and move history. A host exit ends the session. A Wi-Fi outage itself does not award a result.

## Rules

Computer and LAN games default to 15 minutes plus 10 seconds per move. Select Clock during computer or host setup to choose 10+0, 30+0, or Untimed. LAN guests use the host clock settings and need the same game version. Offline match actions pause the clock. Take back restores the position and clocks from before your previous move. In LAN games, either player can request undoing the latest half-move. The other player must accept, and a new move clears the request. After a game, choose Rematch, New opponent, or Analysis board. In the analysis board, use Left/Right to replay the recorded moves and Back to return to the results. Press A on 3DS or Cross on PSP to try a variation from the displayed position. Use the normal board controls to play either side. B or Square returns to the recorded game line. On 3DS you can also use the touch buttons. Press Start or tap Analyze position to get an engine estimate and a suggested line of up to three half-moves. Analysis includes the recorded positions leading up to the review point and any moves made in the current variation. Existing Lichess games can be played in HTTPS-enabled builds; online setup, chat and postgame analysis are included, with live account verification still pending. Match actions include resignation and draw claims. In LAN matches, players may also offer or accept a draw. **Claim with move** lets the player select an intended legal move to claim threefold repetition or the 50-move rule. Fivefold repetition and the 75-move rule end the game automatically. Checkmate takes precedence.

Automatic dead-position detection covers bare kings, a single bishop or knight against a bare king, and bishop-only positions with all bishops on the same color. Other unusual dead positions can be drawn by agreement in LAN matches.

## Lichess sign-in helper

The Windows helper prepares a credential on the selected card. Copy dist/cacert.pem next to the handheld executable, then open Lichess setup and choose Verify account in an HTTPS-enabled build. The game displays the username returned by Lichess after successful verification. **Active games** lists up to five ongoing games and shows whose turn it is. Select a standard game to open its board and play through the Board API. Use the normal D-pad and A/Cross controls; all four promotion choices are available. Start opens the scrolling match menu with available draw/takeback actions, Resign, Reconnect, and Back to games. Use Reconnect if the game disconnects or Refresh to update the game list. Local sign out removes the saved file after confirmation. Online lobby includes casual opponent search, username and incoming challenges, and resuming games. Chat and online results/rematch/analysis are included. These paths pass simulated tests; real account and hardware checks remain pending.

Online moves are submitted once over HTTPS using the [Board move endpoint](https://raw.githubusercontent.com/lichess-org/api/master/doc/specs/tags/board/api-board-game-gameId-move-move.yaml). The board changes only when Lichess sends its accepted position. A cancellable worker temporarily closes the stream, sends the queued move, and reopens the stream for a complete state snapshot. Ordinary requests are serialized; clocks continue counting down and controls remain responsive. Every submission, including an uncertain or rejected one, blocks further moves until synchronization. Uncertain submissions are never automatically resent. If synchronization fails, use Reconnect before attempting another move. Rate limiting pauses requests for one minute. Engine assistance is disabled in this view. Desktop simulated-server tests cover this path; real authenticated and hardware games are not yet verified.

Match actions use the same request/synchronization path. [Draw offers](https://raw.githubusercontent.com/lichess-org/api/master/doc/specs/tags/board/api-board-game-gameId-draw-accept.yaml) and [takebacks](https://raw.githubusercontent.com/lichess-org/api/master/doc/specs/tags/board/api-board-game-gameId-takeback-accept.yaml) provide Offer/Request or Accept/Decline choices according to the latest server flags. A new offer preserves menu focus and is drawn before a pending press is interpreted. Resign opens a confirmation with Cancel selected first. Draws, resignations, accepted takebacks, and restored clocks appear only after a server update. Pending actions share the single request slot with moves; uncertain actions are never automatically resent.

The live preview displays both players' server clocks with green/red turn labels and a low-time warning. Between updates, the active real-time clock counts down from the last received value, including during menus and disconnection. These are display estimates: network delay is not compensated, and only Lichess declares the result. Every game update replaces both times, including after takebacks and reconnects. Casual real-time countdown starts after both players' first moves; the API does not supply a clock-running flag. Other clock types show `--:--`. Ended games display the server's winner/result and stop counting down. Live authenticated and hardware verification remain pending.

For 3DS, install 3ds-curl, 3ds-mbedtls, and 3ds-zlib with devkitPro pacman, then run .\build-3ds.ps1 -LichessHttps. The tested build used curl 8.4.0, mbedTLS 2.28.8, and zlib 1.3.1. For PSP, install the PSPDEV curl, mbedtls, and zlib ports and set HANDHELD_CURL=1 when running the build. Run `install-psp-https.ps1` for pinned official PSPDEV libraries, then `build-psp-windows.ps1 -LichessHttps`. Both supplied packages now compile with HTTPS enabled. In builds without HTTPS, Lichess setup displays Check card instead.

With PowerShell 7.2 or later and the card mounted, run one of these commands from this directory. Replace E:\ with your card drive.

```powershell
pwsh -File .\lichess-sign-in.ps1 -Console 3DS -CardRoot E:\
pwsh -File .\lichess-sign-in.ps1 -Console PSP -CardRoot E:\
```

Complete authorization in your browser. The helper shows the linked username and saves the token next to the game on the card. Run it separately for each console. Keep the token private. Use -Replace only when you want to overwrite the local credential. To revoke a token on Lichess, remove it from your account's API access tokens page.

Run `pwsh -File .\lichess-sign-in.ps1 -SelfTest` to check PKCE and callback validation without contacting Lichess.

Account profile responses are decoded using [PicoJSON 1.3.0](https://github.com/kazuho/picojson/tree/v1.3.0). Its license is included in `picojson.hpp`, which has a small patch to reject overflowing numbers in handheld builds with exceptions disabled.

## Build

From this directory on Windows, build the 3DS package with PowerShell 7 and the devkitPro `3ds-dev` tools:

```powershell
.\build-3ds.ps1 -DevkitProRoot C:\devkitPro
.\test.ps1
```

The desktop test script accepts `-Zig` if Zig 0.15.2 is installed outside the parent workspace. It uses Windows TCP loopback for the multiplayer checks.

For a reproducible PSP build, install Ubuntu under WSL and PSPDEV using the [official Windows guide](https://pspdev.github.io/installation/windows.html). With `PSPDEV` and its `bin` directory set as described in the guide, enter this directory from Ubuntu and run:

```sh
bash build-psp.sh
```

The script builds in a temporary directory and copies the package and compiler versions to `dist/`. See [TEST-REPORT.md](TEST-REPORT.md) for verification and the physical playtest checklist.

If WSL is unavailable, `build-psp-windows.ps1 -Sdk <extracted-toolchain-directory>` uses a portable PSPDEV Windows toolchain with an existing MSYS2 shell. The supplied package was built with [pspdev-win 15.2.0 v2](https://github.com/dmang-dev/pspdev-win/releases/tag/v2). The toolchain archive SHA-256 was `037f902acd90cfcf4711aeee6462479a6d0eabee6faf93ff44fe1081e653d512`.

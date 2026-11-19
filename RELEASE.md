# Lichess Handheld — release candidate

This package contains builds with direct HTTPS enabled for both devices. Desktop simulated-server tests pass; real account authorization, sustained HTTPS on the consoles, cross-console LAN, sleep/resume and PSP-1000 memory use still need physical verification. See TEST-REPORT.md.

## Install

1. Copy the contents of `3DS/` to the 3DS SD card root, or the contents of `PSP/` to the PSP Memory Stick root. The resulting game folders are `/3ds/HandheldChess/` and `/PSP/GAME/HandheldChess/`.
2. Keep `cacert.pem` beside the executable. Launch from the normal homebrew menu. Existing appearance and sign-in files are not included or replaced by this package.
3. Offline and LAN play require no account. For online play, connect a console's card to Windows and run `pwsh -File lichess-sign-in.ps1 -Console 3DS -CardRoot E:\` (or `-Console PSP`), replacing `E:\` with the selected card. Complete the browser authorization. Repeat separately for the other console. The helper verifies the account before saving its token.
4. Open **Lichess setup → Verify account**, then **Online lobby**. Choose 15+10, 10+0 or 30+0, find an opponent, type a username challenge, accept/decline incoming challenges, or resume an active game.
5. To play a public opponent, choose **Find opponent**. You can select **Cancel search** while waiting. If the search ends before a game opens, **Find opponent** is ready to select again.

## Play a friend on PC

Use two Lichess accounts. The PC player signs in at lichess.org in their browser. On the handheld, open **Online lobby → Friends → Add by username** to follow their Lichess account. Select their name and choose **Challenge**, then have them accept on the website. If they send the challenge from the website, open **Incoming** on the handheld to accept it. **Challenge player** also works when you already know the username.

The Friends list uses your Lichess follows. Open it again, select **Refresh**, or press Start to update the list and presence. On 3DS, use **Page up** and **Page down** to scroll by touch when those buttons appear. Select a friend and choose **Unfollow** to remove them after confirmation. The handheld displays up to 1,000 followed accounts.

If you signed in with an older helper, rerun the bundled helper with the same card arguments plus `-Replace` to grant Friends permissions. Ordinary username challenges continue to work with the earlier board permission.

During play, use D-pad and A/B on 3DS; D-pad, Cross and Square on PSP. Start opens match actions; PSP Triangle also opens them. Resignation requires confirmation. Draws and takebacks require the server's acceptance. Select changes Dark/Light immediately.

Use X on 3DS or Circle on PSP to open chat. The same three-dot speech bubble is available by touch on 3DS. Chat has a controller/touch QWERTY keyboard and five quick phrases. Choosing a phrase only fills the draft: press **Send** to submit. Back closes the composer before chat; X/Circle returns directly to the game. Drafts survive closing chat, connection loss and game completion, for the current session. Start in the transcript toggles notifications. L/R scrolls chat.

After a game, choose Rematch, New opponent or Analysis board. Online Rematch creates a casual challenge to the previous opponent with swapped colors. Analysis preserves the finished game while allowing replay, variations and a local engine continuation. Local engine assistance is unavailable during live online games.

Both devices communicate directly with Lichess or with each other on LAN. Use the Windows helper for sign-in or to renew permissions. Online clocks continue during menus and connection loss. Uncertain moves/actions/messages are never automatically resent. The game reconnects to synchronize server state, and failed chat drafts remain available.

Before physical networking tests, record console firmware, Wi-Fi band/security and AP client-isolation settings. No firmware or access-point configuration has been changed by these builds.

The source, asset sources/licenses, build scripts, dependency versions and pinned PSP HTTPS installer are included in `source/`. `SHA256SUMS.txt` identifies packaged files. Do not share the card's `lichess-token.txt`.

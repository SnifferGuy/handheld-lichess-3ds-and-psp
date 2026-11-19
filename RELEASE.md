# Lichess Handheld — Version 1.0 ;3

This package contains HTTPS-enabled builds for Nintendo 3DS and PSP. Azahar and PPSSPP are the release test targets. See [TEST-REPORT.md](TEST-REPORT.md) for current results.

## Install in an emulator

1. For Azahar, copy the contents of `3DS/` into its virtual SD root, usually `sdmc/`. Open `3ds/HandheldChess/HandheldChess.3dsx` in Azahar.
2. For PPSSPP, copy the contents of `PSP/` into its virtual Memory Stick root, usually `memstick/`. Open `PSP/GAME/HandheldChess/EBOOT.PBP` in PPSSPP. Enable **Networking/WLAN** in PPSSPP settings before checking the Lichess connection. Keep `cacert.pem` beside the executable in either emulator.
3. Offline and LAN play require no account. **Lichess setup → Test connection** checks the API before sign-in. For online play, run `pwsh -File lichess-sign-in.ps1 -Console 3DS -CardRoot E:\` (or `-Console PSP`), replacing `E:\` with the emulator's virtual SD or Memory Stick folder. Complete browser authorization. Run it separately for each emulator. The helper verifies the account before saving its token.
4. Open **Lichess setup → Verify account**, then **Online lobby**. Choose 15+10, 10+0 or 30+0, find an opponent, type a username challenge, accept/decline incoming challenges, or resume an active game.
5. To play a public opponent, sign in and choose **Find opponent**. Lichess's public seek API requires the `board:play` permission. You can select **Cancel search** while waiting. If the search ends before a game opens, **Find opponent** is ready to select again.

If PPSSPP shows a graphics startup dialog offering OpenGL, choose **Yes** to retry with that backend.

## Play on LAN in emulators

On the same computer, open both emulators and choose **Host LAN match** on one. In the other, choose **Join LAN match** and enter `127.0.0.1`. On separate computers on the same network, enter the host computer's LAN IPv4 address. The game uses TCP port `49373`. Keep the host match open during reconnect.

## Play over the internet

Each player signs in to their own Lichess account. Players can be on different networks and use either emulator build or the Lichess website. Choose **Online lobby → Find opponent** for a public game or **Challenge player** to invite someone by username. You can also follow someone under **Friends → Add by username** and challenge them from your list. Accept invitations under **Incoming**.

Keep the lobby open while you're waiting for an invitation response. **Stop waiting** closes the invitation stream. Lichess may take a moment to expire the invite. If your connection drops after a game starts, reopen it from **Active games**.

The Friends list uses your Lichess follows. Open it again, select **Refresh**, or press Start to update the list and presence. On 3DS, use **Page up** and **Page down** to scroll by touch when those buttons appear. Select a friend and choose **Unfollow** to remove them after confirmation. The handheld displays up to 1,000 followed accounts.

If you signed in with an older helper, rerun the bundled helper with the same card arguments plus `-Replace` to grant Friends permissions. Ordinary username challenges continue to work with the earlier board permission.

During play, use D-pad and A/B on 3DS; D-pad, Cross and Square on PSP. Start opens match actions; PSP Triangle also opens them. Resignation requires confirmation. Draws and takebacks require the server's acceptance. Select changes Dark/Light immediately.

Use X on 3DS or Circle on PSP to open chat. The same three-dot speech bubble is available by touch on 3DS. Chat has a controller/touch QWERTY keyboard and five quick phrases. Choosing a phrase only fills the draft: press **Send** to submit. Back closes the composer before chat; X/Circle returns directly to the game. Drafts survive closing chat, connection loss and game completion, for the current session. Start in the transcript toggles notifications. L/R scrolls chat.

After a game, choose Rematch, New opponent or Analysis board. Online Rematch creates a casual challenge to the previous opponent with swapped colors. Analysis preserves the finished game while allowing replay, variations and a local engine continuation. Local engine assistance is unavailable during live online games.

Both devices communicate directly with Lichess or with each other on LAN. Use the Windows helper for sign-in or to renew permissions. Online clocks continue during menus and connection loss. Uncertain moves/actions/messages are never automatically resent. The game reconnects to synchronize server state, and failed chat drafts remain available.

The source, asset sources/licenses, build scripts, dependency versions and pinned PSP HTTPS installer are included in `source/`. `SHA256SUMS.txt` identifies packaged files. Do not share the card's `lichess-token.txt`.

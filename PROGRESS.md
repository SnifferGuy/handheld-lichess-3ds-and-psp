# Release progress — 2026-11-19

The emulator-focused **Version 1.0 ;3 release checks are complete**. Release acceptance is based on Azahar and PPSSPP.

Implemented and covered by desktop tests: rules/draws, bounded cancellable AI, offline/LAN clocks and takebacks, LAN framing/reconnect/deduplication, Dark/Light appearance and assets, online game streams/moves/offers, chat/keyboard/drafts/history, matchmaking/challenges, results/rematches, and isolated online/offline analysis.

Build verified: HTTPS-enabled `.3dsx` and `EBOOT.PBP`, certificate bundle, sign-in helper self-tests, repeatable scripts, source/assets/licenses and release packaging.

Azahar and PPSSPP reached the live Lichess API without an account token. Browser sign-in and live reads for account verification, Active Games, and the Friends list passed in both emulators. The player confirmed public matchmaking and live moves on both builds and a private challenge, chat, Friends, and game resume between the emulators. Both emulator hosting directions passed LAN diagnostics for checkmate, reconnect before a move or promotion, lost move acknowledgments, mutual rematches, and repetition claims after reconnect. [TEST-REPORT.md](TEST-REPORT.md) identifies which checks used live play, desktop fixtures, or injected disconnects.

# Release progress — 2026-11-18

The original release plan is **about 85% complete**, including physical acceptance and delivery. The PC cross-play and Friends update is **75% complete** on its four-milestone scale. The service, menus, automated verification and both rebuilt packages are complete. The last milestone is live verification on both handhelds.

Implemented and covered by desktop tests: rules/draws, bounded cancellable AI, offline/LAN clocks and takebacks, LAN framing/reconnect/deduplication, Dark/Light appearance and assets, online game streams/moves/offers, chat/keyboard/drafts/history, matchmaking/challenges, results/rematches, and isolated online/offline analysis.

Build verified: HTTPS-enabled `.3dsx` and `EBOOT.PBP`, certificate bundle, sign-in helper self-tests, repeatable scripts, source/assets/licenses and release packaging.

Still pending:

- Real browser sign-in and revoked-credential recovery on a user's account.
- Full Lichess games, chat and interrupted requests on each physical console.
- PC-to-handheld challenges in both directions, a full browser game on each console, and follow/unfollow synchronization with the website.
- PSP-hosted and 3DS-hosted cross-console LAN games on the actual shared Wi-Fi.
- Firmware/AP configuration record, GPU/touch readability, sleep/resume and PSP-1000 memory measurements.

No firmware, router configuration, memory card credentials or real matches were changed by automated testing. See `TEST-REPORT.md` for the physical checklist.

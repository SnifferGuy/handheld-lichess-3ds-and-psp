# Build dependencies

- C++17 shared game code. Desktop verification: Zig 0.15.2.
- 3DS: devkitARM r68 / GCC 16.1.0, libctru 2.7.1, Citro2D 1.7.0, Citro3D 1.7.1; curl 8.4.0, mbedTLS 2.28.8, zlib 1.3.1. Build with `build-3ds.ps1 -LichessHttps`.
- PSP: portable PSPDEV GCC 15.2.0 v2; PSPSDK commit `9de3ef274702e2f4975bb68569fb3ec4e4a8efe3`. `install-psp-https.ps1` verifies and installs official PSPDEV curl 7.64.1-13, mbedTLS 2.28.10-2 and zlib 1.3.1-8 into the project-local SDK. Build with `build-psp-windows.ps1 -LichessHttps` or `HANDHELD_CURL=1 bash build-psp.sh` with PSPDEV.
- Asset regeneration: Pillow 13.0.0, resvg-py 0.3.3 and devkitPro tex3ds. SVG/font sources and attribution are in `assets/`. The compiled atlas does not require Python to build the game.
- PicoJSON 1.3.0 with bounded-depth/size callers and the documented no-exception numeric fix. BSD license in `picojson.hpp`.
- CA bundle: Mozilla data extracted by curl.se, 2026-11-05; SHA-256 `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285`. Both builds require certificate and hostname verification.

PSP HTTPS package URLs and SHA-256 values are pinned in `install-psp-https.ps1`. Compiler version outputs are included with the release. The SDK installation uses the portable Windows fallback because WSL was unavailable; physical networking behavior remains a separate acceptance gate.

Library license texts are in `licenses/`; Cburnett GPL and Noto Sans OFL texts are in `assets/`. The CA bundle retains its upstream provenance header. Sources: https://pspdev.github.io/psp-packages/ and https://curl.se/docs/caextract.html.

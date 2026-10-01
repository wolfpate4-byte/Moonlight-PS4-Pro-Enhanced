# Moonlight PS4 Pro Enhanced

## 4K30 on PS4 Pro - hardware decoded and real-game tested

An enhanced PS4 Pro build of [Moonlight-PS4](https://github.com/JaimeJimenezG/Moonlight-ps4), based on v1.1.0.

## Highlights

- 3840x2160 @ 30 FPS streaming
- H.264 hardware decoding via PS4 Videodec2
- Tested at 40 Mbps
- DualShock 4 input
- Opus stereo audio
- Real-game tested with Kingdom Come: Deliverance and Satisfactory
- 1080p60 remains available for lower-latency gaming

## 4K30

4K30 works especially well for slower-paced and controller-friendly games.

For competitive FPS and other latency-sensitive games, 1080p60 is recommended.

## Tested setup

- PS4 Pro
- Firmware 12.50
- GoldHEN 2.4b18.7
- Sunshine 2026.516.143833
- 3840x2160 @ 30 FPS
- 40000 kbps
- H.264
- Hardware decoder enabled
- YCbCr disabled

Other PS4 models and firmware versions have not been validated with this release.

## YCbCr / firmware warning

> [!WARNING]
> `ycbcr_kpatch_900.bin` is intended for firmware 9.00 only.
>
> Do NOT use it on any other firmware version.
>
> On firmware 12.50, Moonlight PS4 Pro Enhanced was tested with YCbCr disabled and the BGRA presentation path.

The firmware-9.00 YCbCr patch is intentionally not included in this release.

## Installation

1. Download `Moonlight-PS4-Pro-Enhanced-4K30.pkg`.
2. Install the PKG on your already modified PS4.
3. Start Sunshine on the gaming PC.
4. Start Moonlight on the PS4.
5. Pair with Sunshine when prompted.
6. Select an application and start streaming.

Recommended tested 4K30 settings:

- Resolution: 3840x2160
- FPS: 30
- Bitrate: 40000 kbps
- Codec: H.264
- Hardware decoder: enabled
- YCbCr: disabled

For lower input latency, use 1920x1080 @ 60 FPS.

## Release package

The release binary intentionally keeps the internal upstream version `1.1.0`.

SHA-256:

`99ff7b50559f359d2b660b9ade0a1151d9b795999221b3240a2a7f28118a8b71`

## Credits

Based on Moonlight-PS4 by Jaime Jimenez:

https://github.com/JaimeJimenezG/Moonlight-ps4

Permission to publish the modified source and compiled homebrew package:

https://github.com/JaimeJimenezG/Moonlight-ps4/issues/4

Thanks to Jaime for creating the original PS4 port.

This project also uses Moonlight Common C, OpenOrbis, OpenGNM, FFmpeg, Opus, Mbed TLS, ENet, NanoRS and h264bitstream.

## Disclaimer

Moonlight PS4 Pro Enhanced is an unofficial community project.

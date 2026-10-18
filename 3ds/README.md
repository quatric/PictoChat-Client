# PictoChat for 3DS

A 3DS homebrew client for [PictoChat Online](https://pict.chat) by ayunami2000, ported from [PictoChat for Wii](https://github.com/quatric/PictoChat-Client/tree/main/wii). Join the chat rooms, type with the DS keyboards and draw with the stylus. It looks, sounds and behaves like pict.chat, and you chat with everyone using the website.

The two DS screens go where they belong: the message log on the top screen and the drawing box and keyboards on the touch screen. They are scaled up to fill both screens (the touch screen exactly), or shown pixel for pixel like a DS game if you press SELECT.

## Installing

1. Extract `PictoChat-3DS.zip` to the root of your SD card. It adds `3ds/PictoChat/PictoChat.3dsx`.
2. Open PictoChat from the Homebrew Launcher. The 3DS needs Wi-Fi.

Or install `PictoChat-3DS.cia` with FBI to get PictoChat on the HOME Menu.

For sound, the DSP firmware has to be dumped once with [DSP1](https://github.com/zoogie/DSP1) (`sdmc:/3ds/dspfirm.cdc`). Without it the app works silently.

## Joining

Choose a name and a colour, then select **Join PictoChat**. PictoChat Online asks everyone to solve a captcha first, and a 3DS can't show one. The app shows a short code and a QR code instead. On your phone or computer, open [pictocard.net/pictochat](https://pictocard.net/pictochat), enter the code (or scan the QR code) and solve the captcha. Your 3DS then joins by itself. You only have to do this when you connect, not for every room.

Public rooms are public and logged by PictoChat Online, and flagged messages go to its moderators. Don't share private information.

## Controls

The stylus works like on a DS: draw in the box, tap keys, tools and rooms, or drag a key into the box to drop a letter anywhere.

| Button | In a room | Elsewhere |
| --- | --- | --- |
| A | Send | Join (on the first screen) |
| B | Backspace | Back (Backspace while typing your name) |
| X | Clear | Dark Mode (on the first screen) |
| Y | Type with the system keyboard | Type your name with the system keyboard |
| L / ZL | Next keyboard (accents, kana, symbols, emoji) | |
| R / ZR | Renew (copy the message you're looking at) | |
| Up / Down | Scroll the messages | Scroll the room list |
| Left / Right | Pen size / pen, rainbow pen, eraser | |
| START | Leave the room | Back, or quit on the first screen |
| SELECT | Fill the screens / pixel for pixel | Same |

The Circle Pad (and the C-Stick on a New 3DS) works like the D-Pad.

In private rooms (Chat Room E), send `!join` followed by a room code to move to another private room. pict.chat's other commands (`!list`, `!tripcode`, `!block`) work too.

## Settings

`sdmc:/3ds/PictoChat/settings.txt` remembers your name, colour, Dark Mode (`dark=1`, the same as pict.chat's), the screen size (`pixel=1`) and the sound volume (`volume=0`..`100`). To use another PictoChat Online server, for example a self-hosted [ayunpictojava](https://github.com/ayunami2000/ayunpictojava):

```
chat=ws://192.168.1.10:8080/
site=http://192.168.1.10:8080
captcha=0
```

`captcha=0` skips the pairing step for servers that don't ask for a captcha. `pair=` points the pairing step at another PictoCard server.

## Building

You need devkitPro with devkitARM, libctru and citro2d (the `3ds-dev` group).

```bash
./scripts/build_portlibs.sh   # zlib, libpng, FreeType and mbedTLS 3.6, into ./portlibs/3ds
make                          # PictoChat.3dsx
make dist                     # dist/PictoChat-3DS.zip for the SD card
make cia                      # PictoChat.cia to install with FBI (needs makerom and bannertool)
```

`make AUTOTEST=1` builds a test version that replays `sdmc:/autotest.txt` (button presses, stylus touches and typing, see `source/autotest.c`), for exercising the app in an emulator such as Azahar. A self-hosted ayunpictojava server with no captcha secret is handy for this (`captcha=0` above). `make PROFILE=1` logs the time each frame takes to `sdmc:/pictochat-profile.txt`.

## How it works

The app speaks PictoChat Online's protocol: JSON messages over a WebSocket (mbedTLS for `wss://`, seeded by the 3DS's hardware random number generator), with drawings sent as the same stroke lists pict.chat uses, so messages look identical on both. The two DS screens are drawn in software at 256×384, as on the Wii. They are uploaded to the GPU only when something changed, at twice their size, and the GPU scales them onto the LCDs with filtering. That keeps the pixel art sharp without the uneven pixel widths of a plain 1.25× scale.

The captcha is relayed by pictocard.net (the `/pictochat` page and `/api/v1/pictochat` in the PictoCard website). It loads pict.chat's own hCaptcha, hands the token to the 3DS once, and keeps nothing.

## Credits

- [PictoChat Online](https://pict.chat) and [ayunpictojava](https://github.com/ayunami2000/ayunpictojava) by ayunami2000. The pictures, sounds, icon and NintendoDSBIOS font in `data/` come from it. The sounds were converted to raw PCM with `scripts/convert_sounds.sh`.
- PictoChat is a trademark of Nintendo. This app isn't affiliated with Nintendo or with PictoChat Online.
- Based on [PictoChat for Wii](https://github.com/quatric/PictoChat-Client/tree/main/wii)
- [libctru and citro2d](https://github.com/devkitPro), [mbedTLS](https://github.com/Mbed-TLS/mbedtls), [cJSON](https://github.com/DaveGamble/cJSON), FreeType, libpng, zlib and [QR Code generator](https://www.nayuki.io/page/qr-code-generator-library) by Project Nayuki (MIT)
- Pictochat font by Kirby3 (CC BY-SA 3.0), see `data/pictochat-license.txt`

Licensed under the GNU GPL v3.

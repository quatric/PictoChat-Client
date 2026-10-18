# PictoChat for Wii

A Homebrew Channel client for [PictoChat Online](https://pict.chat) by ayunami2000. Join the chat rooms, type with the DS keyboards and draw with the Wii Remote pointer. It looks, sounds and behaves like pict.chat, and you chat with everyone using the website.

## Installing

1. Extract `PictoChat-Wii.zip` to the root of your SD card or USB drive. It adds `apps/PictoChat`.
2. Open PictoChat from the Homebrew Channel. The Wii needs to be connected to the Internet.

## Joining

Choose a name and a colour, then select **Join PictoChat**. PictoChat Online asks everyone to solve a captcha first, and a Wii can't show one. The app shows a short code and a QR code instead. On your phone or computer, open [pictocard.net/pictochat](https://pictocard.net/pictochat), enter the code (or scan the QR code) and solve the captcha. Your Wii then joins by itself. You only have to do this when you connect, not for every room.

Public rooms are public and logged by PictoChat Online, and flagged messages go to its moderators. Don't share private information.

## Controls

The Wii Remote pointer is the stylus: point and hold **A** to draw, or tap keys, tools and rooms. Drag a key into the box to drop a letter anywhere.

| Button | In a room | Elsewhere |
| --- | --- | --- |
| A | Touch / draw | Select |
| B | Backspace | Back |
| + | Send | Join |
| − | Clear | |
| 1 | Next keyboard (accents, kana, symbols, emoji) | Dark Mode (on the first screen) |
| 2 | Zoom: both screens, the bottom screen, the top screen | Zoom (in the room list) |
| Up / Down | Scroll the messages | Scroll the room list |
| Left / Right | Pen size / pen, rainbow pen, eraser | |
| HOME | Leave the room | Quit |

Zooming shows one screen at twice the size, which makes the keyboard much easier to hit. While the bottom screen is zoomed, the HUD counts the messages that came in so you know to look up. Renew (copy the message you're looking at) is the button under Send on the touch screen.

Every controller that GlowWii supports works too: the Classic Controller, GameCube controller, Wii U GamePad and Pro Controller (on Wii U) and DualShock 3. Without a pointer, the D-Pad or stick moves the cursor. A USB mouse moves the pointer too. On a USB keyboard you can type straight into the box: Enter sends (Shift+Enter for a new line), arrows and Page Up/Down work like on pict.chat, Tab changes the keyboard, F1 zooms and Esc leaves the room.

In private rooms (Chat Room E), send `!join` followed by a room code to move to another private room. pict.chat's other commands (`!list`, `!tripcode`, `!block`) work too.

## Settings

`apps/PictoChat/settings.txt` remembers your name, colour, Dark Mode (`dark=1`, the same as pict.chat's) and the sound volume (`volume=0`..`100`). To use another PictoChat Online server, for example a self-hosted [ayunpictojava](https://github.com/ayunami2000/ayunpictojava):

```
chat=ws://192.168.1.10:8080/
site=http://192.168.1.10:8080
captcha=0
```

`captcha=0` skips the pairing step for servers that don't ask for a captcha. `pair=` points the pairing step at another PictoCard server.

## Building

You need devkitPro with devkitPPC and these portlibs: `ppc-freetype ppc-libpng ppc-libjpeg-turbo ppc-libwebp ppc-zlib ppc-bzip2 ppc-brotli`.

```bash
./scripts/build_portlibs.sh   # mbedTLS 3.6 and the extra controller libraries, into ./portlibs
make                          # PictoChat.dol and the compressed PictoChat-xz.dol
make dist                     # dist/PictoChat-Wii.zip for the Homebrew Channel
```

`make` also writes `PictoChat-xz.dol`, the same program packed into a self-extracting `.dol` a fraction of the size; `make dist` packages it as `boot.dol`. The first build downloads and builds [dolxz](https://github.com/FIX94/dolxz), which needs a host C compiler and liblzma (Debian/Ubuntu: `apt-get install liblzma-dev pkg-config`; macOS: `brew install xz`). `make NO_DOLXZ=1` skips this and uses the plain `.dol`.

`make AUTOTEST=1` builds a test version that replays `sd:/autotest.txt` (button presses, typing and pointer moves) and saves screenshots to `sd:/shots`, for exercising the app in Dolphin. A self-hosted ayunpictojava server with no captcha secret is handy for this (`captcha=0` above).

## How it works

The app speaks PictoChat Online's protocol: JSON messages over a WebSocket (mbedTLS for `wss://`), with drawings sent as the same stroke lists pict.chat uses, so messages look identical on both. Rendering is done in software at the DS's 256×384 and scaled to the TV by the GPU.

The captcha is relayed by pictocard.net (the `/pictochat` page and `/api/v1/pictochat` in the PictoCard website). It loads pict.chat's own hCaptcha, hands the token to the Wii once, and keeps nothing.

## Credits

- [PictoChat Online](https://pict.chat) and [ayunpictojava](https://github.com/ayunami2000/ayunpictojava) by ayunami2000. The pictures, sounds and NintendoDSBIOS font in `data/` come from it. The sounds were converted to raw PCM with `scripts/convert_sounds.sh`.
- PictoChat is a trademark of Nintendo. This app isn't affiliated with Nintendo or with PictoChat Online.
- Graphics, text, input and networking code from PictoCard for Wii; controller support from GlowWii, using [libwiidrc](https://github.com/FIX94/libwiidrc), [libwupc](https://github.com/FIX94/libwupc) and [libsicksaxis](https://github.com/xerpi/libsicksaxis)
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls), [cJSON](https://github.com/DaveGamble/cJSON), FreeType, libpng and [QR Code generator](https://www.nayuki.io/page/qr-code-generator-library) by Project Nayuki (MIT)
- Pictochat font by Kirby3 (CC BY-SA 3.0), see `data/pictochat-license.txt`
- Wii pointer cursor from [PrimmR's Wii Pointer Cursors](https://primmr.dev/projects/wii-pointer-cursors/)

Licensed under the GNU GPL v3.

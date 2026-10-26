# PictoChat for Switch

A Nintendo Switch homebrew client for [PictoChat Online](https://pict.chat) by ayunami2000. Join the chat rooms, type with the DS keyboards and draw on the touch screen, just like PictoChat on the Nintendo DS. It looks, sounds and behaves like pict.chat, and you chat with everyone using the website.

This is the Switch port of [PictoChat for Wii](https://github.com/quatric/PictoChat-Client/tree/main/wii).

## Installing

You need a Switch running custom firmware with the Homebrew Menu (hbmenu).

1. Extract `PictoChat-Switch.zip` to the root of your SD card. It adds `switch/PictoChat/PictoChat.nro`.
2. Open PictoChat from the Homebrew Menu (start hbmenu by holding R while launching a game, rather than from the Album, so the app gets all the memory). The Switch needs to be connected to the Internet.

## Joining

Choose a name and a colour, then select **Join PictoChat**. PictoChat Online asks everyone to solve a captcha first, and the Switch can't show one. The app shows a short code and a QR code instead. On your phone or computer, open [pictocard.net/pictochat](https://pictocard.net/pictochat), enter the code (or scan the QR code) and solve the captcha. Your Switch then joins by itself. You only have to do this when you connect, not for every room.

Public rooms are public and logged by PictoChat Online, and flagged messages go to its moderators. Don't share private information.

## Controls

The touch screen (handheld mode) is the stylus: touch and drag to draw, or tap keys, tools and rooms. Drag a key into the box to drop a letter anywhere. When the Switch is docked, the D-Pad or left stick moves a cursor and A clicks.

| Switch | In a room | Elsewhere |
| --- | --- | --- |
| Touch / A | Touch / draw | Select |
| B | Backspace | Back |
| + / R / ZR | Send | Join |
| L / ZL | Clear | |
| Y | Next keyboard (accents, kana, symbols, emoji) | Dark Mode (on the first screen) |
| X | Next screen layout | Next screen layout |
| Up / Down | Scroll the messages | Scroll the room list |
| Left / Right | Pen size / pen, rainbow pen, eraser | |
| − | Leave the room | Quit |

The Home button belongs to the system, so − leaves the room and quits. Renew (copy the message you're looking at) is the button under Send on the touch screen.

## Screen layouts

X (F1 on a USB keyboard) cycles through the layouts of the DS Virtual Console: Both screens, Bottom screen, Top screen, Side by side, Large bottom screen and Large top screen. The side panels show the current layout's name, and the app remembers your choice. The Switch has one screen, so the layouts that need a second picture on the Wii U GamePad aren't offered. Bottom screen and Top screen only show up in the room list and the chat rooms, because the setup screens need both DS screens.

On a USB keyboard you can type straight into the box: Enter sends (Shift+Enter for a new line), arrows and Page Up/Down work like on pict.chat, Tab changes the keyboard, F1 changes the layout and Esc leaves the room.

In private rooms (Chat Room E), send `!join` followed by a room code to move to another private room. pict.chat's other commands (`!list`, `!tripcode`, `!block`) work too.

## Settings

`sd:/switch/PictoChat/settings.txt` remembers your name, colour, Dark Mode (`dark=1`, the same as pict.chat's), the screen layout (`layout=both`, `bottom`, `top`, `side`, `big-bottom`, `big-top`, `split`, ) and the sound volume (`volume=0`..`100`). To use another PictoChat Online server, for example a self-hosted [ayunpictojava](https://github.com/ayunami2000/ayunpictojava):

```
chat=ws://192.168.1.10:8080/
site=http://192.168.1.10:8080
captcha=0
```

`captcha=0` skips the pairing step for servers that don't ask for a captcha. `pair=` points the pairing step at another PictoCard server.

## Building

You need devkitPro with devkitA64, libnx, `switch-sdl2` and these portlibs: `switch-freetype switch-libpng switch-libjpeg-turbo switch-libwebp switch-zlib switch-bzip2`.

```bash
./scripts/build_portlibs.sh   # mbedTLS 3.6, into ./portlibs
make                          # PictoChat.nro
make dist                     # dist/PictoChat-Switch.zip for the SD card
```

`make AUTOTEST=1` builds a test version that replays `sd:/autotest.txt` and saves screenshots to `sd:/shots`.

## How it works

The app speaks PictoChat Online's protocol: JSON messages over a WebSocket (mbedTLS for `wss://`), with drawings sent as the same stroke lists pict.chat uses, so messages look identical on both. Rendering is done in software at the DS's 256×384 and scaled by the GPU (through SDL2) to the Switch's screen.

The captcha is relayed by pictocard.net (the `/pictochat` page and `/api/v1/pictochat` in the PictoCard website). It loads pict.chat's own hCaptcha, hands the token to the Switch once, and keeps nothing.

## Credits

- [PictoChat Online](https://pict.chat) and [ayunpictojava](https://github.com/ayunami2000/ayunpictojava) by ayunami2000. The pictures, sounds and NintendoDSBIOS font in `data/` come from it. The sounds were converted to raw PCM with `scripts/convert_sounds.sh`.
- PictoChat is a trademark of Nintendo. This app isn't affiliated with Nintendo or with PictoChat Online.
- [libnx](https://github.com/switchbrew/libnx) and [SDL2 for Switch](https://github.com/devkitPro/SDL)
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls), [cJSON](https://github.com/DaveGamble/cJSON), FreeType, libpng and [QR Code generator](https://www.nayuki.io/page/qr-code-generator-library) by Project Nayuki (MIT)
- Pictochat font by Kirby3 (CC BY-SA 3.0), see `data/pictochat-license.txt`
- Wii pointer cursor from [PrimmR's Wii Pointer Cursors](https://primmr.dev/projects/wii-pointer-cursors/)

Licensed under the GNU GPL v3.

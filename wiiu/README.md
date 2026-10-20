# PictoChat for Wii U

A Wii U homebrew client for [PictoChat Online](https://pict.chat) by ayunami2000. Join the chat rooms, type with the DS keyboards and draw on the GamePad's touch screen, just like PictoChat on the Nintendo DS. It looks, sounds and behaves like pict.chat, and you chat with everyone using the website.

This is the Wii U port of [PictoChat for Wii](https://github.com/quatric/PictoChat-Client/tree/main/wii).

## Installing

You need the [Aroma](https://aroma.foryour.cafe) environment.

1. Extract `PictoChat-WUP.zip` to the root of your SD card. It adds `wiiu/apps/PictoChat/PictoChat.wuhb`.
2. Open PictoChat from the Wii U Menu. The Wii U needs to be connected to the Internet.

### Updates

At launch PictoChat checks for a newer release. When there is one it offers it: **A** downloads it and installs it over this copy, then you return to the Wii U Menu; **B** skips it. Your settings are kept. This is done by [libwiiuupdate](https://github.com/quatric/libwiiuupdate).

## Joining

Choose a name and a colour, then select **Join PictoChat**. PictoChat Online asks everyone to solve a captcha first, and the Wii U can't show one. The app shows a short code and a QR code instead. On your phone or computer, open [pictocard.net/pictochat](https://pictocard.net/pictochat), enter the code (or scan the QR code) and solve the captcha. Your Wii U then joins by itself. You only have to do this when you connect, not for every room.

Public rooms are public and logged by PictoChat Online, and flagged messages go to its moderators. Don't share private information.

## Controls

The GamePad's touch screen is the stylus: touch and drag to draw, or tap keys, tools and rooms. Drag a key into the box to drop a letter anywhere.

| GamePad | Wii Remote | In a room | Elsewhere |
| --- | --- | --- | --- |
| Touch | Point + A | Touch / draw | Select |
| B | B | Backspace | Back |
| + / R / ZR | + | Send | Join |
| − / L / ZL | − | Clear | |
| Y | 1 | Next keyboard (accents, kana, symbols, emoji) | Dark Mode (on the first screen) |
| X | 2 | Next screen layout | Next screen layout |
| Up / Down | Up / Down | Scroll the messages | Scroll the room list |
| Left / Right | Left / Right | Pen size / pen, rainbow pen, eraser | |
| HOME | HOME | Leave the room | Quit |

Renew (copy the message you're looking at) is the button under Send on the touch screen.

## Screen layouts

X (2 on a Wii Remote, F1 on a keyboard) cycles through the layouts of the DS Virtual Console. The side panels show the current layout's name, and the app remembers your choice.

| Layout | TV | GamePad |
| --- | --- | --- |
| Both screens | Both screens stacked, as on pict.chat | Same as the TV |
| Bottom screen | The bottom screen at twice the size | Same as the TV |
| Top screen | The top screen at twice the size | Same as the TV |
| Side by side | Both screens next to each other | Same as the TV |
| Large bottom screen | A large bottom screen next to a small top screen | Same as the TV |
| Large top screen | A large top screen next to a small bottom screen | Same as the TV |
| TV and GamePad | The top screen | The bottom screen |
| Upright GamePad | Both screens stacked | Both screens stacked across the whole GamePad; hold it upright with the D-Pad at the bottom |
| Upright GamePad (flipped) | Both screens stacked | The same, with the D-Pad at the top |

When you hold the GamePad upright, its D-Pad and stick turn with it, so Up is still up. Bottom screen and Top screen only show up in the room list and the chat rooms, because the setup screens need both DS screens. While pairing, only Both screens and the upright layouts are available. When you're on the Bottom screen layout, the side panel counts the messages that came in so you know to look up.

The Wii Remote (pointer and buttons, with or without a Nunchuk), Classic Controller and Wii U Pro Controller work too. Without a pointer or the touch screen, the D-Pad or stick moves a cursor. On a USB keyboard you can type straight into the box: Enter sends (Shift+Enter for a new line), arrows and Page Up/Down work like on pict.chat, Tab changes the keyboard, F1 changes the layout and Esc leaves the room.

In private rooms (Chat Room E), send `!join` followed by a room code to move to another private room. pict.chat's other commands (`!list`, `!tripcode`, `!block`) work too.

## Settings

`sd:/wiiu/apps/PictoChat/settings.txt` remembers your name, colour, Dark Mode (`dark=1`, the same as pict.chat's), the screen layout (`layout=both`, `bottom`, `top`, `side`, `big-bottom`, `big-top`, `split`, `upright` or `upright-flipped`) and the sound volume (`volume=0`..`100`). To use another PictoChat Online server, for example a self-hosted [ayunpictojava](https://github.com/ayunami2000/ayunpictojava):

```
chat=ws://192.168.1.10:8080/
site=http://192.168.1.10:8080
captcha=0
```

`captcha=0` skips the pairing step for servers that don't ask for a captcha. `pair=` points the pairing step at another PictoCard server.

## Building

You need devkitPro with devkitPPC, [wut](https://github.com/devkitPro/wut), `wiiu-sdl2` and these portlibs: `ppc-freetype ppc-libpng ppc-libjpeg-turbo ppc-libwebp ppc-zlib ppc-bzip2 ppc-brotli`.

```bash
git submodule update --init   # libwiiuupdate
./scripts/build_portlibs.sh   # mbedTLS 3.6, into ./portlibs
make                          # PictoChat.rpx and PictoChat.wuhb
make dist                     # dist/PictoChat-WUP.zip for the SD card
```

`make AUTOTEST=1` builds a test version that replays `sd:/autotest.txt` (button presses, typing and pointer moves) and saves screenshots to `sd:/shots`, for exercising the app in Cemu. A self-hosted ayunpictojava server with no captcha secret is handy for this (`captcha=0` above).

## How it works

The app speaks PictoChat Online's protocol: JSON messages over a WebSocket (mbedTLS for `wss://`), with drawings sent as the same stroke lists pict.chat uses, so messages look identical on both. Rendering is done in software at the DS's 256×384 and scaled by the GPU (through SDL2) to the TV and the GamePad.

The captcha is relayed by pictocard.net (the `/pictochat` page and `/api/v1/pictochat` in the PictoCard website). It loads pict.chat's own hCaptcha, hands the token to the Wii U once, and keeps nothing.

## Credits

- [PictoChat Online](https://pict.chat) and [ayunpictojava](https://github.com/ayunami2000/ayunpictojava) by ayunami2000. The pictures, sounds and NintendoDSBIOS font in `data/` come from it. The sounds were converted to raw PCM with `scripts/convert_sounds.sh`.
- PictoChat is a trademark of Nintendo. This app isn't affiliated with Nintendo or with PictoChat Online.
- [wut](https://github.com/devkitPro/wut) and [SDL2 for Wii U](https://github.com/devkitPro/SDL)
- [mbedTLS](https://github.com/Mbed-TLS/mbedtls), [cJSON](https://github.com/DaveGamble/cJSON), FreeType, libpng and [QR Code generator](https://www.nayuki.io/page/qr-code-generator-library) by Project Nayuki (MIT)
- Pictochat font by Kirby3 (CC BY-SA 3.0), see `data/pictochat-license.txt`
- Wii pointer cursor from [PrimmR's Wii Pointer Cursors](https://primmr.dev/projects/wii-pointer-cursors/)

Licensed under the GNU GPL v3.

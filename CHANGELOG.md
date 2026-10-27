# Changelog

## 1.4.0 (2026-10-27)

**Switch**
- New: PictoChat for the Nintendo Switch, for the Homebrew Menu. Touch screen, Joy-Cons and Pro Controller, USB keyboard.

## 1.3.1 (2026-10-23)

**Names**
- Your name is always 10 characters at most, like on a DS. This now includes the name PictoChat Online confirms when you join, which could be longer and then showed up in your messages and in the DS Room.

**Wii U and 3DS**
- No changes besides the version number.

## 1.3.0 (2026-10-23)

**DS Room (Wii)**
- Your Wii can now host a PictoChat room for Nintendo DS and 3DS consoles. Choose **DS Room** (or press **−**) on the first screen, then pick Chat Room A, B, C or D, and join the same room from PictoChat on a DS or 3DS. Drawings and text go both ways, and DS players show up with their own names and colors.
- Messages from the Wii only take as many lines as they need on the DS, and the name tabs are sized like the DS's so they don't cover drawings.
- The DS Room uses the Wii's wireless chip, so the Internet is off until you restart PictoChat after leaving a DS room.
- Based on PiictoChat and libWD by Abdelali221.

**Drawing tablets (Wii)**
- Draw with a USB pen tablet (Wacom Intuos, One and Bamboo, Huion, XP-Pen, Gaomon and others) or a uDraw GameTablet on a Wii Remote. The whole tablet is the touch screen, fast strokes stay smooth, and turning a Wacom pen over to its eraser end erases.

**USB mouse (Wii)**
- A USB mouse now works alongside a drawing tablet, and no longer stalls the app every few seconds.

**Updates (Wii U and 3DS)**
- PictoChat for Wii U and 3DS now check for a newer release at launch and offer to install it, like the Wii version: **A** installs it over this copy (the `.wuhb` on Wii U; the `.3dsx`, or the CIA when PictoChat is on the HOME Menu, on 3DS), **B** skips it. Your settings are kept.

## 1.2.1 (2026-10-20)

**Wii U and 3DS**
- PictoChat for Wii U and PictoChat for 3DS are now part of every release, next to the Wii version.
- **Wii U** (needs Aroma): the GamePad's touch screen is the stylus, and Wii Remotes, Classic and Pro Controllers and USB keyboards work too. X cycles through the DS Virtual Console's screen layouts, including TV and GamePad and an upright GamePad.
- **3DS**: the message log goes on the top screen and the drawing box and keyboards on the touch screen, scaled to fill them or pixel for pixel with SELECT. Type with the system keyboard on Y. Install `PictoChat-3DS.cia` with FBI to get PictoChat on the HOME Menu.

**New icon (Wii)**
- A new Homebrew Channel icon: the PictoChat button, filling the full 128×48 frame with the logo centered.

**Updates (Wii)**
- The update check now stops cleanly when you quit, so exiting no longer waits on or races with it.

**Input (Wii)**
- Controller support now comes from libwiiinput (the renamed libinput-wii). Nothing changes for you.

## 1.2.0 (2026-10-18)

**Updates itself (Wii)**
- At launch the app checks GitHub for a newer release. If there is one, a popup offers it: **A** downloads it and installs it over this copy, then returns to the Homebrew Channel. **B** closes the popup. Without a connection, or if the check fails, nothing is shown.
- Connecting to the server waits until the update check has finished, so the popup comes first.

**Input (Wii)**
- All controllers and the USB keyboard go through libinput-wii (Wii Remote, Nunchuk, Classic Controller, GameCube, Wii U Pro and Wii U GamePad, where supported), updated to its latest version.

**Smaller download (Wii)**
- `boot.dol` is now a compressed, self-extracting .dol, and it no longer hangs on boot in Dolphin.

**Repository renamed**
- The project now lives at quatric/PictoChat-Client. Older links and the update check follow the rename.

## 1.1.0 (2026-10-14)

**Zoom (Wii)**
- Press 2 (or F1 on a USB keyboard) in the room list or a room to switch between both screens, the bottom screen at twice the size, and the top screen at twice the size. The keyboard is much easier to hit when zoomed, and the HUD stays visible. While the bottom screen is zoomed, the HUD counts new messages so you know to look up.

**Dark Mode (Wii)**
- pict.chat's dark mode, rendered the same way as on the website. Turn it on with the checkbox or 1 on the first screen. It is saved in `settings.txt` as `dark=1`.

**Renew (Wii)**
- Renew (copy the message you're looking at) has moved from 2 to the button under Send on the touch screen, or Down on a USB keyboard.

## 1.0.0 (2026-10-14)

**PictoChat for Wii**
- A Homebrew Channel client for PictoChat Online: join the chat rooms, type with the DS keyboards and draw with the Wii Remote pointer, with pict.chat's own pictures and sounds.
- PictoChat Online's captcha is solved on another device through pictocard.net/pictochat, by entering a short code or scanning a QR code.

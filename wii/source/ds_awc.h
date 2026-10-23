#pragma once
#include <gctypes.h>

/* The parent side of AWC, the session layer DS PictoChat runs on top of the
 * DS MP protocol: the Wii hosts the room, introduces itself as a member,
 * arbitrates who may send, relays every message to all children and sends
 * its own messages. Ported from PiictoChat by Abdelali221
 * (github.com/abdelali221/PiictoChat). */

#define AWC_MAX_AID 15

/* MP ports used by PictoChat's AWC build */
#define AWC_PORT_SESSION     12
#define AWC_PORT_NEGOTIATION 13
#define AWC_PORT_TRANSPORT   14

/* Beacon parent_type values, as the big endian view of the DS u16 */
#define AWC_PARENTINFO_PSEUDO 0x4823
#define AWC_PARENTINFO_PARENT 0x8ABD

/* CHT (chat layer) messages */
#define CHT_USERINFO_SIZE 84
#define CHT_PIC_HEADER    36
#define CHT_PIC_MAX       (CHT_PIC_HEADER + 10 * 1024)
enum { CHT_LOGIN = 0, CHT_USERINFO = 1, CHT_PICTURE = 2 };

typedef struct {
    /* a child finished sending a CHT message (LOGIN, USERINFO or PICTURE) */
    void (*message)(u16 aid, const u8 *msg, u32 size);
    /* a child left the room */
    void (*left)(u16 aid);
} awc_callbacks;

/* own_mac is the Wii's MAC as WD reports it. The callbacks run on the WD
 * receive thread. */
void awc_init(const u8 *own_mac, const awc_callbacks *cb);
/* Our own USERINFO: name (UTF-16, 10 chars), comment (26), colour 0..15 */
void awc_set_profile(const u16 *name, const u16 *comment, u8 color);

void awc_on_connect(u16 aid, const u8 *mac);
void awc_on_disconnect(u16 aid);
void awc_on_frame(const u8 *frame, s32 len);

/* Builds the next MP frame for WD_MpSendFrame. Returns its length and the
 * children to poll, or 0 when nobody is connected. */
u32 awc_next_frame(u8 *buf, u16 *pollbitmap);

/* Sends a PICTURE of 16..80 rows (8x8 4bpp tiles, 32 tiles per row of tiles)
 * to the room as soon as no transfer is running. 0, or -1 when too many are
 * still waiting. */
int awc_send_picture(const u8 *tiles, int rows);

/* A child's MAC in the DS AWCMacAddr layout (what CHT messages name their
 * sender with), for matching messages to members */
void awc_child_mac(u16 aid, u8 *mac_out);
void awc_own_mac(u8 *mac_out);
u8 awc_child_count(void);

/* A DS that stays in the room drops packets older than the last sequence
 * number it saw from us, so the counters carry on between sessions. */
void awc_save_seq(const char *path);
void awc_load_seq(const char *path);

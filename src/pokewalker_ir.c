#include "pokewalker_ir.h"

#include "global.h"

#include "system.h"

// Pokewalker infrared port (co-op mod), see pokewalker_ir.h.
//
// Retail HGSS talks to the Pokewalker through the IR chip that sits in front
// of the save chip in its Game Card (SPI commands 01h receive, 02h send). On
// a flashcard there is no such chip. RtcPwalker (a usrcheat.dat cheat for
// TWiLight Menu++ on 3DS) uses the 3DS's own IR port instead:
//   - its ARM7 half hooks the ARM7 VBlank handler and installs a service on
//     the IPC sync interrupt that drives the 3DS IR port through RTCom;
//   - its ARM9 half rewrites the app's four IR calls to ask that service.
// That ARM9 half took over unconditionally, so with the cheat on, a Game Card
// IR chip was never used, and it waited forever for the service.
//
// The app's IR calls now come here, so a Game Card can be used with the
// cheat on or off. The cheat's ARM9 half only installs itself while the
// retail call is still at 0x021E5B9C, so it no longer applies; its ARM7 half
// does, and this file talks to it with the cheat's own protocol: a request
// block at 0x027FF400 and its data buffer at 0x027FF410 (shared main RAM,
// uncached, which the ARM7 reads directly), then an IPC sync interrupt. The
// ARM7 sets flags to 0xF0 when the request is done.

// All code and data below live in ITCM (main.lsf), like coop.c, so the mod
// adds nothing to the retail memory layout.
#include <nitro/itcm_begin.h>

typedef struct PwIrRtcomRequest {
    vu8 opcode;
    vu8 flags;
    vu16 size;
    u8 *volatile data;
} PwIrRtcomRequest;

#define PWIR_RTCOM_REQUEST ((PwIrRtcomRequest *)0x027FF400)
#define PWIR_RTCOM_BUFFER  ((u8 *)0x027FF410)

#define PWIR_RTCOM_SEND  1
#define PWIR_RTCOM_RECV  2
#define PWIR_RTCOM_BEGIN 3
#define PWIR_RTCOM_END   4

#define PWIR_FLAGS_PENDING 0x0F
#define PWIR_FLAGS_DONE    0xF0

// The app's IR buffer (ov112_021FFA18): 8-byte header + 0x84 bytes of payload.
#define PWIR_BUFFER_SIZE 0x8C

// SPI command 08h: IR chips in HG/SS, B/W and B2/W2 cards answer AAh.
#define PWIR_CARD_IR_VERSION 0xAA

// No answer from RTCom in this long means the cheat isn't running. Requests
// after that take a few dozen ms; their timeout is only there so a lost
// request can't hang the game.
#define PWIR_PROBE_TIMEOUT   OS_MilliSecondsToTicks(1000)
#define PWIR_REQUEST_TIMEOUT OS_MilliSecondsToTicks(3000)
// The cheat installs its service about 17 s after boot, and an interrupt
// sent before that is lost, so a pending request is signaled again.
#define PWIR_RESEND_INTERVAL OS_MilliSecondsToTicks(50)

typedef enum PwIrPort {
    PWIR_PORT_CARD,    // the Game Card's IR chip (retail code)
    PWIR_PORT_PROBING, // no Game Card IR chip, waiting for RTCom to answer
    PWIR_PORT_RTCOM,   // the 3DS IR port through the RtcPwalker cheat
} PwIrPort;

typedef enum PwIrPoll {
    PWIR_POLL_PENDING,
    PWIR_POLL_DONE,
    PWIR_POLL_TIMED_OUT,
} PwIrPoll;

static u8 sPwIrPort;
static OSTick sPwIrProbeStart;
static OSTick sPwIrLastSignal;

static void PwIr_SignalArm7(void) {
    // IPCSYNC: request the ARM7's IPC sync interrupt, keeping our output
    // bits (8-11) and interrupt enable (14) as they are.
    reg_PXI_SUBPINTF = (u16)((reg_PXI_SUBPINTF & 0x4F00) | 0x2000);
    sPwIrLastSignal = OS_GetTick();
}

static void PwIr_PostRequest(u8 opcode, u8 *data, u16 size) {
    PwIrRtcomRequest *request = PWIR_RTCOM_REQUEST;

    request->data = data;
    request->size = size;
    request->opcode = opcode;
    request->flags = PWIR_FLAGS_PENDING;
    PwIr_SignalArm7();
}

static PwIrPoll PwIr_PollRequest(OSTick start, OSTick timeout) {
    PwIrRtcomRequest *request = PWIR_RTCOM_REQUEST;
    OSTick now;

    if (request->flags == PWIR_FLAGS_DONE) {
        return PWIR_POLL_DONE;
    }
    now = OS_GetTick();
    if (now - start >= timeout) {
        // So a service that shows up later doesn't run a stale request.
        request->flags = 0;
        return PWIR_POLL_TIMED_OUT;
    }
    if (now - sPwIrLastSignal >= PWIR_RESEND_INTERVAL) {
        PwIr_SignalArm7();
    }
    return PWIR_POLL_PENDING;
}

// Spins like the cheat's ARM9 half did: the Pokewalker's timing is tight,
// and the app runs its IR calls on their own thread.
static BOOL PwIr_Request(u8 opcode, u8 *data, u16 size) {
    OSTick start;
    PwIrPoll result;

    PwIr_PostRequest(opcode, data, size);
    start = OS_GetTick();
    do {
        result = PwIr_PollRequest(start, PWIR_REQUEST_TIMEOUT);
    } while (result == PWIR_POLL_PENDING);
    return result == PWIR_POLL_DONE;
}

// Settles the port while the RTCom probe started by PokewalkerIr_Init is
// pending. With wait FALSE it only checks once.
static void PwIr_ResolveProbe(BOOL wait) {
    PwIrPoll result;

    if (sPwIrPort != PWIR_PORT_PROBING) {
        return;
    }
    do {
        result = PwIr_PollRequest(sPwIrProbeStart, PWIR_PROBE_TIMEOUT);
    } while (wait && result == PWIR_POLL_PENDING);
    if (result == PWIR_POLL_DONE) {
        sPwIrPort = PWIR_PORT_RTCOM;
    } else if (result == PWIR_POLL_TIMED_OUT) {
        sPwIrPort = PWIR_PORT_CARD;
    }
}

void PokewalkerIr_Init(void) {
    CARD_SpiWaitInit();

    // The retail IR chip test (ov01_021E662C, and the app right after this):
    // the first answer is thrown away.
    CARD_SpiWaitGetStatus();
    if (CARD_SpiWaitGetStatus() == PWIR_CARD_IR_VERSION) {
        sPwIrPort = PWIR_PORT_CARD;
        return;
    }

    // Ask RTCom to open the 3DS IR port. The answer is picked up by the IR
    // calls that follow, so the app doesn't freeze while it waits.
    sPwIrPort = PWIR_PORT_PROBING;
    PwIr_PostRequest(PWIR_RTCOM_BEGIN, NULL, 0);
    sPwIrProbeStart = OS_GetTick();
}

int PokewalkerIr_Recv(u8 *buf) {
    PwIrRtcomRequest *request = PWIR_RTCOM_REQUEST;
    int size;

    PwIr_ResolveProbe(FALSE);
    switch (sPwIrPort) {
    case PWIR_PORT_PROBING:
        return 0;
    case PWIR_PORT_RTCOM:
        if (!PwIr_Request(PWIR_RTCOM_RECV, PWIR_RTCOM_BUFFER, 0)) {
            return 0;
        }
        size = request->size;
        if (size > PWIR_BUFFER_SIZE) {
            size = PWIR_BUFFER_SIZE;
        }
        MI_CpuCopy8(PWIR_RTCOM_BUFFER, buf, size);
        return size;
    default:
        return CARD_SpiWaitReadRange(buf);
    }
}

void PokewalkerIr_Send(const u8 *buf, int len) {
    PwIr_ResolveProbe(TRUE);
    if (sPwIrPort == PWIR_PORT_RTCOM) {
        if (len > PWIR_BUFFER_SIZE) {
            len = PWIR_BUFFER_SIZE;
        }
        MI_CpuCopy8(buf, PWIR_RTCOM_BUFFER, len);
        PwIr_Request(PWIR_RTCOM_SEND, PWIR_RTCOM_BUFFER, len);
    } else {
        CARD_SpiWaitWriteRange(buf, len);
    }
}

void PokewalkerIr_ExitApp(int a0) {
    PwIr_ResolveProbe(TRUE);
    if (sPwIrPort == PWIR_PORT_RTCOM) {
        PwIr_Request(PWIR_RTCOM_END, NULL, 0);
    }
    sPwIrPort = PWIR_PORT_CARD;
    sub_0201A738(a0);
}

#include <nitro/itcm_end.h>

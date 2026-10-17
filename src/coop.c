#include "coop.h"

#include "global.h"

#include "constants/battle.h"
#include "constants/comm_type.h"
#include "constants/coop.h"
#include "constants/field/map_load.h"
#include "constants/game_stats.h"
#include "constants/global_fieldmap.h"
#include "constants/items.h"
#include "constants/maps.h"
#include "constants/movements.h"
#include "constants/sndseq.h"
#include "constants/std_script.h"

#include "bag.h"
#include "bag_view.h"
#include "bg_window.h"
#include "encounter.h"
#include "error_message_reset.h"
#include "field_system.h"
#include "follow_mon.h"
#include "frontier/overlay_80.h"
#include "game_stats.h"
#include "item.h"
#include "launch_application.h"
#include "map_header.h"
#include "map_object.h"
#include "message_format.h"
#include "msgdata.h"
#include "overlay_trainer_card.h"
#include "party.h"
#include "player_avatar.h"
#include "player_data.h"
#include "pm_string.h"
#include "pokedex.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "save_arrays.h"
#include "save_local_field_data.h"
#include "script.h"
#include "script_manager.h"
#include "scrcmd.h"
#include "string_util.h"
#include "system.h"
#include "task.h"
#include "text.h"
#include "unk_02005D10.h"
#include "unk_0202FBCC.h"
#include "unk_020517A4.h"
#include "unk_020551B8.h"
#include "unk_02055244.h"
#include "unk_020552A4.h"
#include "unk_02033AE0.h"
#include "unk_02034354.h"
#include "unk_02034B0C.h"
#include "unk_02035900.h"
#include "unk_020379A0.h"
#include "unk_02037C94.h"
#include "unk_0205BB1C.h"
#include "unk_0205FD20.h"
#include "unk_02062108.h"
#include "unk_02068FC8.h"

BOOL ScrNative_WaitApplication(ScriptContext *ctx);

// Comm command ids 0-21 are reserved by the comm system; the table
// registered with sub_0203410C is dispatched starting at id 22.
//
// The dispatcher does not tolerate an id >= 22 arriving while no table (or a
// different module's table) is registered: it calls through whatever is at
// that slot. So our table is registered the moment a session starts, and we
// only send while both sides are known to have it registered.
#define COOP_CMD_BASE     22
#define COOP_CMD_POS      (COOP_CMD_BASE + 0)
#define COOP_CMD_REQUEST  (COOP_CMD_BASE + 1)
#define COOP_CMD_RESPONSE (COOP_CMD_BASE + 2)
#define COOP_CMD_CARD     (COOP_CMD_BASE + 3)
#define COOP_CMD_MON      (COOP_CMD_BASE + 4)

// Trainer Card payload, as exchanged by the Union Room.
#define COOP_CARD_SIZE 0x66C

#define COOP_FLAG_VALID (1 << 0)

#define COOP_FOLLOW_VISIBLE      (1 << 0)
#define COOP_FOLLOW_SHINY        (1 << 1)
#define COOP_FOLLOW_GENDER_SHIFT 2

// Response answers on the wire. Values below 10 match COOP_RESULT_*.
#define COOP_ANS_PENDING 10 // prompt is on the partner's screen
#define COOP_ANS_CONFIRM 11 // gift sender took the item/money out; add it now
#define COOP_ANS_OFFER   12 // seller picked goods for a Buy request (payload in the response)
#define COOP_ANS_PAY     13 // buyer paid; seller hands over the goods now
#define COOP_ANS_REFUSE  14 // buyer turned the offer down
#define COOP_ANS_GOODS   15 // seller took the item out; buyer adds it now (Pokémon go by COOP_CMD_MON)

#define COOP_HEARTBEAT_FRAMES     30
#define COOP_SCAN_TIMEOUT_FRAMES  (30 * 60)
#define COOP_LINK_TIMEOUT_FRAMES  (20 * 60)
#define COOP_LOST_FRAMES          90
#define COOP_ANSWER_TIMEOUT       (15 * 60)
#define COOP_PROMPT_TIMEOUT       (180 * 60) // a human is deciding (may be picking from their Bag/party)
#define COOP_GOODS_TIMEOUT        (10 * 60)
#define COOP_CARD_TIMEOUT         (10 * 60)
#define COOP_CONFIRM_TIMEOUT      (10 * 60)
#define COOP_INCOMING_WAIT        (3 * 60)
#define COOP_ACTIVITY_SYNC_FRAMES (15 * 60)
#define COOP_POST_ACTIVITY_FRAMES (20 * 60)
#define COOP_SYNC_PRE_BASE        160
#define COOP_SYNC_POST_BASE       200
#define COOP_SYNC_RANGE           40

// Partner avatar is only spawned when close enough that its map chunk is loaded.
#define COOP_SPAWN_RANGE    16
#define COOP_DESPAWN_RANGE  20
// Beyond this many tiles of lag the avatar snaps instead of walking.
#define COOP_SNAP_RANGE     8
// With this many of the partner's moves still queued, replay them faster.
#define COOP_CATCHUP_MOVES  2
// Moves queued per object (power of 2). A full queue snaps.
#define COOP_PATH_LEN       8

// Movement command groups (asm/macros/movement.inc). Each is followed by its
// up/down/left/right variants. Frames per tile in brackets.
#define COOP_MOVE_WALK_SLOWER                4  // [32]
#define COOP_MOVE_WALK_SLOW                  8  // [16]
#define COOP_MOVE_WALK_NORMAL                MOVEMENT_STEP_UP // [8] walking
#define COOP_MOVE_WALK_FAST                  MOVEMENT_RUN_UP  // [4]
#define COOP_MOVE_WALK_FASTER                20 // [2] bike
#define COOP_MOVE_JUMP_NEAR_FAST             52 // 1 tile [8]
#define COOP_MOVE_JUMP_FAR                   56 // ledges: 2 tiles [16]
#define COOP_MOVE_WALK_SLIGHTLY_FAST         76 // [6]
#define COOP_MOVE_WALK_SLIGHTLY_FASTER       80 // [3]
#define COOP_MOVE_WALK_FASTEST               84 // [1]
#define COOP_MOVE_RUN                        88 // [4] running shoes; player sprites only
#define COOP_MOVE_JUMP_SIDEWAYS              92 // 92/93 1 tile left/right [16], 94/95 3 tiles [12]
#define COOP_MOVE_WALK_EVER_SO_SLIGHTLY_FAST 96 // [7]
#define COOP_MOVE_SNAP                       0xFE // not a movement: too far behind, jump there
#define COOP_MOVE_NONE                       0xFF // not a movement we replay

#define COOP_KEYS_COMBO (PAD_BUTTON_L | PAD_BUTTON_R)

typedef struct CoopPosPacket {
    u16 mapId;
    u16 x; // where the latest move ends (a ledge jump's landing tile)
    u16 z;
    s16 mapCellX;
    s16 mapCellY;
    u8 facing;
    u8 avatarState;
    u8 flags;
    u8 followForm;
    u16 followSpecies;
    u8 followInfo;
    u8 moveSeq; // bumped every time the player starts a move
    u8 moveCmd; // the MOVEMENT_* it used, or COOP_MOVE_NONE
    u8 unused;
} CoopPosPacket; // size: 0x16

// One of the partner's moves still to be replayed.
typedef struct CoopStep {
    u16 x; // where it ends
    u16 z;
    u8 cmd;
    u8 unused;
} CoopStep;

typedef struct CoopPath {
    CoopStep steps[COOP_PATH_LEN];
    u8 head;
    u8 count;
} CoopPath;

typedef struct CoopRequest {
    u8 type;
    u8 seq;
    u8 level;   // SELL_MON
    u8 unused;
    u16 itemId; // ITEM, SELL_ITEM
    u16 qty;    // ITEM, SELL_ITEM
    u32 amount; // MONEY: amount; SELL_*/BUY_*: price
    u16 species; // SELL_MON
    u16 unused2;
} CoopRequest; // size: 0x10

typedef struct CoopResponse {
    u8 type;
    u8 seq;
    u8 answer;
    // TRUE: about the recipient's own request (sCoop.outReq).
    // FALSE: about the request the recipient received (sCoop.inReq).
    u8 toRequester;
    // COOP_ANS_OFFER payload
    u16 itemId;
    u16 qty;
    u16 species;
    u8 level;
    u8 unused;
} CoopResponse; // size: 0xC

// Goods in a Sell/Buy deal: what we're selling, or what we're buying.
typedef struct CoopDeal {
    u8 kind; // COOP_DEAL_*
    u8 slot; // our party slot (when we're the seller of a Pokémon)
    u8 level;
    u8 toPC; // bought Pokémon went to the PC
    u16 itemId;
    u16 qty;
    u16 species;
    u16 unused;
    u32 price;
} CoopDeal;

typedef struct CoopCommCmd {
    void (*callback)(int netId, int size, void *data, void *ctx);
    int (*getSize)(void);
    void *(*getBuffer)(int netId, void *ctx, int size);
} CoopCommCmd;

enum {
    COOP_IN_IDLE,
    COOP_IN_PENDING,   // received, waiting for the field to be idle
    COOP_IN_PROMPTING, // std_coop_incoming is running
};

typedef struct CoopSystem {
    CoopState state;
    u32 timer;
    u16 heartbeat;
    u16 lostFrames;
    u8 activityCount;
    u8 profileSent;
    u8 partnerValid;
    u8 isHost;
    CoopPosPacket lastSent;
    CoopPosPacket partner;

    // Our moves, as sent to the partner
    u16 moveMapId;
    u16 moveCurX; // where the player stood last frame
    u16 moveCurZ;
    u16 moveDestX; // where the latest move ends
    u16 moveDestZ;
    u8 moveSeq;
    u8 moveCmd;
    u8 moveSkip; // tiles of a jump still to come (it moves one tile at a time)
    u8 moveTracked;

    // Replaying the partner's moves
    u8 partnerSeqValid;
    u8 partnerSeq;
    u8 avatarLastCmd; // how the avatar got onto its tile
    u8 monPlace;      // put the follower behind the avatar
    CoopPath avatarPath; // the partner's moves
    CoopPath monPath;    // tiles the avatar left, for the follower

    // Our request to the partner
    CoopRequest outReq;
    u8 outAnswer;
    u8 outPending;
    // Partner's request to us
    CoopRequest inReq;
    u8 inState;
    u8 inCancelled;
    u8 inConfirmed;
    u16 inTimer;
    u8 busyPending;
    u8 busySeq;
    u8 busyType;

    // Item / amount being prepared by the menu script
    u16 giftItem;
    u16 giftQty;
    u32 amount;     // gift money or price (COOP_SUB_SET_AMOUNT / ENTER_AMOUNT)
    u8 amountMode;  // COOP_AMOUNT_*

    // Sell / Buy deals
    CoopDeal deal;        // our goods when selling; the goods we're buying otherwise
    CoopResponse offer;   // the partner's offer for our Buy request
    u8 inPaid;            // seller: the buyer paid
    u8 inRefused;         // seller: the buyer turned our offer down
    u8 goodsReceived;     // buyer: item goods arrived
    u8 monReceived;       // buyer: Pokémon goods arrived
    Pokemon *monSend;     // sent by reference, must outlive the transfer
    Pokemon *monRecv;

    // Custom amount entry
    u8 pickDigit;         // selected digit, 0 = ones
    u32 pickValue;
    u32 pickMax;
    u16 pickMsg;

    // Trainer Card exchange
    u8 cardRequested;
    u8 cardReceived;
    TrainerCardAppArgs *cardSend; // sent by reference, must outlive the transfer
    u8 *cardRecv;
} CoopSystem;

// All co-op code AND data from here on lives in ITCM (main.lsf, Autoload
// ITCM: code and tables in .itcm, variables in .itcm.bss), so the mod adds
// nothing to main RAM. Every byte added to the static binary's .rodata,
// .data or .bss moves all later variables, every overlay and the heaps away
// from where retail HGSS has them, which breaks Action Replay cheats
// (usrcheat.dat) and nds-bootstrap's fixed-address patches.
// DMA and the ARM7 can't reach ITCM. That's fine for everything here: the
// comm layer copies outgoing data with the CPU (it's normally handed stack
// buffers, which live in DTCM), and big transfers use heap buffers.
#include <nitro/itcm_begin.h>

static CoopSystem sCoop;

static void Coop_RecvPos(int netId, int size, void *data, void *ctx);
static void Coop_RecvRequest(int netId, int size, void *data, void *ctx);
static void Coop_RecvResponse(int netId, int size, void *data, void *ctx);
static void Coop_RecvCard(int netId, int size, void *data, void *ctx);
static int Coop_GetPosSize(void);
static int Coop_GetRequestSize(void);
static int Coop_GetResponseSize(void);
static int Coop_GetCardSize(void);
static void *Coop_GetCardBuffer(int netId, void *ctx, int size);
static void Coop_RecvMon(int netId, int size, void *data, void *ctx);
static int Coop_GetMonSize(void);
static void *Coop_GetMonBuffer(int netId, void *ctx, int size);

// The tables below are read-only but deliberately not const: MWCC puts const
// data in .rodata (main RAM) even inside the ITCM section, which would move
// every retail variable after it.
static CoopCommCmd sCoopCmdTable[] = {
    { Coop_RecvPos,      Coop_GetPosSize,      NULL               },
    { Coop_RecvRequest,  Coop_GetRequestSize,  NULL               },
    { Coop_RecvResponse, Coop_GetResponseSize, NULL               },
    { Coop_RecvCard,     Coop_GetCardSize,     Coop_GetCardBuffer },
    { Coop_RecvMon,      Coop_GetMonSize,      Coop_GetMonBuffer  },
};

static s8 sDirDX[] = { 0, 0, -1, 1 };
static s8 sDirDZ[] = { -1, 1, 0, 0 };

#define COOP_PICK_DIGITS 6
static u32 sPow10[COOP_PICK_DIGITS] = { 1, 10, 100, 1000, 10000, 100000 };

static u8 sGiftPockets[] = {
    POCKET_ITEMS,
    POCKET_MEDICINE,
    POCKET_BALLS,
    POCKET_TMHMS,
    POCKET_BERRIES,
    POCKET_BATTLE_ITEMS,
    0xFF,
};

static u16 sGiftQuantities[] = { 1, 5, 10, 0xFFFF };
static u32 sPresetAmounts[] = { 100, 500, 1000, 5000, 10000 };

// COOP_DEBUG: what the fatal-error screen shows (see CoopDbg_Capture).
#define COOP_DBG_WORDS 12
static u32 sCoopDbgKind; // 1 = GF_ASSERT, 2 = alloc failure
static u32 sCoopDbgWords[COOP_DBG_WORDS];

static int Coop_GetMonSize(void) {
    return sizeof(Pokemon);
}

static int Coop_GetPosSize(void) {
    return sizeof(CoopPosPacket);
}

static int Coop_GetRequestSize(void) {
    return sizeof(CoopRequest);
}

static int Coop_GetResponseSize(void) {
    return sizeof(CoopResponse);
}

static int Coop_GetCardSize(void) {
    return COOP_CARD_SIZE;
}

// -------------------------------------------------------------------------
// Movement queues
// -------------------------------------------------------------------------

static void Coop_PathClear(CoopPath *path) {
    path->head = 0;
    path->count = 0;
}

static CoopStep *Coop_PathFront(CoopPath *path) {
    return path->count != 0 ? &path->steps[path->head] : NULL;
}

static void Coop_PathPop(CoopPath *path) {
    path->head = (path->head + 1) & (COOP_PATH_LEN - 1);
    path->count--;
}

// A full queue drops its oldest move; the replay then snaps.
static void Coop_PathPush(CoopPath *path, u16 x, u16 z, u8 cmd) {
    CoopStep *step;

    if (path->count == COOP_PATH_LEN) {
        Coop_PathPop(path);
    }
    step = &path->steps[(path->head + path->count) & (COOP_PATH_LEN - 1)];
    step->x = x;
    step->z = z;
    step->cmd = cmd;
    path->count++;
}

static CoopStep *Coop_PathBack(CoopPath *path) {
    return &path->steps[(path->head + path->count - 1) & (COOP_PATH_LEN - 1)];
}

// Moves that carry an object in a straight line: which way and how many
// tiles. Nothing else (turns, emotes, scripted effects) is ever replayed.
static BOOL Coop_GetMoveShape(u32 cmd, int *dir, int *tiles) {
    *dir = cmd & 3;
    *tiles = 1;
    switch (cmd & ~3) {
    case COOP_MOVE_WALK_SLOWER:
    case COOP_MOVE_WALK_SLOW:
    case COOP_MOVE_WALK_NORMAL:
    case COOP_MOVE_WALK_FAST:
    case COOP_MOVE_WALK_FASTER:
    case COOP_MOVE_JUMP_NEAR_FAST:
    case COOP_MOVE_WALK_SLIGHTLY_FAST:
    case COOP_MOVE_WALK_SLIGHTLY_FASTER:
    case COOP_MOVE_WALK_FASTEST:
    case COOP_MOVE_RUN:
    case COOP_MOVE_WALK_EVER_SO_SLIGHTLY_FAST:
        return TRUE;
    case COOP_MOVE_JUMP_FAR:
        *tiles = 2;
        return TRUE;
    case COOP_MOVE_JUMP_SIDEWAYS:
        *dir = (cmd & 1) ? DIR_EAST : DIR_WEST;
        *tiles = (cmd & 2) ? 3 : 1;
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL Coop_IsJump(u32 cmd) {
    u32 group = cmd & ~3;
    return group == COOP_MOVE_JUMP_NEAR_FAST || group == COOP_MOVE_JUMP_FAR || group == COOP_MOVE_JUMP_SIDEWAYS;
}

// A walk that's a step quicker, to catch up.
static u32 Coop_FasterWalk(u32 group) {
    switch (group) {
    case COOP_MOVE_WALK_SLOWER:
        return COOP_MOVE_WALK_SLOW;
    case COOP_MOVE_WALK_SLOW:
        return COOP_MOVE_WALK_NORMAL;
    case COOP_MOVE_WALK_NORMAL:
    case COOP_MOVE_WALK_SLIGHTLY_FAST:
    case COOP_MOVE_WALK_EVER_SO_SLIGHTLY_FAST:
        return COOP_MOVE_WALK_FAST;
    case COOP_MOVE_WALK_FAST:
    case COOP_MOVE_WALK_SLIGHTLY_FASTER:
    case COOP_MOVE_RUN:
        return COOP_MOVE_WALK_FASTER;
    default:
        return COOP_MOVE_WALK_FASTEST;
    }
}

// -------------------------------------------------------------------------
// Receive callbacks. They run from the comm system's dispatch in the main
// loop, so they only record into static state.
// -------------------------------------------------------------------------

static void Coop_RecvPos(int netId, int size, void *data, void *ctx) {
    CoopPosPacket *pkt = data;

    if (netId == sub_0203769C()) {
        return; // our own broadcast echoed back
    }
    // A new move: queue it for the avatar to replay. The first packet after
    // (re)connecting only tells us where the partner is.
    if (sCoop.partnerSeqValid && pkt->moveSeq != sCoop.partnerSeq) {
        Coop_PathPush(&sCoop.avatarPath, pkt->x, pkt->z, pkt->moveCmd);
    }
    sCoop.partnerSeq = pkt->moveSeq;
    sCoop.partnerSeqValid = TRUE;
    MI_CpuCopy8(data, &sCoop.partner, sizeof(CoopPosPacket));
    sCoop.partnerValid = TRUE;
}

static void Coop_RecvRequest(int netId, int size, void *data, void *ctx) {
    CoopRequest *req = data;

    if (netId == sub_0203769C()) {
        return;
    }
    switch (req->type) {
    case COOP_REQ_CANCEL:
        if (sCoop.inState != COOP_IN_IDLE && req->seq == sCoop.inReq.seq) {
            sCoop.inCancelled = TRUE;
        }
        break;
    case COOP_REQ_CARD:
        sCoop.cardRequested = TRUE;
        break;
    default:
        if (sCoop.inState != COOP_IN_IDLE) {
            sCoop.busyPending = TRUE;
            sCoop.busySeq = req->seq;
            sCoop.busyType = req->type;
            break;
        }
        sCoop.inReq = *req;
        sCoop.inState = COOP_IN_PENDING;
        sCoop.inTimer = 0;
        sCoop.inCancelled = FALSE;
        sCoop.inConfirmed = FALSE;
        sCoop.inPaid = FALSE;
        sCoop.inRefused = FALSE;
        break;
    }
}

static void Coop_RecvResponse(int netId, int size, void *data, void *ctx) {
    CoopResponse *resp = data;

    if (netId == sub_0203769C()) {
        return;
    }
    if (!resp->toRequester) {
        // About the request the partner sent us
        if (sCoop.inState == COOP_IN_IDLE || resp->seq != sCoop.inReq.seq) {
            return;
        }
        switch (resp->answer) {
        case COOP_ANS_CONFIRM:
            sCoop.inConfirmed = TRUE;
            break;
        case COOP_ANS_GOODS:
            sCoop.goodsReceived = TRUE;
            break;
        case COOP_ANS_PAY:
            sCoop.inPaid = TRUE;
            break;
        case COOP_ANS_REFUSE:
            sCoop.inRefused = TRUE;
            break;
        }
        return;
    }
    if (resp->seq != sCoop.outReq.seq) {
        return; // answer to a request we already gave up on
    }
    switch (resp->answer) {
    case COOP_ANS_PENDING:
        sCoop.outPending = TRUE;
        break;
    case COOP_ANS_OFFER:
        sCoop.offer = *resp;
        sCoop.outAnswer = COOP_RESULT_OFFER;
        break;
    case COOP_ANS_GOODS:
        sCoop.goodsReceived = TRUE;
        break;
    default:
        sCoop.outAnswer = resp->answer;
        break;
    }
}

static void Coop_RecvMon(int netId, int size, void *data, void *ctx) {
    if (netId == sub_0203769C()) {
        return;
    }
    sCoop.monReceived = TRUE;
}

// Same scheme as the Trainer Card: our own send is echoed back onto the send
// buffer, the partner's lands in a buffer we allocate.
static void *Coop_GetMonBuffer(int netId, void *ctx, int size) {
    if (netId == sub_0203769C() && sCoop.monSend != NULL) {
        return sCoop.monSend;
    }
    if (sCoop.monRecv == NULL) {
        sCoop.monRecv = Heap_Alloc(HEAP_ID_FIELD2, sizeof(Pokemon));
    }
    return sCoop.monRecv;
}

static void Coop_RecvCard(int netId, int size, void *data, void *ctx) {
    if (netId == sub_0203769C()) {
        return;
    }
    sCoop.cardReceived = TRUE;
}

// Large payloads are reassembled into a buffer we provide. Our own card is
// echoed back to us as well, so it lands on top of the send buffer (same
// bytes).
static void *Coop_GetCardBuffer(int netId, void *ctx, int size) {
    if (netId == sub_0203769C() && sCoop.cardSend != NULL) {
        return sCoop.cardSend;
    }
    if (sCoop.cardRecv == NULL) {
        sCoop.cardRecv = Heap_Alloc(HEAP_ID_FIELD2, COOP_CARD_SIZE);
    }
    return sCoop.cardRecv;
}

// -------------------------------------------------------------------------
// Session state
// -------------------------------------------------------------------------

static int Coop_PartnerNetId(void) {
    return 1 - sub_0203769C();
}

static u8 Coop_PreActivitySyncNo(void) {
    return COOP_SYNC_PRE_BASE + (sCoop.activityCount % COOP_SYNC_RANGE);
}

// activityCount is advanced when the activity starts, on both sides.
static u8 Coop_PostActivitySyncNo(void) {
    return COOP_SYNC_POST_BASE + (sCoop.activityCount % COOP_SYNC_RANGE);
}

static void Coop_RegisterCommands(void) {
    sub_0203410C(sCoopCmdTable, NELEMS(sCoopCmdTable), NULL);
}

static void Coop_SetState(CoopState state) {
    sCoop.state = state;
    sCoop.timer = 0;
}

static void Coop_FreeCardBuffers(void) {
    if (sCoop.cardSend != NULL) {
        Heap_Free(sCoop.cardSend);
        sCoop.cardSend = NULL;
    }
    if (sCoop.cardRecv != NULL) {
        Heap_Free(sCoop.cardRecv);
        sCoop.cardRecv = NULL;
    }
    if (sCoop.monSend != NULL) {
        Heap_Free(sCoop.monSend);
        sCoop.monSend = NULL;
    }
    if (sCoop.monRecv != NULL) {
        Heap_Free(sCoop.monRecv);
        sCoop.monRecv = NULL;
    }
}

// Forget any queued moves on both sides; they start over from wherever the
// players are.
static void Coop_ResetMoves(void) {
    sCoop.moveTracked = FALSE;
    sCoop.partnerSeqValid = FALSE;
    sCoop.avatarLastCmd = COOP_MOVE_NONE;
    sCoop.monPlace = TRUE;
    Coop_PathClear(&sCoop.avatarPath);
    Coop_PathClear(&sCoop.monPath);
}

static void Coop_ResetSession(void) {
    sCoop.partnerValid = FALSE;
    sCoop.profileSent = FALSE;
    sCoop.heartbeat = 0;
    sCoop.lostFrames = 0;
    Coop_ResetMoves();
    sCoop.outAnswer = COOP_RESULT_NONE;
    sCoop.outPending = FALSE;
    sCoop.inState = COOP_IN_IDLE;
    sCoop.inCancelled = FALSE;
    sCoop.inConfirmed = FALSE;
    sCoop.busyPending = FALSE;
    sCoop.cardRequested = FALSE;
    sCoop.cardReceived = FALSE;
    sCoop.inPaid = FALSE;
    sCoop.inRefused = FALSE;
    sCoop.goodsReceived = FALSE;
    sCoop.monReceived = FALSE;
    Coop_FreeCardBuffers();
    MI_CpuClear8(&sCoop.partner, sizeof(CoopPosPacket));
    MI_CpuClear8(&sCoop.lastSent, sizeof(CoopPosPacket));
}

CoopState Coop_GetState(void) {
    return sCoop.state;
}

BOOL Coop_IsConnected(void) {
    switch (sCoop.state) {
    case COOP_STATE_CONNECTED:
    case COOP_STATE_ACTIVITY_SYNC:
    case COOP_STATE_IN_ACTIVITY:
    case COOP_STATE_POST_ACTIVITY:
        return TRUE;
    default:
        return FALSE;
    }
}

BOOL Coop_BattleUsesLinkIds(void) {
    // A co-op link battle is set up in COOP_STATE_IN_ACTIVITY. The retail
    // link features (Colosseum, Union Room, Battle Tower) run with the co-op
    // session off. Wild and trainer battles while linked are neither.
    return sub_02037474() == TRUE && (sCoop.state == COOP_STATE_OFF || sCoop.state == COOP_STATE_IN_ACTIVITY);
}

u16 Coop_BattleAckNetId(void) {
    // A local battle queues every command under id 0 (ov12_02262240), but
    // while linked the live net id is 1 for the player who joined. Same
    // split as Coop_BattleUsesLinkIds: only a co-op link battle (or a retail
    // link feature, co-op off) acknowledges with the link's net id.
    if (sCoop.state != COOP_STATE_OFF && sCoop.state != COOP_STATE_IN_ACTIVITY) {
        return 0;
    }
    return sub_0203769C();
}

BOOL Coop_GetPartnerMapCell(s16 *x, s16 *y, u32 *gender) {
    PlayerProfile *profile;

    if (!Coop_IsConnected() || !sCoop.partnerValid) {
        return FALSE;
    }
    profile = sub_02034818(Coop_PartnerNetId());
    if (profile == NULL) {
        return FALSE;
    }
    *x = sCoop.partner.mapCellX;
    *y = sCoop.partner.mapCellY;
    *gender = PlayerProfile_GetTrainerGender(profile);
    return TRUE;
}

void Coop_OnActivityEnd(void) {
    if (sCoop.state != COOP_STATE_IN_ACTIVITY) {
        return;
    }
    // The battle/trade registered its own comm table (whose callbacks live
    // in its now-unloaded overlay); put ours back straight away.
    Coop_RegisterCommands();
    // Link battles enable fatal comm-error checking; in the overworld a
    // dropped link should just end the session.
    sub_020398D4(0, 0);
    sCoop.partnerValid = FALSE;
    Coop_ResetMoves();
    sCoop.lostFrames = 0;
    // Don't send anything until the partner is out of it too.
    sub_02037AC0(Coop_PostActivitySyncNo());
    Coop_SetState(COOP_STATE_POST_ACTIVITY);
}

// toRequester: TRUE when answering the partner's request, FALSE when telling
// the partner something about our own request.
static void Coop_SendResponseEx(u8 type, u8 seq, u8 answer, BOOL toRequester, const CoopDeal *goods) {
    CoopResponse resp;

    MI_CpuClear8(&resp, sizeof(CoopResponse));
    resp.type = type;
    resp.seq = seq;
    resp.answer = answer;
    resp.toRequester = toRequester;
    if (goods != NULL) {
        resp.itemId = goods->itemId;
        resp.qty = goods->qty;
        resp.species = goods->species;
        resp.level = goods->level;
    }
    sub_02037030(COOP_CMD_RESPONSE, &resp, sizeof(CoopResponse));
}

// Answer to the partner's request
static void Coop_SendResponse(u8 type, u8 seq, u8 answer) {
    Coop_SendResponseEx(type, seq, answer, TRUE, NULL);
}

// About our own request
static void Coop_SendOwnNotice(u8 answer) {
    Coop_SendResponseEx(sCoop.outReq.type, sCoop.outReq.seq, answer, FALSE, NULL);
}

// About the partner's request
static void Coop_SendInNotice(u8 answer, const CoopDeal *goods) {
    Coop_SendResponseEx(sCoop.inReq.type, sCoop.inReq.seq, answer, TRUE, goods);
}

static void Coop_SendCancel(void) {
    CoopRequest req = sCoop.outReq;

    req.type = COOP_REQ_CANCEL;
    sub_02037030(COOP_CMD_REQUEST, &req, sizeof(CoopRequest));
}

// -------------------------------------------------------------------------
// Local state
// -------------------------------------------------------------------------

// FieldSystem_Control runs every frame, including while the overworld is torn
// down for a warp, battle or menu. The player avatar and map objects are only
// valid while the field map is running.
static BOOL Coop_FieldIsLive(FieldSystem *fieldSystem) {
    return fieldSystem->runningFieldMap
        && !fieldSystem->processManager->isPaused
        && fieldSystem->mapObjectManager != NULL
        && fieldSystem->playerAvatar != NULL;
}

// Same cell computation as FieldSystem_InitPokegearArgs.
static void Coop_GetLocalMapCell(FieldSystem *fieldSystem, s16 *outX, s16 *outY) {
    u32 mapId = fieldSystem->location->mapId;
    s16 x, y;

    if (MapHeader_MapIsOnMainMatrix(mapId)) {
        *outX = PlayerAvatar_GetXCoord(fieldSystem->playerAvatar) / 32;
        *outY = PlayerAvatar_GetZCoord(fieldSystem->playerAvatar) / 32;
        return;
    }
    MapHeader_GetWorldMapCoords(mapId, &x, &y);
    if (x == 0 && y == 0) {
        Location *warp = LocalFieldData_GetSpecialSpawnWarpPtr(Save_LocalFieldData_Get(fieldSystem->saveData));
        *outX = warp->x / 32;
        *outY = warp->y / 32;
    } else {
        *outX = x;
        *outY = y;
    }
}

// Notices each move the player starts and records how it was made (walk,
// run, bike, ledge jump...) and where it ends, so the partner can replay it
// exactly. A move changes the coordinates as it starts, so this sees it the
// frame after.
static void Coop_TrackLocalMove(FieldSystem *fieldSystem) {
    PlayerAvatar *avatar = fieldSystem->playerAvatar;
    u32 mapId = fieldSystem->location->mapId;
    u16 x = PlayerAvatar_GetXCoord(avatar);
    u16 z = PlayerAvatar_GetZCoord(avatar);
    u32 cmd;
    int dir, tiles;

    // Outdoor maps share one coordinate space; anything else is a warp.
    if (!sCoop.moveTracked
        || (mapId != sCoop.moveMapId && !(MapHeader_MapIsOnMainMatrix(mapId) && MapHeader_MapIsOnMainMatrix(sCoop.moveMapId)))) {
        sCoop.moveTracked = TRUE;
        sCoop.moveSkip = 0;
        sCoop.moveCurX = sCoop.moveDestX = x;
        sCoop.moveCurZ = sCoop.moveDestZ = z;
        sCoop.moveMapId = mapId;
        return;
    }
    sCoop.moveMapId = mapId;
    if (x == sCoop.moveCurX && z == sCoop.moveCurZ) {
        return;
    }
    if (sCoop.moveSkip != 0) {
        // A jump moves the coordinates one tile at a time; its landing
        // tile was already sent.
        sCoop.moveSkip--;
        sCoop.moveCurX = x;
        sCoop.moveCurZ = z;
        return;
    }

    cmd = MapObject_GetMovementCommand(PlayerAvatar_GetMapObject(avatar));
    sCoop.moveDestX = x;
    sCoop.moveDestZ = z;
    if (Coop_GetMoveShape(cmd, &dir, &tiles)
        && x == sCoop.moveCurX + sDirDX[dir] && z == sCoop.moveCurZ + sDirDZ[dir]) {
        sCoop.moveDestX = x + sDirDX[dir] * (tiles - 1);
        sCoop.moveDestZ = z + sDirDZ[dir] * (tiles - 1);
        sCoop.moveSkip = tiles - 1;
    } else {
        cmd = COOP_MOVE_NONE; // not a plain move (warp tile, script...): the partner just heads here
    }
    sCoop.moveCurX = x;
    sCoop.moveCurZ = z;
    sCoop.moveCmd = cmd;
    sCoop.moveSeq++;
}

static void Coop_BuildLocalPacket(FieldSystem *fieldSystem, CoopPosPacket *pkt) {
    MI_CpuClear8(pkt, sizeof(CoopPosPacket));
    Coop_TrackLocalMove(fieldSystem);

    pkt->mapId = fieldSystem->location->mapId;
    pkt->x = sCoop.moveDestX;
    pkt->z = sCoop.moveDestZ;
    pkt->moveSeq = sCoop.moveSeq;
    pkt->moveCmd = sCoop.moveCmd;
    Coop_GetLocalMapCell(fieldSystem, &pkt->mapCellX, &pkt->mapCellY);
    pkt->facing = PlayerAvatar_GetFacingDirection(fieldSystem->playerAvatar);
    pkt->avatarState = PlayerAvatar_GetState(fieldSystem->playerAvatar);
    pkt->flags = COOP_FLAG_VALID;

    pkt->followSpecies = 0;
    pkt->followForm = 0;
    pkt->followInfo = 0;
    if (FollowMon_IsVisible(fieldSystem)) {
        pkt->followSpecies = fieldSystem->followMon.species;
        pkt->followForm = fieldSystem->followMon.form;
        pkt->followInfo = COOP_FOLLOW_VISIBLE | ((fieldSystem->followMon.gender & 3) << COOP_FOLLOW_GENDER_SHIFT);
        if (fieldSystem->followMon.shiny) {
            pkt->followInfo |= COOP_FOLLOW_SHINY;
        }
    }
}

static BOOL Coop_PacketsEqual(const CoopPosPacket *a, const CoopPosPacket *b) {
    return a->mapId == b->mapId && a->x == b->x && a->z == b->z
        && a->mapCellX == b->mapCellX && a->mapCellY == b->mapCellY
        && a->facing == b->facing && a->avatarState == b->avatarState && a->flags == b->flags
        && a->followSpecies == b->followSpecies && a->followForm == b->followForm && a->followInfo == b->followInfo
        && a->moveSeq == b->moveSeq;
}

static void Coop_SendLocalPacket(FieldSystem *fieldSystem) {
    CoopPosPacket pkt;

    Coop_BuildLocalPacket(fieldSystem, &pkt);
    if (++sCoop.heartbeat < COOP_HEARTBEAT_FRAMES && Coop_PacketsEqual(&pkt, &sCoop.lastSent)) {
        return;
    }
    if (sub_02037030(COOP_CMD_POS, &pkt, sizeof(CoopPosPacket))) {
        sCoop.lastSent = pkt;
        sCoop.heartbeat = 0;
    }
}

// -------------------------------------------------------------------------
// Partner avatar + follower (only call these while Coop_FieldIsLive)
// -------------------------------------------------------------------------

static int Coop_Abs(int v) {
    return v < 0 ? -v : v;
}

static LocalMapObject *Coop_FindObject(FieldSystem *fieldSystem, u32 id) {
    return MapObjectManager_GetFirstActiveObjectByID(fieldSystem->mapObjectManager, id);
}

static void Coop_RemoveObject(FieldSystem *fieldSystem, u32 id) {
    LocalMapObject *obj = Coop_FindObject(fieldSystem, id);
    if (obj != NULL) {
        MapObject_Remove(obj);
    }
}

static void Coop_RemovePartnerObjects(FieldSystem *fieldSystem) {
    Coop_RemoveObject(fieldSystem, COOP_PARTNER_OBJ_ID);
    Coop_RemoveObject(fieldSystem, COOP_PARTNER_MON_OBJ_ID);
}

// Same setup as the follow-mon / photo "special" objects: not tied to any
// map's event list. Warps delete it along with every other object and it
// is recreated once both players are on the same map again. Talking to it
// runs the co-op menu.
static LocalMapObject *Coop_CreateObject(FieldSystem *fieldSystem, u32 id, u32 x, u32 z, u32 facing, u32 sprite) {
    LocalMapObject *obj = MapObject_Create(fieldSystem->mapObjectManager, x, z, facing, sprite, 0, MAP_NOTHING);
    if (obj == NULL) {
        return NULL;
    }
    MapObject_SetID(obj, id);
    MapObject_SetType(obj, 0);
    MapObject_SetEventFlag(obj, 0);
    MapObject_SetScriptID(obj, std_coop_menu);
    MapObject_SetXRange(obj, -1);
    MapObject_SetYRange(obj, -1);
    MapObject_SetFlagsBits(obj, (MapObjectFlagBits)(MAPOBJECTFLAG_KEEP | MAPOBJECTFLAG_UNK13));
    MapObject_ClearFlagsBits(obj, (MapObjectFlagBits)(MAPOBJECTFLAG_UNK7 | MAPOBJECTFLAG_UNK8));
    MapObject_SetFlag29(obj, TRUE);
    return obj;
}

// The tile behind obj, where a follower belongs.
static void Coop_GetTileBehind(LocalMapObject *obj, u32 *x, u32 *z) {
    u32 facing = MapObject_GetFacingDirection(obj) & 3;

    *x = MapObject_GetXCoord(obj) - sDirDX[facing];
    *z = MapObject_GetZCoord(obj) - sDirDZ[facing];
}

// Puts an existing object straight onto a tile. Re-creating it instead would
// reload its graphics from the card, which stalls the game for a moment.
static void Coop_SnapObject(LocalMapObject *obj, u32 x, u32 z, u32 facing) {
    MapObject_SetPositionFromXYZAndDirection(obj, x, MapObject_GetYCoord(obj), z, facing);
    sub_02061070(obj); // height of the new tile
}

static BOOL Coop_PartnerInSameArea(FieldSystem *fieldSystem) {
    u32 mapId = fieldSystem->location->mapId;

    if (sCoop.partner.mapId == mapId) {
        return TRUE;
    }
    // Outdoor maps share one coordinate space on the main matrix.
    return MapHeader_MapIsOnMainMatrix(mapId) && MapHeader_MapIsOnMainMatrix(sCoop.partner.mapId);
}

static int Coop_DirToward(int dx, int dz) {
    if (Coop_Abs(dx) >= Coop_Abs(dz)) {
        return dx > 0 ? DIR_EAST : DIR_WEST;
    }
    return dz > 0 ? DIR_SOUTH : DIR_NORTH;
}

// Pokémon have no running or cycling animation. The real follower walks
// fast when the player runs; do the same.
static u32 Coop_MonWalk(u32 group) {
    switch (group) {
    case COOP_MOVE_WALK_SLOWER:
    case COOP_MOVE_WALK_SLOW:
    case COOP_MOVE_WALK_NORMAL:
        return group;
    case COOP_MOVE_WALK_SLIGHTLY_FAST:
    case COOP_MOVE_WALK_EVER_SO_SLIGHTLY_FAST:
        return COOP_MOVE_WALK_NORMAL;
    default:
        return COOP_MOVE_WALK_FAST;
    }
}

// The next move for obj along its queue. When obj is where the queued move
// starts, that's the partner's own move (same speed and animation, same ledge
// jumps); otherwise it's a step toward where the move ends. Plays faster
// while moves pile up.
// Returns the MOVEMENT_* to play, COOP_MOVE_NONE if the queue is done, or
// COOP_MOVE_SNAP if obj is too far behind to walk.
static u32 Coop_NextMove(LocalMapObject *obj, CoopPath *path, BOOL isMon) {
    int x = MapObject_GetXCoord(obj);
    int z = MapObject_GetZCoord(obj);
    CoopStep *step;
    int dx, dz, dist, dir, tiles, behind;
    u32 group;

    while ((step = Coop_PathFront(path)) != NULL && step->x == x && step->z == z) {
        Coop_PathPop(path); // already there
    }
    if (step == NULL) {
        return COOP_MOVE_NONE;
    }
    dx = step->x - x;
    dz = step->z - z;
    dist = Coop_Abs(dx) + Coop_Abs(dz);
    if (dist > COOP_SNAP_RANGE || path->count == COOP_PATH_LEN) {
        return COOP_MOVE_SNAP;
    }

    if (Coop_GetMoveShape(step->cmd, &dir, &tiles) && dx == sDirDX[dir] * tiles && dz == sDirDZ[dir] * tiles) {
        Coop_PathPop(path);
        if (Coop_IsJump(step->cmd)) {
            return step->cmd;
        }
        group = step->cmd & ~3;
        behind = path->count;
    } else {
        // A move we don't replay, or obj isn't where it starts.
        group = COOP_MOVE_WALK_NORMAL;
        if (Coop_GetMoveShape(step->cmd, &dir, &tiles) && !Coop_IsJump(step->cmd)) {
            group = step->cmd & ~3;
        }
        dir = Coop_DirToward(dx, dz);
        if (dist > 1) {
            group = Coop_FasterWalk(group);
        }
        behind = path->count - 1;
    }
    if (behind >= COOP_CATCHUP_MOVES) {
        group = Coop_FasterWalk(group);
    }
    if (behind >= COOP_CATCHUP_MOVES * 2) {
        group = Coop_FasterWalk(group);
    }
    if (isMon) {
        group = Coop_MonWalk(group);
    }
    return group + dir;
}

// The follower's move onto the tile the avatar is leaving: the way the avatar
// got onto it (so it takes the same ledges), at the avatar's current pace.
static u8 Coop_MonStepCmd(u32 arrived, u32 now) {
    int dir, nowDir, tiles;

    if (!Coop_GetMoveShape(arrived, &dir, &tiles) || Coop_IsJump(arrived)
        || !Coop_GetMoveShape(now, &nowDir, &tiles) || Coop_IsJump(now)) {
        return arrived;
    }
    return (now & ~3) + dir;
}

// Returns the avatar, or NULL if it isn't shown.
static LocalMapObject *Coop_UpdateAvatar(FieldSystem *fieldSystem) {
    LocalMapObject *obj = Coop_FindObject(fieldSystem, COOP_PARTNER_OBJ_ID);
    PlayerProfile *profile = sub_02034818(Coop_PartnerNetId());
    u32 sprite, cmd, x, z;
    int dx, dz, range;

    if (!sCoop.partnerValid || profile == NULL || !Coop_PartnerInSameArea(fieldSystem)) {
        if (obj != NULL) {
            MapObject_Remove(obj);
        }
        Coop_PathClear(&sCoop.avatarPath);
        return NULL;
    }

    dx = (int)sCoop.partner.x - (int)PlayerAvatar_GetXCoord(fieldSystem->playerAvatar);
    dz = (int)sCoop.partner.z - (int)PlayerAvatar_GetZCoord(fieldSystem->playerAvatar);
    range = obj != NULL ? COOP_DESPAWN_RANGE : COOP_SPAWN_RANGE;
    if (Coop_Abs(dx) > range || Coop_Abs(dz) > range) {
        if (obj != NULL) {
            MapObject_Remove(obj);
        }
        Coop_PathClear(&sCoop.avatarPath);
        return NULL;
    }

    sprite = PlayerAvatar_GetSpriteByStateAndGender(sCoop.partner.avatarState, PlayerProfile_GetTrainerGender(profile));

    if (obj != NULL && MapObject_GetSpriteID(obj) != sprite) {
        // Partner got on/off a bike or started surfing: rebuild with the new sprite.
        MapObject_Remove(obj);
        obj = NULL;
    }
    if (obj == NULL) {
        Coop_PathClear(&sCoop.avatarPath);
        sCoop.avatarLastCmd = COOP_MOVE_NONE;
        sCoop.monPlace = TRUE;
        return Coop_CreateObject(fieldSystem, COOP_PARTNER_OBJ_ID, sCoop.partner.x, sCoop.partner.z, sCoop.partner.facing, sprite);
    }

    if (!MapObject_AreBitsSetForMovementScriptInit(obj)) {
        return obj; // still playing the previous move
    }
    MapObject_ClearHeldMovementIfActive(obj);

    x = MapObject_GetXCoord(obj);
    z = MapObject_GetZCoord(obj);
    if (Coop_PathFront(&sCoop.avatarPath) == NULL && (x != sCoop.partner.x || z != sCoop.partner.z)) {
        // Nothing queued, yet not where the partner is (they warped within
        // the map, or a move was missed): head there.
        Coop_PathPush(&sCoop.avatarPath, sCoop.partner.x, sCoop.partner.z, COOP_MOVE_NONE);
    }
    cmd = Coop_NextMove(obj, &sCoop.avatarPath, FALSE);
    if (cmd == COOP_MOVE_NONE) {
        if (MapObject_GetFacingDirection(obj) != sCoop.partner.facing) {
            MapObject_SetHeldMovement(obj, MOVEMENT_FACE_UP + sCoop.partner.facing);
        }
        return obj;
    }
    if (cmd == COOP_MOVE_SNAP) {
        Coop_PathClear(&sCoop.avatarPath);
        Coop_SnapObject(obj, sCoop.partner.x, sCoop.partner.z, sCoop.partner.facing);
        sCoop.avatarLastCmd = COOP_MOVE_NONE;
        sCoop.monPlace = TRUE;
        return obj;
    }
    Coop_PathPush(&sCoop.monPath, x, z, Coop_MonStepCmd(sCoop.avatarLastCmd, cmd));
    sCoop.avatarLastCmd = cmd;
    MapObject_SetHeldMovement(obj, cmd);
    return obj;
}

static void Coop_UpdateFollower(FieldSystem *fieldSystem, LocalMapObject *avatar) {
    LocalMapObject *obj = Coop_FindObject(fieldSystem, COOP_PARTNER_MON_OBJ_ID);
    u32 species = sCoop.partner.followSpecies;
    u8 info = sCoop.partner.followInfo;
    u32 sprite, cmd, x, z;
    CoopStep *step;

    if (avatar == NULL || species == 0 || !(info & COOP_FOLLOW_VISIBLE)) {
        if (obj != NULL) {
            MapObject_Remove(obj);
        }
        Coop_PathClear(&sCoop.monPath);
        return;
    }

    sprite = FollowMon_GetSpriteID(species, sCoop.partner.followForm, (info >> COOP_FOLLOW_GENDER_SHIFT) & 3);
    if (obj != NULL && MapObject_GetSpriteID(obj) != sprite) {
        // Partner's lead Pokemon changed.
        MapObject_Remove(obj);
        obj = NULL;
    }
    if (obj == NULL) {
        Coop_GetTileBehind(avatar, &x, &z);
        obj = Coop_CreateObject(fieldSystem, COOP_PARTNER_MON_OBJ_ID, x, z, MapObject_GetFacingDirection(avatar), sprite);
        if (obj != NULL) {
            // Same parameters FollowMon_CreateMapObject sets (minus the hidden
            // flag it starts with), so the sprite gets the right form/size/shiny.
            MapObject_SetParam(obj, 0, 2);
            FollowMon_SetObjectParams(obj, species, sCoop.partner.followForm, (info & COOP_FOLLOW_SHINY) != 0);
        }
        sCoop.monPlace = FALSE;
        Coop_PathClear(&sCoop.monPath);
        return;
    }

    if (!MapObject_AreBitsSetForMovementScriptInit(obj)) {
        return;
    }
    MapObject_ClearHeldMovementIfActive(obj);
    if (sCoop.monPlace) {
        // The avatar was just placed or snapped: go straight behind it.
        Coop_GetTileBehind(avatar, &x, &z);
        Coop_SnapObject(obj, x, z, MapObject_GetFacingDirection(avatar));
        sCoop.monPlace = FALSE;
        Coop_PathClear(&sCoop.monPath);
        return;
    }
    cmd = Coop_NextMove(obj, &sCoop.monPath, TRUE);
    if (cmd == COOP_MOVE_SNAP) {
        step = Coop_PathBack(&sCoop.monPath);
        Coop_SnapObject(obj, step->x, step->z, MapObject_GetFacingDirection(avatar));
        Coop_PathClear(&sCoop.monPath);
    } else if (cmd != COOP_MOVE_NONE) {
        MapObject_SetHeldMovement(obj, cmd);
    }
}

static void Coop_UpdatePartnerObjects(FieldSystem *fieldSystem) {
    LocalMapObject *avatar = Coop_UpdateAvatar(fieldSystem);
    Coop_UpdateFollower(fieldSystem, avatar);
}

// -------------------------------------------------------------------------
// Session control
// -------------------------------------------------------------------------

static BOOL Coop_MapTypeAllowed(FieldSystem *fieldSystem) {
    // Union Room, Colosseum and Battle Tower run their own comm sessions.
    return fieldSystem->mapLoadType == MAP_LOAD_TYPE_OVERWORLD || fieldSystem->mapLoadType == MAP_LOAD_TYPE_SAFARI;
}

static void Coop_StartHost(FieldSystem *fieldSystem) {
    if (sub_02037474()) {
        PlaySE(SEQ_SE_DP_BEEP); // another link feature already owns the wireless
        return;
    }
    Coop_ResetSession();
    sCoop.isHost = TRUE;
    sub_02037D8C(fieldSystem->saveData, COMM_TYPE_SINGLE_BATTLE, 0, (s32)fieldSystem->linkBattleRuleset);
    // The comm manager (and its empty command table) now exists; register
    // ours before any peer can send us a co-op command.
    Coop_RegisterCommands();
    Coop_SetState(COOP_STATE_HOST_STARTING);
    PlaySE(SEQ_SE_DP_PC_LOGIN);
}

static void Coop_StartJoin(FieldSystem *fieldSystem) {
    if (sub_02037474()) {
        PlaySE(SEQ_SE_DP_BEEP);
        return;
    }
    Coop_ResetSession();
    sCoop.isHost = FALSE;
    sub_02037DD4(fieldSystem->saveData, COMM_TYPE_SINGLE_BATTLE, 0, (s32)fieldSystem->linkBattleRuleset);
    Coop_RegisterCommands();
    Coop_SetState(COOP_STATE_JOIN_STARTING);
    PlaySE(SEQ_SE_DP_PC_LOGIN);
}

// Network side only; the partner objects are cleaned up by Coop_FieldUpdate
// the next time the field is live.
static void Coop_Disconnect(void) {
    Coop_ResetSession();
    if (sub_02037D78()) {
        sub_02038094();
        Coop_SetState(COOP_STATE_DISCONNECTING);
    } else {
        Coop_SetState(COOP_STATE_OFF);
    }
    PlaySE(SEQ_SE_DP_PC_LOGOFF);
}

// Never use sub_02037700 here: when the link layer has flagged an error it
// also switches on fatal error mode (sub_020398D4(1, 1)), which turns any
// hiccup into the unrecoverable "communication error" screen.
static BOOL Coop_LinkLost(void) {
    return sub_02037454() < 2 || sub_0203569C();
}

static BOOL Coop_PartnerPresent(void) {
    return sub_02037454() >= 2 && sub_02034818(Coop_PartnerNetId()) != NULL;
}

static void Coop_OnConnected(void) {
    sub_020398D4(0, 0); // a dropped link ends the session instead of forcing a reset
    sCoop.lostFrames = 0;
    Coop_SetState(COOP_STATE_CONNECTED);
    PlaySE(SEQ_SE_DP_SAVE);
}

static void Coop_UpdateConnecting(void) {
    sCoop.timer++;

    switch (sCoop.state) {
    case COOP_STATE_HOST_STARTING:
        if (sub_020373B4(sub_0203769C())) {
            sub_02034434(); // publish our trainer profile
            Coop_SetState(COOP_STATE_HOST_WAITING);
        }
        break;
    case COOP_STATE_HOST_WAITING:
        if (Coop_PartnerPresent()) {
            Coop_OnConnected();
        }
        // A host waits indefinitely; L+R+B cancels.
        break;
    case COOP_STATE_JOIN_STARTING:
        if (sub_02034DB8()) {
            Coop_SetState(COOP_STATE_JOIN_SCANNING);
        }
        break;
    case COOP_STATE_JOIN_SCANNING:
        if (sub_0203511C() > 0) {
            sub_02037E1C(sub_02035150(0));
            Coop_SetState(COOP_STATE_JOIN_CONNECTING);
        } else if (sCoop.timer > COOP_SCAN_TIMEOUT_FRAMES) {
            Coop_Disconnect();
        }
        break;
    case COOP_STATE_JOIN_CONNECTING:
        if (sub_0203569C() || sCoop.timer > COOP_LINK_TIMEOUT_FRAMES) {
            Coop_Disconnect();
        } else if (sub_02037EC8()) {
            if (!sCoop.profileSent) {
                sub_02034434(); // publish our trainer profile
                sCoop.profileSent = TRUE;
            }
            if (Coop_PartnerPresent()) {
                Coop_OnConnected();
            }
        }
        break;
    case COOP_STATE_DISCONNECTING:
        if (!sub_02037D78()) {
            Coop_SetState(COOP_STATE_OFF);
        }
        break;
    default:
        break;
    }
}

static void Coop_UpdatePostActivity(void) {
    sCoop.timer++;
    if (sub_02037B38(Coop_PostActivitySyncNo())) {
        Coop_SetState(COOP_STATE_CONNECTED);
        return;
    }
    if (sCoop.timer > COOP_POST_ACTIVITY_FRAMES) {
        Coop_Disconnect();
    }
}

// Requests from the partner, and our automatic replies. Returns TRUE if it
// started the prompt script this frame (so no other field input may run).
static BOOL Coop_UpdateIncoming(FieldSystem *fieldSystem, BOOL live, BOOL movementAllowed) {
    if (sCoop.busyPending) {
        Coop_SendResponse(sCoop.busyType, sCoop.busySeq, COOP_RESULT_BUSY);
        sCoop.busyPending = FALSE;
    }

    if (sCoop.cardRequested && sCoop.state == COOP_STATE_CONNECTED) {
        sCoop.cardRequested = FALSE;
        if (sCoop.cardSend == NULL) {
            sCoop.cardSend = sub_020691C4(HEAP_ID_FIELD2);
        }
        sub_02068FC8(1, 1, 0, 255, fieldSystem, sCoop.cardSend);
        sub_02036FD8(COOP_CMD_CARD, sCoop.cardSend, COOP_CARD_SIZE);
    }

    if (sCoop.inState != COOP_IN_PENDING) {
        return FALSE;
    }
    if (sCoop.inCancelled) {
        sCoop.inState = COOP_IN_IDLE;
        return FALSE;
    }
    if (sCoop.state == COOP_STATE_CONNECTED && live && movementAllowed && !FieldSystem_TaskIsRunning(fieldSystem)) {
        sCoop.inState = COOP_IN_PROMPTING;
        StartMapSceneScript(fieldSystem, std_coop_incoming, NULL);
        return TRUE;
    }
    if (++sCoop.inTimer > COOP_INCOMING_WAIT || sCoop.state != COOP_STATE_CONNECTED) {
        Coop_SendResponse(sCoop.inReq.type, sCoop.inReq.seq, COOP_RESULT_BUSY);
        sCoop.inState = COOP_IN_IDLE;
    }
    return FALSE;
}

static BOOL Coop_HandleInput(FieldSystem *fieldSystem, u16 newKeys, u16 heldKeys) {
    if ((heldKeys & COOP_KEYS_COMBO) != COOP_KEYS_COMBO) {
        return FALSE;
    }
    if (newKeys & PAD_BUTTON_X) {
        if (sCoop.state == COOP_STATE_OFF) {
            Coop_StartHost(fieldSystem);
        }
        return TRUE;
    }
    if (newKeys & PAD_BUTTON_Y) {
        if (sCoop.state == COOP_STATE_OFF) {
            Coop_StartJoin(fieldSystem);
        }
        return TRUE;
    }
    if (newKeys & PAD_BUTTON_B) {
        if (sCoop.state != COOP_STATE_OFF && sCoop.state != COOP_STATE_DISCONNECTING && sCoop.state != COOP_STATE_IN_ACTIVITY) {
            Coop_Disconnect();
        }
        return TRUE;
    }
    return FALSE;
}

// Returns TRUE if it consumed this frame's input.
static BOOL Coop_FieldUpdate(FieldSystem *fieldSystem, u16 newKeys, u16 heldKeys, BOOL movementAllowed) {
    BOOL live = Coop_FieldIsLive(fieldSystem);
    BOOL consumed = FALSE;

    if (sCoop.state == COOP_STATE_OFF) {
        if (live) {
            // A save made while linked stores the partner objects; clean them up.
            Coop_RemovePartnerObjects(fieldSystem);
            if (movementAllowed && Coop_MapTypeAllowed(fieldSystem)) {
                consumed = Coop_HandleInput(fieldSystem, newKeys, heldKeys);
            }
        }
        return consumed;
    }

    // Host: pass every member's trainer profile on to the others, exactly
    // like the Colosseum does, or the guest never learns who we are.
    if (sCoop.isHost && sub_02037474() && sCoop.state != COOP_STATE_HOST_STARTING && sCoop.state != COOP_STATE_DISCONNECTING) {
        sub_02034638();
    }

    if (sCoop.state == COOP_STATE_IN_ACTIVITY) {
        return FALSE; // the link battle / trade owns the connection
    }

    if (sCoop.state != COOP_STATE_DISCONNECTING && !Coop_MapTypeAllowed(fieldSystem)) {
        Coop_Disconnect();
        return FALSE;
    }

    if (live && movementAllowed && !FieldSystem_TaskIsRunning(fieldSystem)) {
        consumed = Coop_HandleInput(fieldSystem, newKeys, heldKeys);
    }

    switch (sCoop.state) {
    case COOP_STATE_CONNECTED:
    case COOP_STATE_ACTIVITY_SYNC:
    case COOP_STATE_POST_ACTIVITY:
        if (Coop_LinkLost()) {
            if (++sCoop.lostFrames > COOP_LOST_FRAMES) {
                Coop_Disconnect();
                return consumed;
            }
        } else {
            sCoop.lostFrames = 0;
        }
        break;
    default:
        break;
    }

    switch (sCoop.state) {
    case COOP_STATE_CONNECTED:
        if (live) {
            Coop_SendLocalPacket(fieldSystem);
            Coop_UpdatePartnerObjects(fieldSystem);
        }
        if (Coop_UpdateIncoming(fieldSystem, live, movementAllowed)) {
            consumed = TRUE;
        }
        break;
    case COOP_STATE_ACTIVITY_SYNC:
        // No position packets: the partner may switch to the battle/trade
        // comm table at any moment now. The script is driving this state.
        Coop_UpdateIncoming(fieldSystem, live, movementAllowed);
        break;
    case COOP_STATE_POST_ACTIVITY:
        Coop_UpdatePostActivity();
        if (live) {
            consumed = TRUE;
        }
        break;
    default:
        Coop_UpdateConnecting();
        break;
    }

    if (!live) {
        consumed = FALSE;
    }
    return consumed;
}

BOOL Coop_FieldControl(FieldSystem *fieldSystem) {
    BOOL movementAllowed = FieldSystem_IsPlayerMovementAllowed(fieldSystem);

    if (Coop_FieldUpdate(fieldSystem, gSystem.newKeys, gSystem.heldKeys, movementAllowed)) {
        return FALSE; // the rest of FieldSystem_Control must not act on this input
    }
    return movementAllowed;
}

// -------------------------------------------------------------------------
// Link battle started from the overworld. Mirrors Task_StartEncounter's
// leave/restore of the overworld around the Colosseum's link battle
// (Task_02050960 in encounter.c). It lives here and not in encounter.c so
// that file keeps its retail size (see main.lsf), which is also why it uses
// its own copies of encounter.c's static helpers.
// -------------------------------------------------------------------------

// Task_StartBattle
static BOOL Task_CoopStartBattle(TaskManager *taskManager) {
    FieldSystem *fieldSystem = TaskManager_GetFieldSystem(taskManager);
    BattleSetup *setup = TaskManager_GetEnvironment(taskManager);
    u32 *state = TaskManager_GetStatePtr(taskManager);

    switch (*state) {
    case 0:
        Battle_LaunchApp(fieldSystem, setup);
        Field_SetEnvironmentSoundState_None_Unk2();
        (*state)++;
        break;
    case 1:
        if (!FieldSystem_ApplicationIsRunning(fieldSystem)) {
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// sub_0205087C: the link battle record on the Trainer Card
static void Coop_RecordLinkBattleResult(s32 winFlag, FieldSystem *fieldSystem) {
    switch (winFlag & 0xF) {
    case 1:
    case 6:
        sub_02034AC0(fieldSystem->saveData, 1);
        break;
    case 2:
    case 5:
        sub_02034AC0(fieldSystem->saveData, -1);
        break;
    }
}

static BOOL Task_CoopLinkBattle(TaskManager *taskManager) {
    FieldSystem *fieldSystem = TaskManager_GetFieldSystem(taskManager);
    Encounter *encounter = TaskManager_GetEnvironment(taskManager);
    u32 *state = TaskManager_GetStatePtr(taskManager);

    switch (*state) {
    case 0:
        MapObjectManager_PauseAllMovement(fieldSystem->mapObjectManager);
        sub_02055218(taskManager, encounter->effect, encounter->bgm);
        (*state)++;
        break;
    case 1:
        CallTask_LeaveOverworld(taskManager);
        (*state)++;
        break;
    case 2:
        TaskManager_Call(taskManager, Task_CoopStartBattle, encounter->setup);
        (*state)++;
        break;
    case 3:
        // Swap the battle's comm command table back to the co-op one before
        // anything else runs; its callbacks lived in the battle overlay.
        Coop_OnActivityEnd();
        Coop_RecordLinkBattleResult(encounter->setup->winFlag, fieldSystem);
        sub_02052444(encounter->setup, fieldSystem);
        GameStats_AddScore(Save_GameStats_Get(fieldSystem->saveData), SCORE_EVENT_LINK_BATTLE);
        CallTask_RestoreOverworld(taskManager);
        (*state)++;
        break;
    case 4:
        MapObjectManager_UnpauseAllMovement(fieldSystem->mapObjectManager);
        CallTask_FadeFromBlack(taskManager);
        (*state)++;
        break;
    case 5:
        BattleSetup_Delete(encounter->setup);
        Heap_Free(encounter);
        sub_0202FC24();
        return TRUE;
    }
    return FALSE;
}

// Runs as a subtask of the co-op script (both players call it after syncing).
static void CallTask_CoopLinkBattle(TaskManager *taskManager) {
    FieldSystem *fieldSystem = TaskManager_GetFieldSystem(taskManager);
    BattleSetup *setup;
    Encounter *encounter;
    u32 result;

    setup = BattleSetup_New(HEAP_ID_FIELD2, BATTLE_TYPE_LINK | BATTLE_TYPE_TRAINER);
    BattleSetup_InitForFixedLevelFacility(setup, fieldSystem, 0); // 0 = keep real levels
    sub_0202FBF0(fieldSystem->saveData, HEAP_ID_FIELD2, &result);
    setup->unk1B2 = 0; // single battle, no ruleset

    // Encounter_New
    encounter = Heap_AllocAtEnd(HEAP_ID_FIELD2, sizeof(Encounter));
    encounter->winFlag = NULL;
    encounter->effect = BattleSetup_GetWildTransitionEffect(setup);
    encounter->bgm = BattleSetup_GetWildBattleMusic(setup);
    encounter->unkC = 0;
    encounter->setup = setup;
    TaskManager_Call(taskManager, Task_CoopLinkBattle, encounter);
}

// -------------------------------------------------------------------------
// Script command: Coop <sub>, <arg>, <retvar>
// ctx->data[0] = retvar id, data[1] = frame counter for the native waits.
// -------------------------------------------------------------------------

static u16 *Coop_RetVar(ScriptContext *ctx) {
    return GetVarPointer(ctx->fieldSystem, ctx->data[0]);
}

static MessageFormat *Coop_MsgFmt(ScriptContext *ctx) {
    MessageFormat **msgFmt = FieldSysGetAttrAddr(ctx->fieldSystem, SCRIPTENV_MESSAGE_FORMAT);
    return *msgFmt;
}

static void Coop_BufferNumber(MessageFormat *msgFmt, u32 slot, u32 num) {
    BufferIntegerAsString(msgFmt, slot, num, CountDigits(num), PRINTING_MODE_LEFT_ALIGN, TRUE);
}

// Slots: see constants/coop.h
static void Coop_BufferItemAndQty(MessageFormat *msgFmt, u16 itemId, u16 qty) {
    if (qty > 1) {
        BufferItemNamePlural(msgFmt, 1, itemId);
    } else {
        BufferItemName(msgFmt, 1, itemId);
    }
    Coop_BufferNumber(msgFmt, 2, qty);
}

static void Coop_BufferAmount(MessageFormat *msgFmt, u32 amount) {
    Coop_BufferNumber(msgFmt, 3, amount);
}

static void Coop_BufferMon(MessageFormat *msgFmt, u16 species, u8 level) {
    BufferSpeciesName(msgFmt, 4, species);
    Coop_BufferNumber(msgFmt, 5, level);
}

static void Coop_BufferDeal(MessageFormat *msgFmt, const CoopDeal *deal) {
    if (deal->kind == COOP_DEAL_MON) {
        Coop_BufferMon(msgFmt, deal->species, deal->level);
    } else {
        Coop_BufferItemAndQty(msgFmt, deal->itemId, deal->qty);
    }
    Coop_BufferAmount(msgFmt, deal->price);
}

static PlayerProfile *Coop_Profile(FieldSystem *fieldSystem) {
    return Save_PlayerData_GetProfile(fieldSystem->saveData);
}

static u32 Coop_Money(FieldSystem *fieldSystem) {
    return PlayerProfile_GetMoney(Coop_Profile(fieldSystem));
}

// -------------------------------------------------------------------------
// Sell / Buy deals
//
// Order of events, whoever started the deal:
//   buyer takes the money out and sends PAY (for a Sell request that's the
//   ACCEPT answer) -> seller takes the goods out, adds the money and sends
//   the goods -> buyer adds the goods.
// Nothing is ever created without being removed on the other side first.
// -------------------------------------------------------------------------

static BOOL Coop_MonCanBattle(Pokemon *mon) {
    return !GetMonData(mon, MON_DATA_IS_EGG, NULL) && GetMonData(mon, MON_DATA_HP, NULL) != 0;
}

static u8 Coop_CheckSellableMon(FieldSystem *fieldSystem, int slot) {
    Party *party = SaveArray_Party_Get(fieldSystem->saveData);
    int count = Party_GetCount(party);
    int i, others = 0;
    Pokemon *mon;

    if (slot < 0 || slot >= count) {
        return COOP_MON_NONE;
    }
    mon = Party_GetMonByIndex(party, slot);
    if (ItemIdIsMail(GetMonData(mon, MON_DATA_HELD_ITEM, NULL))) {
        return COOP_MON_MAIL;
    }
    for (i = 0; i < count; i++) {
        if (i != slot && Coop_MonCanBattle(Party_GetMonByIndex(party, i))) {
            others++;
        }
    }
    if (others == 0) {
        return COOP_MON_LAST;
    }
    return COOP_MON_OK;
}

static BOOL Coop_HasRoomForGoods(FieldSystem *fieldSystem, const CoopDeal *deal) {
    if (deal->kind == COOP_DEAL_MON) {
        return Party_GetCount(SaveArray_Party_Get(fieldSystem->saveData)) < PARTY_SIZE
            || PCStorage_CountEmptySpotsInAllBoxes(SaveArray_PCStorage_Get(fieldSystem->saveData)) > 0;
    }
    return Bag_HasSpaceForItem(Save_Bag_Get(fieldSystem->saveData), deal->itemId, deal->qty, HEAP_ID_FIELD2);
}

// Seller side, once the buyer has paid. aboutOwnRequest: we started the deal
// (a Sell request) rather than answering the partner's Buy request.
static BOOL Coop_DeliverGoods(FieldSystem *fieldSystem, BOOL aboutOwnRequest) {
    CoopDeal *deal = &sCoop.deal;

    if (deal->kind == COOP_DEAL_MON) {
        Party *party = SaveArray_Party_Get(fieldSystem->saveData);
        if (deal->slot >= Party_GetCount(party)) {
            return FALSE;
        }
        if (sCoop.monSend == NULL) {
            sCoop.monSend = Heap_Alloc(HEAP_ID_FIELD2, sizeof(Pokemon));
        }
        MI_CpuCopy8(Party_GetMonByIndex(party, deal->slot), sCoop.monSend, sizeof(Pokemon));
        if (!sub_02036FD8(COOP_CMD_MON, sCoop.monSend, sizeof(Pokemon))) {
            return FALSE;
        }
        Party_RemoveMon(party, deal->slot);
    } else {
        Bag *bag = Save_Bag_Get(fieldSystem->saveData);
        if (Bag_GetQuantity(bag, deal->itemId, HEAP_ID_FIELD2) < deal->qty) {
            return FALSE;
        }
        Bag_TakeItem(bag, deal->itemId, deal->qty, HEAP_ID_FIELD2);
        if (aboutOwnRequest) {
            Coop_SendOwnNotice(COOP_ANS_GOODS);
        } else {
            Coop_SendInNotice(COOP_ANS_GOODS, NULL);
        }
    }
    PlayerProfile_AddMoney(Coop_Profile(fieldSystem), deal->price);
    return TRUE;
}

// Buyer side, once the goods arrived.
static void Coop_ReceiveGoods(FieldSystem *fieldSystem) {
    CoopDeal *deal = &sCoop.deal;

    deal->toPC = FALSE;
    if (deal->kind == COOP_DEAL_MON) {
        Party *party = SaveArray_Party_Get(fieldSystem->saveData);
        Pokemon *mon = sCoop.monRecv;
        if (Party_GetCount(party) < PARTY_SIZE) {
            Party_AddMon(party, mon);
        } else {
            PCStorage_PlaceMonInFirstEmptySlotInAnyBox(SaveArray_PCStorage_Get(fieldSystem->saveData), Mon_GetBoxMon(mon));
            deal->toPC = TRUE;
        }
        if (!GetMonData(mon, MON_DATA_IS_EGG, NULL)) {
            Pokedex_SetMonCaughtFlag(Save_Pokedex_Get(fieldSystem->saveData), mon);
        }
        Heap_Free(sCoop.monRecv);
        sCoop.monRecv = NULL;
    } else {
        Bag_AddItem(Save_Bag_Get(fieldSystem->saveData), deal->itemId, deal->qty, HEAP_ID_FIELD2);
    }
}

// Buyer: wait for the goods. data[2]: TRUE if this is the partner's Sell
// request (we're answering it), FALSE if it's our Buy request.
static BOOL Coop_Native_WaitGoods(ScriptContext *ctx) {
    u16 *ret = Coop_RetVar(ctx);
    BOOL incoming = ctx->data[2];
    BOOL arrived;

    ctx->data[1]++;
    if (sCoop.deal.kind == COOP_DEAL_MON) {
        arrived = sCoop.monReceived && sCoop.monRecv != NULL;
    } else {
        arrived = sCoop.goodsReceived;
    }
    if (arrived) {
        Coop_ReceiveGoods(ctx->fieldSystem);
        if (incoming) {
            sCoop.inState = COOP_IN_IDLE;
        }
        *ret = COOP_RESULT_ACCEPT;
        return TRUE;
    }
    if (incoming && sCoop.inCancelled) {
        // The seller gave up waiting before our payment reached them, so
        // they never handed anything over: give the money back.
        PlayerProfile_AddMoney(Coop_Profile(ctx->fieldSystem), sCoop.deal.price);
        sCoop.inState = COOP_IN_IDLE;
        *ret = COOP_RESULT_CANCELLED;
        return TRUE;
    }
    if (sCoop.state != COOP_STATE_CONNECTED || ctx->data[1] > COOP_GOODS_TIMEOUT) {
        if (incoming) {
            sCoop.inState = COOP_IN_IDLE;
        }
        *ret = COOP_RESULT_NO_ANSWER;
        return TRUE;
    }
    return FALSE;
}

// Buyer: take the money out and tell the seller.
static BOOL Coop_PayAndWait(ScriptContext *ctx, BOOL incoming) {
    PlayerProfile_SubMoney(Coop_Profile(ctx->fieldSystem), sCoop.deal.price);
    sCoop.goodsReceived = FALSE;
    sCoop.monReceived = FALSE;
    if (incoming) {
        Coop_SendResponse(sCoop.inReq.type, sCoop.inReq.seq, COOP_RESULT_ACCEPT);
    } else {
        Coop_SendOwnNotice(COOP_ANS_PAY);
    }
    ctx->data[2] = incoming;
    SetupNativeScript(ctx, Coop_Native_WaitGoods);
    return TRUE;
}

// Seller answering a Buy request: wait for the buyer to pay for our offer.
static BOOL Coop_Native_WaitPay(ScriptContext *ctx) {
    u16 *ret = Coop_RetVar(ctx);

    ctx->data[1]++;
    if (sCoop.inPaid) {
        *ret = Coop_DeliverGoods(ctx->fieldSystem, FALSE) ? COOP_RESULT_ACCEPT : COOP_RESULT_NO_ANSWER;
        sCoop.inState = COOP_IN_IDLE;
        return TRUE;
    }
    if (sCoop.inRefused) {
        *ret = COOP_RESULT_DECLINE;
        sCoop.inState = COOP_IN_IDLE;
        return TRUE;
    }
    if (sCoop.inCancelled || sCoop.state != COOP_STATE_CONNECTED || ctx->data[1] > COOP_PROMPT_TIMEOUT) {
        *ret = COOP_RESULT_NO_ANSWER;
        sCoop.inState = COOP_IN_IDLE;
        return TRUE;
    }
    return FALSE;
}

// -------------------------------------------------------------------------
// Custom amount entry, drawn in the message box:
//   <prompt>
//   $ 0 0 1 5 0 0      (selected digit in red)
// Up/Down change the digit, Left/Right move, A confirms, B cancels.
// -------------------------------------------------------------------------

#define COOP_PICK_X         8
#define COOP_PICK_SPACING   10
#define COOP_PICK_Y         16
#define COOP_CHAR_DOLLAR    0x1A8
#define COOP_CHAR_DIGIT0    0x121

static void Coop_DrawAmountPicker(ScriptContext *ctx) {
    Window *window = FieldSysGetAttrAddr(ctx->fieldSystem, SCRIPTENV_WINDOW);
    String *raw = String_New(128, HEAP_ID_FIELD2);
    String *str = String_New(128, HEAP_ID_FIELD2);
    int i;

    FillWindowPixelBuffer(window, 15);

    ReadMsgDataIntoString(ctx->msgdata, sCoop.pickMsg, raw);
    StringExpandPlaceholders(Coop_MsgFmt(ctx), str, raw);
    AddTextPrinterParameterized(window, 1, str, 0, 0, TEXT_SPEED_NOTRANSFER, NULL);

    String_SetEmpty(str);
    String_AddChar(str, COOP_CHAR_DOLLAR);
    AddTextPrinterParameterized(window, 1, str, 0, COOP_PICK_Y, TEXT_SPEED_NOTRANSFER, NULL);
    for (i = 0; i < COOP_PICK_DIGITS; i++) {
        int digit = COOP_PICK_DIGITS - 1 - i; // leftmost is the highest
        String_SetEmpty(str);
        String_AddChar(str, COOP_CHAR_DIGIT0 + (sCoop.pickValue / sPow10[digit]) % 10);
        AddTextPrinterParameterizedWithColor(window, 1, str, COOP_PICK_X + i * COOP_PICK_SPACING, COOP_PICK_Y, TEXT_SPEED_NOTRANSFER,
            digit == sCoop.pickDigit ? MAKE_TEXT_COLOR(3, 4, 15) : MAKE_TEXT_COLOR(1, 2, 15), NULL);
    }
    CopyWindowToVram(window);

    String_Delete(raw);
    String_Delete(str);
}

static BOOL Coop_Native_PickAmount(ScriptContext *ctx) {
    u16 *ret = Coop_RetVar(ctx);
    u32 unit = sPow10[sCoop.pickDigit];
    u32 digit = (sCoop.pickValue / unit) % 10;
    u32 value = sCoop.pickValue;
    u8 pos = sCoop.pickDigit;

    if (gSystem.newKeys & PAD_BUTTON_A) {
        if (sCoop.pickValue == 0) {
            PlaySE(SEQ_SE_DP_BEEP);
            return FALSE;
        }
        PlaySE(SEQ_SE_DP_SELECT);
        sCoop.amount = sCoop.pickValue;
        Coop_BufferAmount(Coop_MsgFmt(ctx), sCoop.amount);
        *ret = TRUE;
        return TRUE;
    }
    if (gSystem.newKeys & PAD_BUTTON_B) {
        PlaySE(SEQ_SE_DP_SELECT);
        *ret = FALSE;
        return TRUE;
    }

    if (gSystem.newAndRepeatedKeys & PAD_KEY_UP) {
        value = value - digit * unit + ((digit + 1) % 10) * unit;
    } else if (gSystem.newAndRepeatedKeys & PAD_KEY_DOWN) {
        value = value - digit * unit + ((digit + 9) % 10) * unit;
    } else if (gSystem.newAndRepeatedKeys & PAD_KEY_LEFT) {
        if (pos < COOP_PICK_DIGITS - 1) {
            pos++;
        }
    } else if (gSystem.newAndRepeatedKeys & PAD_KEY_RIGHT) {
        if (pos > 0) {
            pos--;
        }
    }
    if (value > sCoop.pickMax) {
        value = sCoop.pickMax;
    }
    if (value != sCoop.pickValue || pos != sCoop.pickDigit) {
        sCoop.pickValue = value;
        sCoop.pickDigit = pos;
        PlaySE(SEQ_SE_DP_SELECT);
        Coop_DrawAmountPicker(ctx);
    }
    return FALSE;
}

// -------------------------------------------------------------------------
// Native waits
// -------------------------------------------------------------------------

static BOOL Coop_Native_WaitResponse(ScriptContext *ctx) {
    FieldSystem *fieldSystem = ctx->fieldSystem;
    u16 *ret = Coop_RetVar(ctx);
    u8 answer = sCoop.outAnswer;

    ctx->data[1]++;
    if (sCoop.state != COOP_STATE_CONNECTED) {
        *ret = COOP_RESULT_NO_ANSWER;
        return TRUE;
    }
    switch (answer) {
    case COOP_RESULT_ACCEPT:
        switch (sCoop.outReq.type) {
        case COOP_REQ_ITEM:
            // Gifts: take it out of our own Bag/wallet first, then tell the
            // partner to add it. Nothing is ever created without being removed.
            Bag_TakeItem(Save_Bag_Get(fieldSystem->saveData), sCoop.outReq.itemId, sCoop.outReq.qty, HEAP_ID_FIELD2);
            Coop_SendOwnNotice(COOP_ANS_CONFIRM);
            break;
        case COOP_REQ_MONEY:
            PlayerProfile_SubMoney(Coop_Profile(fieldSystem), sCoop.outReq.amount);
            Coop_SendOwnNotice(COOP_ANS_CONFIRM);
            break;
        case COOP_REQ_SELL_ITEM:
        case COOP_REQ_SELL_MON:
            // The buyer already paid.
            if (!Coop_DeliverGoods(fieldSystem, TRUE)) {
                answer = COOP_RESULT_NO_ANSWER;
            }
            break;
        }
        *ret = answer;
        return TRUE;
    case COOP_RESULT_OFFER:
        // Our Buy request: the partner picked what they'd sell us.
        sCoop.deal.kind = sCoop.outReq.type == COOP_REQ_BUY_MON ? COOP_DEAL_MON : COOP_DEAL_ITEM;
        sCoop.deal.itemId = sCoop.offer.itemId;
        sCoop.deal.qty = sCoop.offer.qty;
        sCoop.deal.species = sCoop.offer.species;
        sCoop.deal.level = sCoop.offer.level;
        sCoop.deal.price = sCoop.outReq.amount;
        Coop_BufferDeal(Coop_MsgFmt(ctx), &sCoop.deal);
        *ret = answer;
        return TRUE;
    case COOP_RESULT_DECLINE:
    case COOP_RESULT_BUSY:
    case COOP_RESULT_FULL:
    case COOP_RESULT_POOR:
        *ret = answer;
        return TRUE;
    default:
        break;
    }
    if (gSystem.newKeys & PAD_BUTTON_B) {
        Coop_SendCancel();
        *ret = COOP_RESULT_CANCELLED;
        return TRUE;
    }
    if (ctx->data[1] > (sCoop.outPending ? COOP_PROMPT_TIMEOUT : COOP_ANSWER_TIMEOUT)) {
        Coop_SendCancel();
        *ret = COOP_RESULT_NO_ANSWER;
        return TRUE;
    }
    return FALSE;
}

static BOOL Coop_Native_WaitCard(ScriptContext *ctx) {
    u16 *ret = Coop_RetVar(ctx);

    ctx->data[1]++;
    if (sCoop.cardReceived) {
        *ret = TRUE;
        return TRUE;
    }
    if (sCoop.state != COOP_STATE_CONNECTED || ctx->data[1] > COOP_CARD_TIMEOUT) {
        *ret = FALSE;
        return TRUE;
    }
    return FALSE;
}

static BOOL Coop_Native_WaitConfirm(ScriptContext *ctx) {
    FieldSystem *fieldSystem = ctx->fieldSystem;
    u16 *ret = Coop_RetVar(ctx);

    ctx->data[1]++;
    if (sCoop.inConfirmed) {
        if (sCoop.inReq.type == COOP_REQ_ITEM) {
            Bag_AddItem(Save_Bag_Get(fieldSystem->saveData), sCoop.inReq.itemId, sCoop.inReq.qty, HEAP_ID_FIELD2);
        } else {
            PlayerProfile_AddMoney(Coop_Profile(fieldSystem), sCoop.inReq.amount);
        }
        sCoop.inState = COOP_IN_IDLE;
        *ret = COOP_RESULT_ACCEPT;
        return TRUE;
    }
    if (sCoop.inCancelled || sCoop.state != COOP_STATE_CONNECTED || ctx->data[1] > COOP_CONFIRM_TIMEOUT) {
        sCoop.inState = COOP_IN_IDLE;
        *ret = COOP_RESULT_NO_ANSWER;
        return TRUE;
    }
    return FALSE;
}

static BOOL Coop_Native_ActivitySync(ScriptContext *ctx) {
    u16 *ret = Coop_RetVar(ctx);

    ctx->data[1]++;
    if (sCoop.state != COOP_STATE_ACTIVITY_SYNC) {
        *ret = FALSE; // link dropped
        return TRUE;
    }
    if (sub_02037B38(Coop_PreActivitySyncNo())) {
        *ret = TRUE;
        return TRUE;
    }
    if (ctx->data[1] > COOP_ACTIVITY_SYNC_FRAMES) {
        Coop_SetState(COOP_STATE_CONNECTED);
        *ret = FALSE;
        return TRUE;
    }
    return FALSE;
}

// Answering the partner's request (after the Yes/No).
static BOOL Coop_Answer(ScriptContext *ctx, u16 yesNo) {
    FieldSystem *fieldSystem = ctx->fieldSystem;
    u16 *ret = Coop_RetVar(ctx);
    CoopRequest *req = &sCoop.inReq;
    BOOL room;

    if (sCoop.inCancelled || sCoop.state != COOP_STATE_CONNECTED || sCoop.inState != COOP_IN_PROMPTING) {
        sCoop.inState = COOP_IN_IDLE;
        *ret = COOP_RESULT_CANCELLED;
        return FALSE;
    }
    if (yesNo != 0 || req->type == COOP_REQ_BUY_ITEM || req->type == COOP_REQ_BUY_MON) { // YesNo: 0 = yes
        Coop_SendResponse(req->type, req->seq, COOP_RESULT_DECLINE);
        sCoop.inState = COOP_IN_IDLE;
        *ret = COOP_RESULT_DECLINE;
        return FALSE;
    }

    switch (req->type) {
    case COOP_REQ_ITEM:
    case COOP_REQ_MONEY:
        if (req->type == COOP_REQ_ITEM) {
            room = Bag_HasSpaceForItem(Save_Bag_Get(fieldSystem->saveData), req->itemId, req->qty, HEAP_ID_FIELD2);
        } else {
            room = Coop_Money(fieldSystem) + req->amount <= MAX_MONEY;
        }
        if (!room) {
            Coop_SendResponse(req->type, req->seq, COOP_RESULT_FULL);
            sCoop.inState = COOP_IN_IDLE;
            *ret = COOP_RESULT_FULL;
            return FALSE;
        }
        Coop_SendResponse(req->type, req->seq, COOP_RESULT_ACCEPT);
        SetupNativeScript(ctx, Coop_Native_WaitConfirm);
        return TRUE;

    case COOP_REQ_SELL_ITEM:
    case COOP_REQ_SELL_MON:
        sCoop.deal.kind = req->type == COOP_REQ_SELL_MON ? COOP_DEAL_MON : COOP_DEAL_ITEM;
        sCoop.deal.itemId = req->itemId;
        sCoop.deal.qty = req->qty;
        sCoop.deal.species = req->species;
        sCoop.deal.level = req->level;
        sCoop.deal.price = req->amount;
        if (!Coop_HasRoomForGoods(fieldSystem, &sCoop.deal)) {
            Coop_SendResponse(req->type, req->seq, COOP_RESULT_FULL);
            sCoop.inState = COOP_IN_IDLE;
            *ret = COOP_RESULT_FULL;
            return FALSE;
        }
        if (Coop_Money(fieldSystem) < req->amount) {
            Coop_SendResponse(req->type, req->seq, COOP_RESULT_POOR);
            sCoop.inState = COOP_IN_IDLE;
            *ret = COOP_RESULT_POOR;
            return FALSE;
        }
        return Coop_PayAndWait(ctx, TRUE);
    }

    // Battle, Pokémon trade
    Coop_SendResponse(req->type, req->seq, COOP_RESULT_ACCEPT);
    sCoop.inState = COOP_IN_IDLE;
    *ret = COOP_RESULT_ACCEPT;
    return FALSE;
}

BOOL ScrCmd_Coop(ScriptContext *ctx) {
    FieldSystem *fieldSystem = ctx->fieldSystem;
    u16 sub = ScriptReadHalfword(ctx);
    u16 arg = ScriptGetVar(ctx);
    u16 *ret;
    void **appData;
    PlayerProfile *profile;
    Bag *bag = Save_Bag_Get(fieldSystem->saveData);
    u32 qty;

    ctx->data[0] = ScriptReadHalfword(ctx);
    ctx->data[1] = 0;
    ret = Coop_RetVar(ctx);

    switch (sub) {
    case COOP_SUB_BUFFER_PARTNER:
        profile = sub_02034818(Coop_PartnerNetId());
        *ret = FALSE;
        if (sCoop.state == COOP_STATE_CONNECTED && profile != NULL) {
            BufferPlayersName(Coop_MsgFmt(ctx), 0, profile);
            *ret = TRUE;
        }
        return FALSE;

    case COOP_SUB_REQUEST:
        *ret = FALSE;
        if (sCoop.state != COOP_STATE_CONNECTED) {
            return FALSE;
        }
        sCoop.outReq.seq++;
        sCoop.outReq.type = arg;
        sCoop.outReq.itemId = 0;
        sCoop.outReq.qty = 0;
        sCoop.outReq.amount = 0;
        sCoop.outReq.species = 0;
        sCoop.outReq.level = 0;
        switch (arg) {
        case COOP_REQ_ITEM:
            sCoop.outReq.itemId = sCoop.giftItem;
            sCoop.outReq.qty = sCoop.giftQty;
            break;
        case COOP_REQ_MONEY:
        case COOP_REQ_BUY_ITEM:
        case COOP_REQ_BUY_MON:
            sCoop.outReq.amount = sCoop.amount;
            break;
        case COOP_REQ_SELL_ITEM:
            sCoop.deal.kind = COOP_DEAL_ITEM;
            sCoop.deal.itemId = sCoop.giftItem;
            sCoop.deal.qty = sCoop.giftQty;
            sCoop.deal.price = sCoop.amount;
            sCoop.outReq.itemId = sCoop.giftItem;
            sCoop.outReq.qty = sCoop.giftQty;
            sCoop.outReq.amount = sCoop.amount;
            break;
        case COOP_REQ_SELL_MON:
            // sCoop.deal was filled in by COOP_SUB_SET_MON
            sCoop.deal.price = sCoop.amount;
            sCoop.outReq.species = sCoop.deal.species;
            sCoop.outReq.level = sCoop.deal.level;
            sCoop.outReq.amount = sCoop.amount;
            break;
        case COOP_REQ_CARD:
            sCoop.cardReceived = FALSE;
            break;
        }
        sCoop.outAnswer = COOP_RESULT_NONE;
        sCoop.outPending = FALSE;
        *ret = sub_02037030(COOP_CMD_REQUEST, &sCoop.outReq, sizeof(CoopRequest));
        return FALSE;

    case COOP_SUB_WAIT_RESPONSE:
        SetupNativeScript(ctx, Coop_Native_WaitResponse);
        return TRUE;

    case COOP_SUB_CHOOSE_ITEM: {
        BagView *bagView = Bag_CreateView(bag, sGiftPockets, HEAP_ID_FIELD3);
        // Mode 1 is the party "Give" picker: A returns the item, B returns
        // ITEM_NONE. Mode 3 (DP's script picker) never takes input in HGSS.
        sub_0207789C(bagView, fieldSystem->saveData, 1, fieldSystem->bagCursor, &fieldSystem->menuInputState);
        Bag_LaunchApp(fieldSystem, bagView);
        appData = FieldSysGetAttrAddr(fieldSystem, SCRIPTENV_RUNNING_APP_DATA);
        *appData = bagView;
        SetupNativeScript(ctx, ScrNative_WaitApplication);
        return TRUE;
    }

    case COOP_SUB_GET_ITEM: {
        u16 itemId = ITEM_NONE;
        appData = FieldSysGetAttrAddr(fieldSystem, SCRIPTENV_RUNNING_APP_DATA);
        if (*appData != NULL) {
            itemId = BagView_GetItemId(*appData);
            Heap_Free(*appData);
            *appData = NULL;
        }
        if (itemId == ITEM_NONE) {
            *ret = COOP_ITEM_NONE;
        } else if (itemId >= ITEM_HM01 && itemId <= ITEM_HM08) {
            *ret = COOP_ITEM_NOT_SENDABLE; // never let a player give away an HM they need
        } else {
            sCoop.giftItem = itemId;
            *ret = COOP_ITEM_OK;
        }
        return FALSE;
    }

    case COOP_SUB_SET_ITEM_QTY:
        *ret = 0;
        if (arg >= NELEMS(sGiftQuantities)) {
            return FALSE;
        }
        qty = Bag_GetQuantity(bag, sCoop.giftItem, HEAP_ID_FIELD2);
        if (qty > sGiftQuantities[arg]) {
            qty = sGiftQuantities[arg];
        }
        if (qty > 999) {
            qty = 999;
        }
        if (qty != 0) {
            sCoop.giftQty = qty;
            Coop_BufferItemAndQty(Coop_MsgFmt(ctx), sCoop.giftItem, qty);
            *ret = qty;
        }
        return FALSE;

    case COOP_SUB_AMOUNT_MODE:
        sCoop.amountMode = arg;
        return FALSE;

    case COOP_SUB_SET_AMOUNT:
        *ret = FALSE;
        if (arg >= NELEMS(sPresetAmounts)) {
            return FALSE;
        }
        if (sCoop.amountMode == COOP_AMOUNT_WALLET && Coop_Money(fieldSystem) < sPresetAmounts[arg]) {
            return FALSE;
        }
        sCoop.amount = sPresetAmounts[arg];
        Coop_BufferAmount(Coop_MsgFmt(ctx), sCoop.amount);
        *ret = TRUE;
        return FALSE;

    case COOP_SUB_ENTER_AMOUNT:
        *ret = FALSE;
        sCoop.pickMax = MAX_MONEY;
        if (sCoop.amountMode == COOP_AMOUNT_WALLET && Coop_Money(fieldSystem) < sCoop.pickMax) {
            sCoop.pickMax = Coop_Money(fieldSystem);
        }
        if (sCoop.pickMax == 0) {
            return FALSE;
        }
        sCoop.pickMsg = arg;
        sCoop.pickValue = 0;
        sCoop.pickDigit = 2; // hundreds
        Coop_DrawAmountPicker(ctx);
        SetupNativeScript(ctx, Coop_Native_PickAmount);
        return TRUE;

    case COOP_SUB_SET_MON: {
        Pokemon *mon;
        *ret = Coop_CheckSellableMon(fieldSystem, arg);
        if (*ret != COOP_MON_OK) {
            return FALSE;
        }
        mon = Party_GetMonByIndex(SaveArray_Party_Get(fieldSystem->saveData), arg);
        sCoop.deal.kind = COOP_DEAL_MON;
        sCoop.deal.slot = arg;
        sCoop.deal.species = GetMonData(mon, MON_DATA_SPECIES_OR_EGG, NULL);
        sCoop.deal.level = GetMonData(mon, MON_DATA_LEVEL, NULL);
        Coop_BufferMon(Coop_MsgFmt(ctx), sCoop.deal.species, sCoop.deal.level);
        return FALSE;
    }

    case COOP_SUB_SEND_OFFER:
        // We're answering the partner's Buy request with what we picked.
        if (sCoop.inCancelled || sCoop.state != COOP_STATE_CONNECTED || sCoop.inState != COOP_IN_PROMPTING) {
            sCoop.inState = COOP_IN_IDLE;
            *ret = COOP_RESULT_CANCELLED;
            return FALSE;
        }
        if (arg == COOP_DEAL_ITEM) {
            sCoop.deal.kind = COOP_DEAL_ITEM;
            sCoop.deal.itemId = sCoop.giftItem;
            sCoop.deal.qty = sCoop.giftQty;
            sCoop.deal.species = 0;
            sCoop.deal.level = 0;
        } // Pokémon: filled in by COOP_SUB_SET_MON
        sCoop.deal.price = sCoop.inReq.amount;
        Coop_BufferDeal(Coop_MsgFmt(ctx), &sCoop.deal);
        sCoop.inPaid = FALSE;
        sCoop.inRefused = FALSE;
        Coop_SendInNotice(COOP_ANS_OFFER, &sCoop.deal);
        SetupNativeScript(ctx, Coop_Native_WaitPay);
        return TRUE;

    case COOP_SUB_BUY_CONFIRM:
        // We're the buyer, looking at the partner's offer for our Buy request.
        if (sCoop.state != COOP_STATE_CONNECTED) {
            *ret = COOP_RESULT_NO_ANSWER;
            return FALSE;
        }
        if (arg != 0) {
            Coop_SendOwnNotice(COOP_ANS_REFUSE);
            *ret = COOP_RESULT_DECLINE;
            return FALSE;
        }
        if (!Coop_HasRoomForGoods(fieldSystem, &sCoop.deal)) {
            Coop_SendOwnNotice(COOP_ANS_REFUSE);
            *ret = COOP_RESULT_FULL;
            return FALSE;
        }
        if (Coop_Money(fieldSystem) < sCoop.deal.price) {
            Coop_SendOwnNotice(COOP_ANS_REFUSE);
            *ret = COOP_RESULT_POOR;
            return FALSE;
        }
        return Coop_PayAndWait(ctx, FALSE);

    case COOP_SUB_DEAL_INFO:
        *ret = sCoop.deal.kind | (sCoop.deal.toPC ? COOP_DEAL_TO_PC : 0);
        return FALSE;

    case COOP_SUB_WAIT_CARD:
        SetupNativeScript(ctx, Coop_Native_WaitCard);
        return TRUE;

    case COOP_SUB_SHOW_CARD: {
        TrainerCardAppArgs *card = sub_020691C4(HEAP_ID_FIELD2);
        if (sCoop.cardRecv != NULL) {
            MI_CpuCopy8(sCoop.cardRecv, card, COOP_CARD_SIZE);
            Heap_Free(sCoop.cardRecv);
            sCoop.cardRecv = NULL;
        } else {
            sub_02068FC8(1, 1, 0, 255, fieldSystem, card); // shouldn't happen
        }
        TrainerCard_LaunchApp(fieldSystem, card);
        appData = FieldSysGetAttrAddr(fieldSystem, SCRIPTENV_RUNNING_APP_DATA);
        *appData = card;
        SetupNativeScript(ctx, ScrNative_WaitApplication_DestroyTaskData);
        return TRUE;
    }

    case COOP_SUB_INCOMING_INFO: {
        MessageFormat *msgFmt = Coop_MsgFmt(ctx);
        CoopRequest *req = &sCoop.inReq;
        *ret = COOP_REQ_NONE;
        profile = sub_02034818(Coop_PartnerNetId());
        if (sCoop.inState != COOP_IN_PROMPTING || profile == NULL) {
            sCoop.inState = COOP_IN_IDLE;
            return FALSE;
        }
        BufferPlayersName(msgFmt, 0, profile);
        switch (req->type) {
        case COOP_REQ_ITEM:
            Coop_BufferItemAndQty(msgFmt, req->itemId, req->qty);
            break;
        case COOP_REQ_SELL_ITEM:
            Coop_BufferItemAndQty(msgFmt, req->itemId, req->qty);
            Coop_BufferAmount(msgFmt, req->amount);
            break;
        case COOP_REQ_SELL_MON:
            Coop_BufferMon(msgFmt, req->species, req->level);
            Coop_BufferAmount(msgFmt, req->amount);
            break;
        case COOP_REQ_MONEY:
        case COOP_REQ_BUY_ITEM:
        case COOP_REQ_BUY_MON:
            Coop_BufferAmount(msgFmt, req->amount);
            break;
        }
        // Let the requester know a human is looking at it (longer timeout).
        Coop_SendResponse(req->type, req->seq, COOP_ANS_PENDING);
        *ret = req->type;
        return FALSE;
    }

    case COOP_SUB_ANSWER:
        return Coop_Answer(ctx, arg);

    case COOP_SUB_ACTIVITY_SYNC:
        *ret = FALSE;
        if (sCoop.state != COOP_STATE_CONNECTED) {
            return FALSE;
        }
        Coop_SetState(COOP_STATE_ACTIVITY_SYNC);
        sub_02037AC0(Coop_PreActivitySyncNo());
        SetupNativeScript(ctx, Coop_Native_ActivitySync);
        return TRUE;

    case COOP_SUB_START_BATTLE:
    case COOP_SUB_START_TRADE:
        if (sCoop.state != COOP_STATE_ACTIVITY_SYNC) {
            // Link dropped after the sync. The trade script restores the
            // overworld next, so leave it here to keep that balanced.
            if (sub == COOP_SUB_START_TRADE) {
                CallTask_LeaveOverworld(fieldSystem->taskman);
                return TRUE;
            }
            return FALSE;
        }
        sCoop.activityCount++;
        Coop_SetState(COOP_STATE_IN_ACTIVITY);
        if (sub == COOP_SUB_START_BATTLE) {
            CallTask_CoopLinkBattle(fieldSystem->taskman);
        } else {
            CallTask_WirelessTrade(fieldSystem->taskman);
        }
        return TRUE;

    case COOP_SUB_LEAVE_FIELD:
        CallTask_LeaveOverworld(fieldSystem->taskman);
        return TRUE;

    case COOP_SUB_ACTIVITY_END:
        Coop_OnActivityEnd();
        return FALSE;

    default:
        GF_ASSERT(FALSE);
        return FALSE;
    }
}

// -------------------------------------------------------------------------
// COOP_DEBUG: while the comm manager exists, a GF_ASSERT or a failed heap
// allocation shows "A communication error has occurred". These make that
// screen show "A" (assert) or "D" (alloc) and the Thumb return addresses
// found on the stack instead, to symbolize against main.elf.xMAP.
// -------------------------------------------------------------------------

// Scan the stack above this frame for Thumb return addresses (odd values in
// the main RAM code range). The first ones are the callers of the failing
// function. ITCM return addresses (this file) fall outside the range.
static void CoopDbg_Capture(u32 kind) {
    vu32 marker = 0;
    vu32 *p = &marker;
    int i, n = 0;

    if (sCoopDbgKind != 0) {
        return;
    }
    sCoopDbgKind = kind;
    for (i = 0; i < 160 && n < COOP_DBG_WORDS; i++) {
        u32 w = p[i];
        if ((w & 1) && w >= 0x02000000 && w < 0x02300000) {
            sCoopDbgWords[n++] = w;
        }
    }
}

void CoopDbg_AssertFailReset(void) {
    CoopDbg_Capture(1);
    PrintErrorMessageAndReset();
}

void CoopDbg_AllocFailReset(void) {
    CoopDbg_Capture(2);
    PrintErrorMessageAndReset();
}

static void CoopDbg_AddHex(String *str, u32 v) {
    int i;
    for (i = 7; i >= 0; i--) {
        u32 d = (v >> (i * 4)) & 0xF;
        String_AddChar(str, d < 10 ? 0x121 + d : 0x12B + (d - 10));
    }
}

void CoopDbg_ReadErrorMessage(struct MsgData *msgData, s32 strno, struct String *dest) {
    int i;

    ReadMsgDataIntoString(msgData, strno, dest);
    if (sCoopDbgKind == 0) {
        return;
    }
    String_SetEmpty(dest);
    String_AddChar(dest, sCoopDbgKind == 1 ? 0x12B : 0x12E); // 'A' assert, 'D' alloc
    for (i = 0; i < COOP_DBG_WORDS; i++) {
        if (sCoopDbgWords[i] == 0) {
            break;
        }
        String_AddChar(dest, (i % 2 == 0) ? 0xE000 : 0x1DE);
        CoopDbg_AddHex(dest, sCoopDbgWords[i]);
    }
}

#include <nitro/itcm_end.h>

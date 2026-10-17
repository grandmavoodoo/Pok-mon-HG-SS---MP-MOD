#ifndef POKEHEARTGOLD_COOP_H
#define POKEHEARTGOLD_COOP_H

// Wireless co-op mod
//
// Two DS systems link over the DS local wireless (the same WM-based comm
// stack the Union Room / Colosseum use). While linked:
//   - each player sees the other (and the Pokemon following them) walking
//     around the overworld,
//   - each player sees the other on the Pokegear town map,
//   - talking to the partner opens a menu: Battle, Trade (Pokemon, item or
//     money) or view their Trainer Card.
//
// Controls (overworld, while no menu/script is open):
//   hold L + R, press X : host a session
//   hold L + R, press Y : join a session (connects to the first host found)
//   hold L + R, press B : disconnect / cancel
//   face your partner, press A : co-op menu (script std_coop_menu)

#include "field_system.h"
#include "script.h"

// Mod-private map object ids for the partner's avatar and their follower.
// Chosen well above any object id used by map event data.
#define COOP_PARTNER_OBJ_ID     0xF2
#define COOP_PARTNER_MON_OBJ_ID 0xF3

typedef enum CoopState {
    COOP_STATE_OFF,
    COOP_STATE_HOST_STARTING,
    COOP_STATE_HOST_WAITING,
    COOP_STATE_JOIN_STARTING,
    COOP_STATE_JOIN_SCANNING,
    COOP_STATE_JOIN_CONNECTING,
    COOP_STATE_CONNECTED,
    COOP_STATE_ACTIVITY_SYNC, // both agreed to battle/trade, waiting for each other
    COOP_STATE_IN_ACTIVITY,   // link battle or trade running; it owns the comm table
    COOP_STATE_POST_ACTIVITY, // back from it, waiting for the partner to be back too
    COOP_STATE_DISCONNECTING,
} CoopState;

// Called once per frame by FieldSystem_Control in place of
// FieldSystem_IsPlayerMovementAllowed, before normal input. Runs the co-op
// update and returns whether the player may move this frame: FALSE when
// movement isn't allowed anyway, or when the co-op update used this frame's
// input.
BOOL Coop_FieldControl(FieldSystem *fieldSystem);

// BattleSetup_New: may this battle take the link's player ids? Replaces the
// retail "comm is running" check (sub_02037474).
BOOL Coop_BattleUsesLinkIds(void);

// ov12_0226430C (battle command acknowledgement): the net id to tag it with.
// Replaces the retail sub_0203769C() call one for one, so overlay 12 keeps
// its retail size.
u16 Coop_BattleAckNetId(void);

CoopState Coop_GetState(void);
BOOL Coop_IsConnected(void);

// Pokegear map: the partner's world-map cell (same units as
// PokegearArgs.matrixXCoord / matrixYCoord). Returns FALSE if not linked.
BOOL Coop_GetPartnerMapCell(s16 *x, s16 *y, u32 *gender);

// Called as soon as a link battle / trade app returns.
void Coop_OnActivityEnd(void);

// Script command: Coop <sub>, <arg>, <retvar> (see constants/coop.h)
BOOL ScrCmd_Coop(ScriptContext *ctx);

#endif // POKEHEARTGOLD_COOP_H

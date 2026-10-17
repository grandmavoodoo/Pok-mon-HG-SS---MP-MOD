#ifndef POKEHEARTGOLD_CONSTANTS_COOP_H
#define POKEHEARTGOLD_CONSTANTS_COOP_H

// Co-op mod: shared between src/coop.c and the co-op field scripts
// (scr_seq_0003, std_coop_menu / std_coop_incoming).
//
// Message buffer slots used by the co-op texts (msg_0040):
//   0 partner name, 1 item, 2 item quantity, 3 money amount / price,
//   4 Pokémon species, 5 Pokémon level

// Coop <sub>, <arg>, <retvar>
#define COOP_SUB_BUFFER_PARTNER 0  // ret: 1 if linked; partner name -> str slot 0
#define COOP_SUB_REQUEST        1  // arg: COOP_REQ_*; ret: 1 if sent
#define COOP_SUB_WAIT_RESPONSE  2  // ret: COOP_RESULT_*
#define COOP_SUB_CHOOSE_ITEM    3  // opens the Bag (overworld must be faded out)
#define COOP_SUB_GET_ITEM       4  // ret: COOP_ITEM_*
#define COOP_SUB_SET_ITEM_QTY   5  // arg: quantity menu choice; ret: quantity or 0
#define COOP_SUB_SET_AMOUNT     6  // arg: amount menu choice; ret: 1 if allowed (see COOP_SUB_AMOUNT_MODE)
#define COOP_SUB_WAIT_CARD      7  // ret: 1 once the partner's Trainer Card arrived
#define COOP_SUB_SHOW_CARD      8  // opens the partner's Trainer Card
#define COOP_SUB_INCOMING_INFO  9  // ret: COOP_REQ_* of the pending incoming request
#define COOP_SUB_ANSWER         10 // arg: YesNo result (0 = yes); ret: COOP_RESULT_*
#define COOP_SUB_ACTIVITY_SYNC  11 // ret: 1 when both sides are ready
#define COOP_SUB_START_BATTLE   12
#define COOP_SUB_START_TRADE    13
#define COOP_SUB_ACTIVITY_END   14
#define COOP_SUB_AMOUNT_MODE    15 // arg: COOP_AMOUNT_*
#define COOP_SUB_ENTER_AMOUNT   16 // arg: msg id of the one-line prompt; ret: 1 if an amount was entered
#define COOP_SUB_SET_MON        17 // arg: party slot (255 = cancelled); ret: COOP_MON_*
#define COOP_SUB_SEND_OFFER     18 // arg: COOP_DEAL_*; ret: COOP_RESULT_* (seller side of a Buy request)
#define COOP_SUB_BUY_CONFIRM    19 // arg: YesNo result; ret: COOP_RESULT_* (buyer accepting an offer)
// 20 unused
#define COOP_SUB_LEAVE_FIELD    21 // leaves the overworld (follow with RestoreOverworld)
#define COOP_SUB_DEAL_INFO      22 // ret: COOP_DEAL_* of the current deal; bit 7 set if a bought Pokémon went to the PC

#define COOP_ITEM_NONE          0
#define COOP_ITEM_OK            1
#define COOP_ITEM_NOT_SENDABLE  2

#define COOP_MON_NONE 0 // cancelled
#define COOP_MON_OK   1
#define COOP_MON_LAST 2 // it's the last Pokémon that can battle
#define COOP_MON_MAIL 3 // holding Mail

#define COOP_AMOUNT_WALLET 0 // must be affordable (gift, offer)
#define COOP_AMOUNT_PRICE  1 // anything up to MAX_MONEY (asking price)

#define COOP_DEAL_ITEM 0
#define COOP_DEAL_MON  1
#define COOP_DEAL_TO_PC 0x80

#define COOP_REQ_NONE      0
#define COOP_REQ_BATTLE    1
#define COOP_REQ_TRADE     2
#define COOP_REQ_CARD      3
#define COOP_REQ_ITEM      4
#define COOP_REQ_MONEY     5
#define COOP_REQ_CANCEL    6
// 7 unused
#define COOP_REQ_SELL_ITEM 8  // requester sells item x qty for amount
#define COOP_REQ_SELL_MON  9  // requester sells a Pokémon for amount
#define COOP_REQ_BUY_ITEM  10 // requester pays amount for an item the partner picks
#define COOP_REQ_BUY_MON   11 // requester pays amount for a Pokémon the partner picks

#define COOP_RESULT_NONE      0
#define COOP_RESULT_ACCEPT    1
#define COOP_RESULT_DECLINE   2
#define COOP_RESULT_BUSY      3
#define COOP_RESULT_FULL      4
#define COOP_RESULT_NO_ANSWER 5
#define COOP_RESULT_CANCELLED 6
#define COOP_RESULT_POOR      7 // not enough money
#define COOP_RESULT_OFFER     8 // partner picked something for our Buy request

#endif // POKEHEARTGOLD_CONSTANTS_COOP_H

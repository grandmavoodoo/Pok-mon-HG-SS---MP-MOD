#ifndef POKEHEARTGOLD_POKEWALKER_IR_H
#define POKEHEARTGOLD_POKEWALKER_IR_H

#include <nitro/types.h>

// Pokewalker infrared port (co-op mod).
//
// The Pokewalker app (overlay 112) reaches the IR port through these instead
// of calling the Game Card's IR chip (CARD_SpiWait*) directly. Each time the
// app starts, the port is picked:
//   1. the IR chip in a Game Card, the same way as retail: the card the game
//      runs from, or under nds-bootstrap on DSi/3DS the card in Slot-1;
//   2. otherwise the 3DS's own IR port, if the RtcPwalker cheat's ARM7 half
//      (RTCom) is running;
//   3. otherwise the Game Card code anyway, which hears nothing, as retail.
// The calls are one-for-one swaps, so overlay 112 keeps its retail size.

// In place of CARD_SpiWaitInit (ov112_021E5964, once per app start).
void PokewalkerIr_Init(void);

// In place of CARD_SpiWaitReadRange: receives a packet into buf (the app's
// 0x8C-byte IR buffer) and returns its length, 0 if nothing arrived.
int PokewalkerIr_Recv(u8 *buf);

// In place of CARD_SpiWaitWriteRange: sends len bytes of buf.
void PokewalkerIr_Send(const u8 *buf, int len);

// In place of sub_0201A738 in the app's exit (ov112_021E7910): closes the
// 3DS IR port if it was used, then calls sub_0201A738(a0).
void PokewalkerIr_ExitApp(int a0);

#endif // POKEHEARTGOLD_POKEWALKER_IR_H

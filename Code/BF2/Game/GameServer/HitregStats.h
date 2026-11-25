// [TEMP] [CHOSEN, not recovered] [RETIRE-WHEN: server-hitreg] Server-side counters for the
// hit-registration investigation (frame pacing, player input queues). Off unless the console
// variable `server-hitreg-stats` holds a report interval in seconds; then one `[TEMP][HITREG]`
// server line and one line per active player go to stderr each interval. Remove with its callers
// once the investigation is closed.

#ifndef GAME_GAMESERVER_HITREGSTATS_H
#define GAME_GAMESERVER_HITREGSTATS_H

namespace dice {
namespace hfe {
namespace hitreg {

bool enabled();

// One dedicated main-loop frame. lateness is how far past the last consumed tick boundary the
// frame sampled its clock, in seconds.
void frame(int ticks, double lateness);

// An input accepted into the player's ActionBuffer, with the index the client stamped on it.
void inputReceived(int playerId, int inputIndex);

// One simulation tick of a human player: a queued input, a replay of the last one, or the
// neutral input used after 30 replays.
void inputConsumed(int playerId);
void inputReplayed(int playerId, bool neutral);

// The ActionBuffer trim check after each server update: queue size before the trim and how
// many inputs it dropped.
void trimCheck(int playerId, int queued, int dropped, unsigned int ping, unsigned int interpolation);

} // namespace hitreg
} // namespace hfe
} // namespace dice

#endif // GAME_GAMESERVER_HITREGSTATS_H

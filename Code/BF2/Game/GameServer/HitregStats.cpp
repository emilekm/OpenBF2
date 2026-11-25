// [TEMP] [CHOSEN, not recovered] [RETIRE-WHEN: server-hitreg] See HitregStats.h.

#include "HitregStats.h"
#include "../Utility/Vars.h"
#include "../../Core/System.h"

#include <algorithm>
#include <cstdio>
#include <map>

namespace dice {
namespace hfe {
namespace hitreg {

namespace {

const double kLongTickGap = 0.040;      // a tick that followed a skipped one at 30 Hz
const double kLongLateness = 0.010;
const int    kMaxIndexAdvance = 1000;   // a larger jump is a new connection, not lost inputs

struct PlayerStats
{
    bool         hasIndex = false;
    int          lastIndex = 0;
    int          indexAdvance = 0;
    int          received = 0;
    int          lost = 0;
    int          consumed = 0;
    int          replayed = 0;
    int          neutral = 0;
    int          trimmed = 0;
    int          trimEvents = 0;
    long long    queueSum = 0;
    int          queueSamples = 0;
    int          queueMax = 0;
    unsigned int ping = 0;
    unsigned int interpolation = 0;

    bool active() const { return received || consumed || replayed || neutral || queueSamples; }

    void resetWindow()
    {
        PlayerStats fresh;
        fresh.hasIndex = hasIndex;
        fresh.lastIndex = lastIndex;
        *this = fresh;
    }
};

struct ServerStats
{
    double windowStart = -1.0;
    double lastFrame = -1.0;
    double lastTick = -1.0;
    int    frames = 0;
    int    ticks = 0;
    int    framesByTicks[4] = {};
    int    longGaps = 0;
    double maxGap = 0.0;
    int    tickFrames = 0;
    double latenessSum = 0.0;
    double latenessMax = 0.0;
    int    longLateness = 0;
};

ServerStats s_server;
std::map<int, PlayerStats> s_players;

int* intervalSeconds()
{
    static int* const interval = Vars::getInt("server-hitreg-stats", 0);
    return interval;
}

void startWindow(double now)
{
    const double lastTick = s_server.lastTick;
    s_server = ServerStats();
    s_server.windowStart = now;
    s_server.lastFrame = now;
    s_server.lastTick = lastTick;
}

void report(double window)
{
    const ServerStats& s = s_server;
    std::fprintf(stderr,
        "[TEMP][HITREG] server window=%.1f frames=%d ticks=%d tickrate=%.4f f0=%d f1=%d f2=%d f3=%d "
        "gap40=%d gapmax=%.1f late_avg=%.2f late_max=%.2f late10=%d players=%d\n",
        window, s.frames, s.ticks, s.ticks / window,
        s.framesByTicks[0], s.framesByTicks[1], s.framesByTicks[2], s.framesByTicks[3],
        s.longGaps, s.maxGap * 1000.0,
        s.tickFrames ? s.latenessSum * 1000.0 / s.tickFrames : 0.0, s.latenessMax * 1000.0,
        s.longLateness, static_cast<int>(s_players.size()));

    for (std::map<int, PlayerStats>::iterator it = s_players.begin(); it != s_players.end();)
    {
        PlayerStats& p = it->second;
        if (!p.active())
        {
            it = s_players.erase(it);
            continue;
        }
        const int simulated = p.consumed + p.replayed + p.neutral;
        std::fprintf(stderr,
            "[TEMP][HITREG] player id=%d inputrate=%.4f surplus_ppm=%+.0f recv=%d lost=%d used=%d "
            "replay=%d neutral=%d trim=%d trim_events=%d q_avg=%.2f q_max=%d ping=%u interp=%u\n",
            it->first, p.indexAdvance / window,
            simulated ? (p.indexAdvance - simulated) * 1e6 / simulated : 0.0,
            p.received, p.lost, p.consumed, p.replayed, p.neutral, p.trimmed, p.trimEvents,
            p.queueSamples ? static_cast<double>(p.queueSum) / p.queueSamples : 0.0, p.queueMax,
            p.ping, p.interpolation);
        p.resetWindow();
        ++it;
    }
    std::fflush(stderr);
}

} // namespace

bool enabled()
{
    return *intervalSeconds() > 0;
}

void frame(int ticks, double lateness)
{
    const double now = g_system->getWallClockTime();
    // Frames are only counted while enabled; a long silence means the stats were off.
    if (s_server.windowStart < 0.0 || now - s_server.lastFrame > 1.0)
    {
        startWindow(now);
        for (std::map<int, PlayerStats>::iterator it = s_players.begin(); it != s_players.end(); ++it)
            it->second.resetWindow();
    }
    s_server.lastFrame = now;

    ++s_server.frames;
    ++s_server.framesByTicks[std::min(std::max(ticks, 0), 3)];
    if (ticks > 0)
    {
        s_server.ticks += ticks;
        ++s_server.tickFrames;
        s_server.latenessSum += lateness;
        s_server.latenessMax = std::max(s_server.latenessMax, lateness);
        if (lateness > kLongLateness)
            ++s_server.longLateness;
        if (s_server.lastTick >= 0.0)
        {
            const double gap = now - s_server.lastTick;
            s_server.maxGap = std::max(s_server.maxGap, gap);
            if (gap > kLongTickGap)
                ++s_server.longGaps;
        }
        s_server.lastTick = now;
    }

    const double window = now - s_server.windowStart;
    if (window >= *intervalSeconds())
    {
        report(window);
        startWindow(now);
    }
}

void inputReceived(int playerId, int inputIndex)
{
    PlayerStats& p = s_players[playerId];
    ++p.received;
    const int advance = inputIndex - p.lastIndex;
    if (p.hasIndex && advance > 0 && advance <= kMaxIndexAdvance)
    {
        p.indexAdvance += advance;
        p.lost += advance - 1;
    }
    p.hasIndex = true;
    p.lastIndex = inputIndex;
}

void inputConsumed(int playerId)
{
    ++s_players[playerId].consumed;
}

void inputReplayed(int playerId, bool neutral)
{
    PlayerStats& p = s_players[playerId];
    if (neutral)
        ++p.neutral;
    else
        ++p.replayed;
}

void trimCheck(int playerId, int queued, int dropped, unsigned int ping, unsigned int interpolation)
{
    PlayerStats& p = s_players[playerId];
    p.queueSum += queued;
    ++p.queueSamples;
    p.queueMax = std::max(p.queueMax, queued);
    if (dropped > 0)
    {
        p.trimmed += dropped;
        ++p.trimEvents;
    }
    p.ping = ping;
    p.interpolation = interpolation;
}

} // namespace hitreg
} // namespace hfe
} // namespace dice

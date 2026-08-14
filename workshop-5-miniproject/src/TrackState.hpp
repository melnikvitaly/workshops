#pragma once

// What the tracking loop is doing right now. Drives the status LED and the log.
enum class TrackState
{
    Touring,  // boot-time walk around the working zone; the loop is not yet live
    Disarmed, // motion inhibited by the BOOT button
    NoLink,   // no frame from the PC within TRACK_TIMEOUT_MS
    Lost,     // link alive, but the PC cannot see dot and/or target
    Tracking, // closed loop running
};

inline const char *trackStateName(TrackState s)
{
    switch (s)
    {
    case TrackState::Touring:  return "TOUR";
    case TrackState::Disarmed: return "DISARM";
    case TrackState::NoLink:   return "NOLINK";
    case TrackState::Lost:     return "LOST";
    case TrackState::Tracking: return "TRACK";
    }
    return "?";
}

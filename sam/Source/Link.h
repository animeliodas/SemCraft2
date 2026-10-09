/* SemCraft 2 - the link to the Minecraft mod: a minimal WebSocket client (RFC 6455 text frames) on 127.0.0.1.
 * A background thread connects (and reconnects every second), answers pings and queues received messages;
 * Send() may be called from any thread and never blocks the game for long (TCP_NODELAY, small frames).
 * Adapted from universal-modder's MIT minecraft-gta5-passthrough example (gta/src/ws.cpp).
 *
 * This header must stay free of engine headers: Link.cpp is compiled without the engine's precompiled header.
 */
#ifndef SEMCRAFT2_LINK_H
#define SEMCRAFT2_LINK_H

#include <string>

namespace link {

// Start the background connection to host:port. Safe to call once.
void Start(const char *strHost, int iPort);
// Stop and join the thread.
void Stop(void);
// Connected right now?
bool Connected(void);
// Bumped on every successful (re)connect, so callers can resend their state.
int Generation(void);
// Queue a text message (dropped if not connected).
bool Send(const std::string &strText);
// Next received message, if any.
bool Poll(std::string &strMessage);

}; // namespace

#endif

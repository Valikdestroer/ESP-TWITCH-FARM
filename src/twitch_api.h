#ifndef TWITCH_API_H
#define TWITCH_API_H

#include "config.h"
#include <Arduino.h>

class TwitchAPI {
public:
    static void init();
    static String getCleanToken();
    static const char* getClientId();
    static bool fetchCurrentUser();
    static bool fetchIntegrityToken();
    static bool fetchInventoryAndProgress();
    static bool findLiveStreamForGame(const char* gameName);
    static bool fetchPlaybackAccessToken(const char* channelLogin, String& outSig, String& outValue, String& outSessionId);
    static bool pingUsherPlaylist(const char* channelLogin, const String& sig, const String& value);
    static bool sendMinuteWatchedHeartbeat(const char* channelLogin, const char* channelId, const char* broadcastId);
    static bool claimDrop(const char* dropInstanceId);

    // Priority Game Queue Management
    static void selectNextGameFromQueue();
    static void autoDiscoverCampaigns();
    static void markStreamerFailed(const char* channelLogin);
    static bool isStreamerFailed(const char* channelLogin);

    // Twitch PubSub WebSockets Engine
    static void connectPubSub();
    static void disconnectPubSub();
    static void subscribePubSubTopics();
    static void loopPubSub();

private:
    static String sendGraphQLRequest(const String& payload);
    static bool sendSpadeBeacon(const char* channelId, const char* broadcastId, const char* sessionId);
    static bool sendGqlSpadeEvents(const char* channelId, const char* broadcastId);
    static void sendPubSubPing();
    static String urlEncode(const String& str);
};

#endif // TWITCH_API_H



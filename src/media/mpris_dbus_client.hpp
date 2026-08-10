#pragma once
#include "media/mpris_client.hpp"
#include <dbus/dbus.h>
#include <map>
#include <unordered_map>

class EventLoop;

// Event-driven MPRIS client. Subscribes to
// org.freedesktop.DBus.Properties.PropertiesChanged (arg0 namespace
// org.mpris.MediaPlayer2.Player) and NameOwnerChanged for
// org.mpris.MediaPlayer2.*, hooked into the daemon's epoll loop via
// libdbus watch functions. No 1 Hz metadata polling: PlaybackStatus
// and Metadata arrive inside the signal payload; only Position (never
// signaled) is interpolated in poll() and resynced every 2 s.
class MprisDBusClient : public MprisClient {
public:
    explicit MprisDBusClient(EventLoop& loop);
    ~MprisDBusClient() override;

    bool init() override;
    void shutdown() override;
    void poll(int elapsed_ms) override;
    const MprisState& state() const override { return state_; }

    void playPause() override;
    void next() override;
    void previous() override;
    void seekTo(int64_t position_us) override;

private:
    struct PlayerInfo {
        std::string unique;       // :1.234
        std::string well_known;   // org.mpris.MediaPlayer2.spotify
        bool playing = false;
        uint64_t last_activity_ms = 0;  // monotonic, arbitration tie-break
    };

    // D-Bus <-> epoll bridge
    static dbus_bool_t watchAdd(DBusWatch* w, void* data);
    static void watchRemove(DBusWatch* w, void* data);
    static void watchToggle(DBusWatch* w, void* data);
    void updateWatch(DBusWatch* w);
    static DBusHandlerResult filterCb(DBusConnection* c, DBusMessage* m, void* data);

    void handlePropertiesChanged(DBusMessage* m);
    void handleNameOwnerChanged(DBusMessage* m);

    // Enumeration / queries (short blocking calls, 150 ms timeout)
    void enumeratePlayers();
    void addPlayer(const std::string& well_known);
    void refreshPlayer(const std::string& unique);
    std::string queryPlaybackStatus(const std::string& dest);
    bool queryMetadata(const std::string& dest, MprisState& out);
    int64_t queryPosition(const std::string& dest);
    void sendPlayerCommand(const char* method);

    void parseMetadataIter(DBusMessageIter* dict, MprisState& out);
    void recomputeActive(MprisEvent hint, bool force_notify);
    uint64_t nowMs() const;

    EventLoop& loop_;
    DBusConnection* conn_ = nullptr;
    bool private_conn_ = false;

    std::map<std::string, PlayerInfo> players_;     // by unique name
    std::string active_unique_;
    MprisState state_;

    std::unordered_map<int, uint32_t> watch_events_;  // fd -> epoll events
    uint64_t activity_counter_ = 0;

    int resync_ms_ = 0;  // accumulates poll() time until a Position Get
};

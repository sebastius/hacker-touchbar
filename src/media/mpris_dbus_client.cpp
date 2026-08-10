#include "media/mpris_dbus_client.hpp"
#include "engine/event_loop.hpp"
#include "util/log.hpp"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <sys/epoll.h>
#include <unistd.h>

namespace {
constexpr int DBUS_CALL_TIMEOUT_MS = 150;
constexpr int64_t POSITION_RESYNC_MS = 2000;
constexpr const char* MPRIS_PREFIX = "org.mpris.MediaPlayer2.";
constexpr const char* PLAYER_IFACE = "org.mpris.MediaPlayer2.Player";
constexpr const char* MPRIS_PATH = "/org/mpris/MediaPlayer2";

uint32_t dbusFlagsToEpoll(unsigned flags) {
    uint32_t ev = 0;
    if (flags & DBUS_WATCH_READABLE) ev |= EPOLLIN;
    if (flags & DBUS_WATCH_WRITABLE) ev |= EPOLLOUT;
    return ev;
}

unsigned epollToDbusFlags(uint32_t events) {
    unsigned flags = 0;
    if (events & EPOLLIN) flags |= DBUS_WATCH_READABLE;
    if (events & EPOLLOUT) flags |= DBUS_WATCH_WRITABLE;
    if (events & (EPOLLERR | EPOLLHUP)) flags |= DBUS_WATCH_ERROR;
    return flags;
}
}  // namespace

MprisDBusClient::MprisDBusClient(EventLoop& loop) : loop_(loop) {}

MprisDBusClient::~MprisDBusClient() { shutdown(); }

uint64_t MprisDBusClient::nowMs() const {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ---- D-Bus <-> epoll bridge ----

dbus_bool_t MprisDBusClient::watchAdd(DBusWatch* w, void* data) {
    auto* self = static_cast<MprisDBusClient*>(data);
    if (!dbus_watch_get_enabled(w)) return true;
    self->updateWatch(w);
    return true;
}

void MprisDBusClient::watchRemove(DBusWatch* w, void* data) {
    auto* self = static_cast<MprisDBusClient*>(data);
    int fd = dbus_watch_get_unix_fd(w);
    auto it = self->watch_events_.find(fd);
    if (it != self->watch_events_.end()) {
        self->loop_.removeFd(fd);
        self->watch_events_.erase(it);
    }
}

void MprisDBusClient::watchToggle(DBusWatch* w, void* data) {
    auto* self = static_cast<MprisDBusClient*>(data);
    if (!dbus_watch_get_enabled(w))
        watchRemove(w, data);
    else
        self->updateWatch(w);
}

void MprisDBusClient::updateWatch(DBusWatch* w) {
    int fd = dbus_watch_get_unix_fd(w);
    uint32_t ev = dbusFlagsToEpoll(dbus_watch_get_flags(w));
    if (ev == 0) return;

    auto it = watch_events_.find(fd);
    if (it != watch_events_.end() && it->second == ev) return;
    if (it != watch_events_.end()) loop_.removeFd(fd);

    uint32_t registered = ev | EPOLLERR | EPOLLHUP;
    if (!loop_.addFd(fd, registered, [this, w](uint32_t events) {
            dbus_watch_handle(w, epollToDbusFlags(events));
            while (dbus_connection_dispatch(conn_) == DBUS_DISPATCH_DATA_REMAINS) {}
        })) {
        log_warn("MPRIS: failed to watch D-Bus fd %d", fd);
        return;
    }
    watch_events_[fd] = ev;
}

// ---- signal handling ----

DBusHandlerResult MprisDBusClient::filterCb(DBusConnection*, DBusMessage* m,
                                            void* data) {
    auto* self = static_cast<MprisDBusClient*>(data);
    if (dbus_message_is_signal(m, "org.freedesktop.DBus.Properties",
                               "PropertiesChanged") &&
        strcmp(dbus_message_get_path(m) ? dbus_message_get_path(m) : "",
               MPRIS_PATH) == 0) {
        self->handlePropertiesChanged(m);
    } else if (dbus_message_is_signal(m, "org.freedesktop.DBus",
                                      "NameOwnerChanged")) {
        self->handleNameOwnerChanged(m);
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

void MprisDBusClient::handleNameOwnerChanged(DBusMessage* m) {
    const char* name = nullptr;
    const char* old_owner = nullptr;
    const char* new_owner = nullptr;
    if (!dbus_message_get_args(m, nullptr,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_STRING, &old_owner,
                               DBUS_TYPE_STRING, &new_owner,
                               DBUS_TYPE_INVALID))
        return;
    if (!name || strncmp(name, MPRIS_PREFIX, strlen(MPRIS_PREFIX)) != 0)
        return;

    if (new_owner && *new_owner) {
        log_info("MPRIS: player appeared: %s", name);
        addPlayer(name);
    } else if (old_owner && *old_owner) {
        log_info("MPRIS: player gone: %s", name);
        players_.erase(old_owner);
        if (active_unique_ == old_owner)
            recomputeActive(MprisEvent::StateChanged, true);
    }
}

void MprisDBusClient::handlePropertiesChanged(DBusMessage* m) {
    const char* sender = dbus_message_get_sender(m);
    if (!sender) return;

    DBusMessageIter args;
    if (!dbus_message_iter_init(m, &args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
        return;
    const char* iface = nullptr;
    dbus_message_iter_get_basic(&args, &iface);
    if (!iface || strcmp(iface, PLAYER_IFACE) != 0) return;
    if (!dbus_message_iter_next(&args) ||
        dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_ARRAY)
        return;

    auto pit = players_.find(sender);
    if (pit == players_.end()) return;  // not a tracked MPRIS player
    PlayerInfo& player = pit->second;
    player.last_activity_ms = nowMs();

    bool status_changed = false;
    bool metadata_changed = false;
    bool became_playing = false;
    MprisState incoming = state_;  // filled from the payload when active

    DBusMessageIter dict;
    dbus_message_iter_recurse(&args, &dict);
    while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, val;
        dbus_message_iter_recurse(&dict, &entry);
        const char* key = nullptr;
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        dbus_message_iter_recurse(&entry, &val);

        if (key && strcmp(key, "PlaybackStatus") == 0 &&
            dbus_message_iter_get_arg_type(&val) == DBUS_TYPE_STRING) {
            const char* ps = nullptr;
            dbus_message_iter_get_basic(&val, &ps);
            bool playing = ps && strcmp(ps, "Playing") == 0;
            if (playing != player.playing) {
                became_playing = playing;
                status_changed = true;
                player.playing = playing;
            }
        } else if (key && strcmp(key, "Metadata") == 0 &&
                   dbus_message_iter_get_arg_type(&val) == DBUS_TYPE_ARRAY) {
            DBusMessageIter meta;
            dbus_message_iter_recurse(&val, &meta);
            parseMetadataIter(&meta, incoming);
            metadata_changed = true;
        }
        dbus_message_iter_next(&dict);
    }

    // Playing always wins arbitration immediately.
    const std::string prev_active = active_unique_;
    recomputeActive(became_playing ? MprisEvent::PlaybackStarted
                                   : MprisEvent::StateChanged, false);

    // If arbitration swapped the active player, recomputeActive already
    // refreshed state_ and fired the right event.
    if (sender == active_unique_ && prev_active == active_unique_) {
        bool track_changed = metadata_changed &&
            (incoming.title != state_.title ||
             incoming.track_id != state_.track_id);
        if (metadata_changed) {
            state_.title = incoming.title;
            state_.artist = incoming.artist;
            state_.album = incoming.album;
            state_.track_id = incoming.track_id;
            state_.art_url = incoming.art_url;
            if (incoming.length_us > 0) state_.length_us = incoming.length_us;
            state_.position_us = queryPosition(active_unique_);
            resync_ms_ = 0;
        }
        if (status_changed) {
            state_.playing = player.playing;
            state_.position_us = queryPosition(active_unique_);
            resync_ms_ = 0;
        }
        if (track_changed)
            fire(MprisEvent::TrackChanged);
        else if (became_playing)
            fire(MprisEvent::PlaybackStarted);
        else if (status_changed || metadata_changed)
            fire(MprisEvent::StateChanged);
    }
}

// ---- enumeration and queries ----

void MprisDBusClient::enumeratePlayers() {
    DBusMessage* msg = dbus_message_new_method_call(
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "ListNames");
    if (!msg) return;
    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(conn_, msg,
                                                  DBUS_CALL_TIMEOUT_MS, nullptr);
    dbus_message_unref(msg);
    if (!reply) return;

    DBusMessageIter args, sub;
    if (dbus_message_iter_init(reply, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_ARRAY) {
        dbus_message_iter_recurse(&args, &sub);
        while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_STRING) {
            const char* name = nullptr;
            dbus_message_iter_get_basic(&sub, &name);
            if (name && strncmp(name, MPRIS_PREFIX, strlen(MPRIS_PREFIX)) == 0)
                addPlayer(name);
            dbus_message_iter_next(&sub);
        }
    }
    dbus_message_unref(reply);
}

void MprisDBusClient::addPlayer(const std::string& well_known) {
    // Resolve the unique owner so we can match signal senders.
    DBusMessage* msg = dbus_message_new_method_call(
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetNameOwner");
    if (!msg) return;
    const char* wk = well_known.c_str();
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &wk, DBUS_TYPE_INVALID);
    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(conn_, msg,
                                                  DBUS_CALL_TIMEOUT_MS, nullptr);
    dbus_message_unref(msg);
    if (!reply) return;

    const char* unique = nullptr;
    if (!dbus_message_get_args(reply, nullptr, DBUS_TYPE_STRING, &unique,
                               DBUS_TYPE_INVALID) || !unique) {
        dbus_message_unref(reply);
        return;
    }
    dbus_message_unref(reply);

    PlayerInfo info;
    info.unique = unique;
    info.well_known = well_known;
    info.last_activity_ms = ++activity_counter_;
    players_[info.unique] = info;
    refreshPlayer(info.unique);
}

void MprisDBusClient::refreshPlayer(const std::string& unique) {
    auto it = players_.find(unique);
    if (it == players_.end()) return;
    PlayerInfo& player = it->second;

    std::string ps = queryPlaybackStatus(player.well_known);
    player.playing = (ps == "Playing");
    recomputeActive(MprisEvent::StateChanged, false);
}

std::string MprisDBusClient::queryPlaybackStatus(const std::string& dest) {
    DBusMessage* msg = dbus_message_new_method_call(
        dest.c_str(), MPRIS_PATH, "org.freedesktop.DBus.Properties", "Get");
    if (!msg) return "";
    const char* iface = PLAYER_IFACE;
    const char* prop = "PlaybackStatus";
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface,
                             DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(conn_, msg,
                                                  DBUS_CALL_TIMEOUT_MS, nullptr);
    dbus_message_unref(msg);
    if (!reply) return "";

    std::string result;
    DBusMessageIter args, variant;
    if (dbus_message_iter_init(reply, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&args, &variant);
        if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
            const char* val = nullptr;
            dbus_message_iter_get_basic(&variant, &val);
            if (val) result = val;
        }
    }
    dbus_message_unref(reply);
    return result;
}

void MprisDBusClient::parseMetadataIter(DBusMessageIter* dict, MprisState& out) {
    while (dbus_message_iter_get_arg_type(dict) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry, val;
        dbus_message_iter_recurse(dict, &entry);
        const char* key = nullptr;
        dbus_message_iter_get_basic(&entry, &key);
        dbus_message_iter_next(&entry);
        dbus_message_iter_recurse(&entry, &val);

        if (!key) {
        } else if (strcmp(key, "xesam:title") == 0 &&
                   dbus_message_iter_get_arg_type(&val) == DBUS_TYPE_STRING) {
            const char* v = nullptr;
            dbus_message_iter_get_basic(&val, &v);
            out.title = v ? v : "";
        } else if (strcmp(key, "xesam:artist") == 0 &&
                   dbus_message_iter_get_arg_type(&val) == DBUS_TYPE_ARRAY) {
            DBusMessageIter arr;
            dbus_message_iter_recurse(&val, &arr);
            if (dbus_message_iter_get_arg_type(&arr) == DBUS_TYPE_STRING) {
                const char* v = nullptr;
                dbus_message_iter_get_basic(&arr, &v);
                out.artist = v ? v : "";
            }
        } else if (strcmp(key, "xesam:album") == 0 &&
                   dbus_message_iter_get_arg_type(&val) == DBUS_TYPE_STRING) {
            const char* v = nullptr;
            dbus_message_iter_get_basic(&val, &v);
            out.album = v ? v : "";
        } else if (strcmp(key, "mpris:length") == 0 &&
                   dbus_message_iter_get_arg_type(&val) == DBUS_TYPE_INT64) {
            int64_t v = 0;
            dbus_message_iter_get_basic(&val, &v);
            out.length_us = v;
        } else if (strcmp(key, "mpris:trackid") == 0 &&
                   dbus_message_iter_get_arg_type(&val) == DBUS_TYPE_OBJECT_PATH) {
            const char* v = nullptr;
            dbus_message_iter_get_basic(&val, &v);
            out.track_id = v ? v : "";
        } else if (strcmp(key, "mpris:artUrl") == 0 &&
                   dbus_message_iter_get_arg_type(&val) == DBUS_TYPE_STRING) {
            const char* v = nullptr;
            dbus_message_iter_get_basic(&val, &v);
            out.art_url = v ? v : "";
        }
        dbus_message_iter_next(dict);
    }
}

bool MprisDBusClient::queryMetadata(const std::string& dest, MprisState& out) {
    DBusMessage* msg = dbus_message_new_method_call(
        dest.c_str(), MPRIS_PATH, "org.freedesktop.DBus.Properties", "Get");
    if (!msg) return false;
    const char* iface = PLAYER_IFACE;
    const char* prop = "Metadata";
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface,
                             DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(conn_, msg,
                                                  DBUS_CALL_TIMEOUT_MS, nullptr);
    dbus_message_unref(msg);
    if (!reply) return false;

    bool ok = false;
    DBusMessageIter args, variant, dict;
    if (dbus_message_iter_init(reply, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&args, &variant);
        if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_ARRAY) {
            dbus_message_iter_recurse(&variant, &dict);
            parseMetadataIter(&dict, out);
            ok = true;
        }
    }
    dbus_message_unref(reply);
    return ok;
}

int64_t MprisDBusClient::queryPosition(const std::string& dest) {
    auto it = players_.find(dest);
    const std::string& target =
        (it != players_.end()) ? it->second.well_known : dest;

    DBusMessage* msg = dbus_message_new_method_call(
        target.c_str(), MPRIS_PATH, "org.freedesktop.DBus.Properties", "Get");
    if (!msg) return 0;
    const char* iface = PLAYER_IFACE;
    const char* prop = "Position";
    dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface,
                             DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);
    DBusMessage* reply =
        dbus_connection_send_with_reply_and_block(conn_, msg,
                                                  DBUS_CALL_TIMEOUT_MS, nullptr);
    dbus_message_unref(msg);
    if (!reply) return state_.position_us;

    int64_t pos = state_.position_us;
    DBusMessageIter args, variant;
    if (dbus_message_iter_init(reply, &args) &&
        dbus_message_iter_get_arg_type(&args) == DBUS_TYPE_VARIANT) {
        dbus_message_iter_recurse(&args, &variant);
        if (dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_INT64)
            dbus_message_iter_get_basic(&variant, &pos);
    }
    dbus_message_unref(reply);
    return pos;
}

// ---- arbitration: playing wins, then most-recently-active ----

void MprisDBusClient::recomputeActive(MprisEvent hint, bool force_notify) {
    std::string best_playing, best_any;
    uint64_t best_playing_act = 0, best_any_act = 0;
    for (auto& [unique, p] : players_) {
        if (p.playing && (best_playing.empty() ||
                          p.last_activity_ms > best_playing_act)) {
            best_playing = unique;
            best_playing_act = p.last_activity_ms;
        }
        if (best_any.empty() || p.last_activity_ms > best_any_act) {
            best_any = unique;
            best_any_act = p.last_activity_ms;
        }
    }
    const std::string& chosen = !best_playing.empty() ? best_playing : best_any;

    if (chosen == active_unique_ && !force_notify) {
        // Active unchanged; nothing to re-pull.
        if (chosen.empty()) return;
        return;
    }

    const bool had_player = state_.has_player;
    const std::string old_track = state_.track_id;

    active_unique_ = chosen;
    if (chosen.empty()) {
        state_ = MprisState{};
        if (had_player) fire(MprisEvent::StateChanged);
        return;
    }

    PlayerInfo& p = players_[chosen];
    MprisState next;
    next.has_player = true;
    next.playing = p.playing;
    queryMetadata(p.well_known, next);
    state_.has_player = true;
    state_.playing = next.playing;
    state_.title = next.title;
    state_.artist = next.artist;
    state_.album = next.album;
    state_.track_id = next.track_id;
    state_.art_url = next.art_url;
    state_.player = p.well_known;
    state_.length_us = next.length_us;
    state_.position_us = queryPosition(chosen);
    resync_ms_ = 0;

    if (!old_track.empty() && state_.track_id != old_track) {
        fire(MprisEvent::TrackChanged);
        return;
    }
    fire(hint);
}

// ---- public API ----

bool MprisDBusClient::init() {
    // When started as root (DRM master), reach the invoking user's
    // session bus. libdbus refuses to honor DBUS_SESSION_BUS_ADDRESS in
    // a setresuid window (saved-uid 0 != real uid trips its setuid
    // guard), so open the address explicitly with a private connection
    // and authenticate as the user via SO_PEERCRED inside the window.
    std::string explicit_addr;
    uid_t user_uid = 0;
    bool as_user = false;
    if (getuid() == 0 && !getenv("DBUS_SESSION_BUS_ADDRESS")) {
        const char* sudo_uid = getenv("SUDO_UID");
        if (sudo_uid) {
            user_uid = static_cast<uid_t>(atoi(sudo_uid));
            explicit_addr =
                std::string("unix:path=/run/user/") + sudo_uid + "/bus";
            as_user = setresuid(user_uid, user_uid, 0) == 0;
            if (!as_user)
                log_warn("MPRIS: could not drop to uid %d, D-Bus auth may fail",
                         (int)user_uid);
        }
    }

    DBusError err;
    dbus_error_init(&err);
    if (!explicit_addr.empty()) {
        conn_ = dbus_connection_open_private(explicit_addr.c_str(), &err);
        if (conn_ && !dbus_bus_register(conn_, &err)) {
            dbus_connection_close(conn_);
            dbus_connection_unref(conn_);
            conn_ = nullptr;
        }
        private_conn_ = conn_ != nullptr;
    } else {
        conn_ = dbus_bus_get(DBUS_BUS_SESSION, &err);
    }
    if (as_user)
        setresuid(0, 0, 0);
    if (dbus_error_is_set(&err) || !conn_) {
        log_warn("MPRIS: D-Bus connection failed: %s",
                 dbus_error_is_set(&err) ? err.message : "unknown");
        dbus_error_free(&err);
        conn_ = nullptr;
        return false;
    }
    dbus_connection_set_exit_on_disconnect(conn_, FALSE);

    if (!dbus_connection_set_watch_functions(conn_, watchAdd, watchRemove,
                                             watchToggle, this, nullptr)) {
        log_warn("MPRIS: failed to register D-Bus watches");
        return false;
    }
    if (!dbus_connection_add_filter(conn_, filterCb, this, nullptr)) {
        log_warn("MPRIS: failed to add D-Bus filter");
        return false;
    }

    const char* match_props =
        "type='signal',interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged',arg0namespace='org.mpris.MediaPlayer2.Player'";
    const char* match_names =
        "type='signal',sender='org.freedesktop.DBus',"
        "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
        "arg0namespace='org.mpris.MediaPlayer2'";
    dbus_bus_add_match(conn_, match_props, &err);
    if (dbus_error_is_set(&err)) {
        // Older bus daemons lack arg0namespace; fall back to a broad
        // match and filter in-process.
        log_warn("MPRIS: arg0namespace match unsupported, using broad match");
        dbus_error_free(&err);
        dbus_bus_add_match(conn_,
            "type='signal',interface='org.freedesktop.DBus.Properties',"
            "member='PropertiesChanged'", &err);
    }
    dbus_error_free(&err);
    dbus_bus_add_match(conn_, match_names, &err);
    if (dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        dbus_bus_add_match(conn_,
            "type='signal',sender='org.freedesktop.DBus',"
            "interface='org.freedesktop.DBus',member='NameOwnerChanged'", &err);
    }
    dbus_error_free(&err);
    dbus_connection_flush(conn_);

    enumeratePlayers();
    if (!players_.empty())
        recomputeActive(MprisEvent::StateChanged, true);

    log_info("MPRIS: %zu player(s), event-driven (PropertiesChanged)",
             players_.size());
    return true;
}

void MprisDBusClient::shutdown() {
    if (!conn_) return;
    for (auto& [fd, _] : watch_events_) loop_.removeFd(fd);
    watch_events_.clear();
    dbus_connection_remove_filter(conn_, filterCb, this);
    dbus_connection_set_watch_functions(conn_, nullptr, nullptr, nullptr,
                                        nullptr, nullptr);
    if (private_conn_) dbus_connection_close(conn_);
    dbus_connection_unref(conn_);
    conn_ = nullptr;
}

void MprisDBusClient::poll(int elapsed_ms) {
    if (!state_.has_player || !state_.playing) return;

    // Interpolate between signals; resync against the player every 2 s
    // (Position is never included in PropertiesChanged).
    state_.position_us += static_cast<int64_t>(elapsed_ms) * 1000;
    if (state_.length_us > 0 && state_.position_us > state_.length_us)
        state_.position_us = state_.length_us;

    resync_ms_ += elapsed_ms;
    if (resync_ms_ >= POSITION_RESYNC_MS && conn_) {
        resync_ms_ = 0;
        state_.position_us = queryPosition(active_unique_);
    }
}

void MprisDBusClient::sendPlayerCommand(const char* method) {
    if (!conn_ || active_unique_.empty()) return;
    auto it = players_.find(active_unique_);
    if (it == players_.end()) return;
    DBusMessage* msg = dbus_message_new_method_call(
        it->second.well_known.c_str(), MPRIS_PATH, PLAYER_IFACE, method);
    if (msg) {
        dbus_connection_send(conn_, msg, nullptr);
        dbus_connection_flush(conn_);
        dbus_message_unref(msg);
    }
}

void MprisDBusClient::playPause() { sendPlayerCommand("PlayPause"); }
void MprisDBusClient::next() { sendPlayerCommand("Next"); }
void MprisDBusClient::previous() { sendPlayerCommand("Previous"); }

void MprisDBusClient::seekTo(int64_t position_us) {
    if (!conn_ || !state_.has_player || state_.track_id.empty()) return;
    auto it = players_.find(active_unique_);
    if (it == players_.end()) return;

    DBusMessage* msg = dbus_message_new_method_call(
        it->second.well_known.c_str(), MPRIS_PATH, PLAYER_IFACE, "SetPosition");
    if (!msg) return;
    const char* track = state_.track_id.c_str();
    dbus_message_append_args(msg, DBUS_TYPE_OBJECT_PATH, &track,
                             DBUS_TYPE_INT64, &position_us, DBUS_TYPE_INVALID);
    dbus_connection_send(conn_, msg, nullptr);
    dbus_connection_flush(conn_);
    dbus_message_unref(msg);

    state_.position_us = position_us;  // optimistic; resync confirms
    resync_ms_ = 0;
}

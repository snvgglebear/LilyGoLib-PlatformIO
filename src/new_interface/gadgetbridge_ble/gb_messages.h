/**
 * @file      gb_messages.h
 * @license   MIT
 * @brief     SMS and chat notifications, threaded into conversations.
 *
 * The protocol has no dedicated message type: an SMS and a Signal message both
 * arrive as `notify` (§5.3), told apart only by which optional fields the phone
 * filled in. This turns that stream into something a watch can actually read --
 * one thread per correspondent, oldest message first, with replies the watch
 * sends appended in place so a conversation reads as a conversation.
 *
 * Notifications that are not message-like (build finished, package delivered)
 * are left alone; GbApp keeps those in its plain notification list.
 */
#pragma once

#include <stdint.h>

#include <string>
#include <vector>

#include "gb_protocol.h"

/// Conversations kept on the watch, oldest evicted first.
#ifndef GB_MAX_CONVERSATIONS
#define GB_MAX_CONVERSATIONS 8
#endif

/// Messages kept per conversation.
#ifndef GB_MAX_MESSAGES_PER_CONVERSATION
#define GB_MAX_MESSAGES_PER_CONVERSATION 12
#endif

struct GbMessage {
    int32_t id = 0;             ///< notification id it arrived as; 0 for our own replies
    bool outgoing = false;      ///< true for replies sent from the watch
    std::string text;
    int64_t received = 0;       ///< local epoch seconds, 0 if the clock was not set yet
};

struct GbConversation {
    std::string app;            ///< `src`, e.g. "Signal" or "Messages"
    std::string contact;        ///< `sender`, falling back to `tel` then `title`
    std::string tel;            ///< set for SMS/MMS; replies route by number (§6.6)
    std::vector<GbMessage> messages;    ///< oldest first
    bool unread = false;
    int64_t updated = 0;
    int32_t latest_id = 0;      ///< id of the newest incoming message, for reply/dismiss

    /// Newest message text, for the conversation list.
    const std::string &preview() const;
};

class GbMessageStore
{
public:
    /**
     * Does this notification look like a message rather than an alert?
     *
     * True when the phone filled in `tel` (SMS/MMS) or `sender`, or when `src`
     * names a known messaging app -- some of them send neither field.
     */
    static bool isTextMessage(const GbNotification &notification);

    /**
     * File a notification into its conversation.
     *
     * @return false if it is not message-like, in which case the caller keeps
     *         it as an ordinary notification instead.
     *
     * Gadgetbridge re-sends a notification under the same id when Android
     * updates it, so a repeat id replaces that message rather than doubling it.
     */
    bool ingest(const GbNotification &notification, int64_t now);

    /// Handle `notify-` (§5.4): drop that message, and the thread if it empties.
    void removeNotification(int32_t id);

    void clear();

    /// Append a reply the watch just sent, so the thread shows both sides.
    void appendOutgoing(size_t index, const std::string &text, int64_t now);

    void markRead(size_t index);

    size_t unreadCount() const;

    /// Newest activity first -- the order the conversation list shows.
    const std::vector<GbConversation> &conversations() const
    {
        return m_conversations;
    }

    const GbConversation *at(size_t index) const
    {
        return index < m_conversations.size() ? &m_conversations[index] : nullptr;
    }

    /**
     * True once since the last incoming message, so the UI can raise a popup
     * exactly once per message instead of on every refresh.
     */
    bool takeArrivalFlag();

private:
    /// Groups by app plus `tel`, `sender` or `title` -- whichever the phone sent.
    static std::string threadKey(const GbNotification &notification);

    std::vector<GbConversation> m_conversations;   ///< newest activity first
    std::vector<std::string> m_keys;               ///< thread key, parallel to m_conversations
    bool m_arrived = false;
};

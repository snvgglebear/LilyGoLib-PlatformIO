/**
 * @file      gb_messages.cpp
 * @license   MIT
 * @brief     Threading SMS and chat notifications into conversations.
 *            See gb_messages.h.
 */
#include "gb_messages.h"

#include <algorithm>

namespace
{

const std::string GB_EMPTY;

/// Unit separator: cannot occur in an app name or a contact name.
const char GB_KEY_SEPARATOR = '\x1f';

std::string lowercase(const std::string &text)
{
    std::string out = text;
    for (char &c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

/**
 * Apps whose notifications are conversations.
 *
 * Only consulted when the phone sent neither `tel` nor `sender`, which is the
 * one case where nothing on the wire says "this is a message". Matching on an
 * app name is inherently a guess -- see the `type` field proposed in
 * android-sms-notifications-plan.md for the fix that removes the guessing.
 */
bool isMessagingApp(const std::string &source)
{
    static const char *const apps[] = {
        "message", "messaging", "sms", "mms", "chat",
        "signal", "whatsapp", "telegram", "threema", "conversations",
        "element", "matrix", "briar", "session", "viber", "silence",
        "messenger", "discord", "slack", "mattermost", "quicksy",
    };
    const std::string haystack = lowercase(source);
    for (const char *app : apps) {
        if (haystack.find(app) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

const std::string &GbConversation::preview() const
{
    return messages.empty() ? GB_EMPTY : messages.back().text;
}

bool GbMessageStore::isTextMessage(const GbNotification &notification)
{
    // A phone number means SMS/MMS, and a named sender means somebody said
    // something to you. Either is enough on its own.
    if (!notification.tel.empty() || !notification.sender.empty()) {
        return true;
    }
    return isMessagingApp(notification.src);
}

std::string GbMessageStore::threadKey(const GbNotification &notification)
{
    // Prefer the number: the same contact can arrive with or without a resolved
    // name depending on whether Android matched it to the address book.
    const std::string &who = !notification.tel.empty() ? notification.tel
                             : (!notification.sender.empty() ? notification.sender
                                : notification.title);
    return notification.src + GB_KEY_SEPARATOR + who;
}

bool GbMessageStore::ingest(const GbNotification &notification, int64_t now)
{
    if (!isTextMessage(notification)) {
        return false;
    }

    // Notification bodies are the message; the title is the contact. Some apps
    // put the text in the title when there is no body at all.
    const std::string &text = !notification.body.empty() ? notification.body
                              : (!notification.subject.empty() ? notification.subject
                                 : notification.title);

    const std::string key = threadKey(notification);
    size_t index = m_conversations.size();
    for (size_t i = 0; i < m_keys.size(); i++) {
        if (m_keys[i] == key) {
            index = i;
            break;
        }
    }

    if (index == m_conversations.size()) {
        GbConversation conversation;
        conversation.app = notification.src;
        conversation.contact = !notification.sender.empty() ? notification.sender
                               : (!notification.tel.empty() ? notification.tel
                                  : notification.title);
        conversation.tel = notification.tel;
        m_conversations.insert(m_conversations.begin(), conversation);
        m_keys.insert(m_keys.begin(), key);
    } else if (index != 0) {
        // Most recent activity first, so the list needs no sorting.
        GbConversation conversation = m_conversations[index];
        std::string moved_key = m_keys[index];
        m_conversations.erase(m_conversations.begin() + index);
        m_keys.erase(m_keys.begin() + index);
        m_conversations.insert(m_conversations.begin(), conversation);
        m_keys.insert(m_keys.begin(), moved_key);
    }

    GbConversation &conversation = m_conversations.front();
    if (conversation.tel.empty()) {
        conversation.tel = notification.tel;     // a later message may resolve it
    }
    if (!notification.sender.empty()) {
        conversation.contact = notification.sender;
    }

    // Android updates a conversation notification in place under the same id,
    // so a repeat replaces that message instead of appending a duplicate.
    bool replaced = false;
    for (GbMessage &message : conversation.messages) {
        if (!message.outgoing && message.id == notification.id) {
            message.text = text;
            message.received = now;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        GbMessage message;
        message.id = notification.id;
        message.outgoing = false;
        message.text = text;
        message.received = now;
        conversation.messages.push_back(message);
        if (conversation.messages.size() > GB_MAX_MESSAGES_PER_CONVERSATION) {
            conversation.messages.erase(conversation.messages.begin());
        }
    }

    conversation.unread = true;
    conversation.updated = now;
    conversation.latest_id = notification.id;

    if (m_conversations.size() > GB_MAX_CONVERSATIONS) {
        m_conversations.pop_back();
        m_keys.pop_back();
    }

    m_arrived = true;
    return true;
}

void GbMessageStore::removeNotification(int32_t id)
{
    for (size_t i = m_conversations.size(); i-- > 0;) {
        GbConversation &conversation = m_conversations[i];

        for (size_t m = conversation.messages.size(); m-- > 0;) {
            if (!conversation.messages[m].outgoing && conversation.messages[m].id == id) {
                conversation.messages.erase(conversation.messages.begin() + m);
            }
        }

        // A thread with nothing but our own replies left has been read and
        // dismissed on the phone -- there is nothing to come back to.
        bool has_incoming = false;
        for (const GbMessage &message : conversation.messages) {
            if (!message.outgoing) {
                has_incoming = true;
                break;
            }
        }
        if (!has_incoming) {
            m_conversations.erase(m_conversations.begin() + i);
            m_keys.erase(m_keys.begin() + i);
        }
    }
}

void GbMessageStore::clear()
{
    m_conversations.clear();
    m_keys.clear();
}

void GbMessageStore::appendOutgoing(size_t index, const std::string &text, int64_t now)
{
    if (index >= m_conversations.size()) {
        return;
    }
    GbConversation &conversation = m_conversations[index];

    GbMessage message;
    message.outgoing = true;
    message.text = text;
    message.received = now;
    conversation.messages.push_back(message);
    if (conversation.messages.size() > GB_MAX_MESSAGES_PER_CONVERSATION) {
        conversation.messages.erase(conversation.messages.begin());
    }
    conversation.updated = now;
}

void GbMessageStore::markRead(size_t index)
{
    if (index < m_conversations.size()) {
        m_conversations[index].unread = false;
    }
}

size_t GbMessageStore::unreadCount() const
{
    size_t count = 0;
    for (const GbConversation &conversation : m_conversations) {
        if (conversation.unread) {
            count++;
        }
    }
    return count;
}

bool GbMessageStore::takeArrivalFlag()
{
    bool arrived = m_arrived;
    m_arrived = false;
    return arrived;
}

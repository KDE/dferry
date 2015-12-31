#ifndef EVENT_H
#define EVENT_H

#include "error.h"
#include "message.h"
#include <string>

class Commutex;
class TransceiverPrivate;

// these are exclusively sent from and to Transceiver instances so far, nevertheless it seems logical
// to dispatch events in EventDispatcher, what with the name...
struct Event
{
    enum Type : uint32 {
        SendMessage = 0,
        SendMessageWithPendingReply,
        SpontaneousMessageReceived,
        PendingReplySuccess,
        PendingReplyFailure,
        PendingReplyCancel, // 5
        MainTransceiverDisconnect,
        SecondaryTransceiverConnect,
        SecondaryTransceiverDisconnect,
        UniqueNameReceived
    };

    Event(Type t) : type(t) {}
    virtual ~Event() = 0;

    Type type;
};

struct SendMessageEvent : public Event
{
    SendMessageEvent() : Event(Event::SendMessage) {}
    Message message;
};

struct SendMessageWithPendingReplyEvent : public Event
{
    SendMessageWithPendingReplyEvent() : Event(Event::SendMessageWithPendingReply) {}
    Message message;
    TransceiverPrivate *transceiver;
};

struct SpontaneousMessageReceivedEvent : public Event
{
    SpontaneousMessageReceivedEvent() : Event(Event::SpontaneousMessageReceived) {}
    Message message;
};

struct PendingReplySuccessEvent : public Event
{
    PendingReplySuccessEvent() : Event(Event::PendingReplySuccess) {}
    Message reply;
};

struct PendingReplyFailureEvent : public Event
{
    PendingReplyFailureEvent() : Event(Event::PendingReplyFailure) {}
    uint32 m_serial;
    Error m_error;
};

struct PendingReplyCancelEvent : public Event
{
    PendingReplyCancelEvent() : Event(Event::PendingReplyCancel) {}
    uint32 serial;
};

struct MainTransceiverDisconnectEvent : public Event
{
    MainTransceiverDisconnectEvent() : Event(Event::MainTransceiverDisconnect) {}
    // no additiona3 data members - we could also just use Event
};

struct SecondaryTransceiverConnectEvent : public Event
{
    SecondaryTransceiverConnectEvent() : Event(Event::SecondaryTransceiverConnect) {}
    TransceiverPrivate *transceiver;
    Commutex *id;
};

struct SecondaryTransceiverDisconnectEvent : public Event
{
    SecondaryTransceiverDisconnectEvent() : Event(Event::SecondaryTransceiverDisconnect) {}
    TransceiverPrivate *transceiver;
};

struct UniqueNameReceivedEvent : public Event
{
    UniqueNameReceivedEvent() : Event(Event::UniqueNameReceived) {}
    std::string uniqueName;
};

#endif // EVENT_H

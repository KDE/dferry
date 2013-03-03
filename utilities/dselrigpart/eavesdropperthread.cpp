#include "eavesdropperthread.h"

#include "eavesdroppermodel.h"
#include "epolleventdispatcher.h"
#include "localsocket.h"
#include "message.h"
#include "transceiver.h"

EavesdropperThread::EavesdropperThread(EavesdropperModel *model)
{
    // do not parent this to the model; it doesn't work across threads
    moveToThread(&m_thread);
    // ### verify that the connection is a QueuedConnection
    connect(this, SIGNAL(messageReceived(Message *, QDateTime)),
            model, SLOT(addMessage(Message *, QDateTime)));
    connect(&m_thread, SIGNAL(started()), SLOT(run()));
    m_thread.start();
}

static void fillEavesdropMessage(Message *spyEnable, const char *messageType)
{
    spyEnable->setType(Message::MethodCallMessage);
    spyEnable->setDestination(std::string("org.freedesktop.DBus"));
    spyEnable->setInterface(std::string("org.freedesktop.DBus"));
    spyEnable->setPath(std::string("/org/freedesktop/DBus"));
    spyEnable->setMethod(std::string("AddMatch"));
    ArgumentList argList;
    ArgumentList::WriteCursor writer = argList.beginWrite();
    std::string str = "eavesdrop=true,type=";
    str += messageType;
    writer.writeString(cstring(str.c_str()));
    writer.finish();
    spyEnable->setArgumentList(argList);
}

void EavesdropperThread::run()
{
    m_dispatcher = new EpollEventDispatcher;

    m_transceiver = new Transceiver(m_dispatcher);
    m_transceiver->setClient(this);
    {
        static const int messageTypeCount = 4;
        const char *messageType[messageTypeCount] = {
            "signal",
            "method_call",
            "method_return",
            "error"
        };
        for (int i = 0; i < messageTypeCount; i++) {
            Message *spyEnable = new Message;
            fillEavesdropMessage(spyEnable, messageType[i]);
            m_transceiver->sendAsync(spyEnable);
        }
    }

    while (true) {
        m_dispatcher->poll();
    }
}

void EavesdropperThread::messageReceived(Message *message)
{
    emit messageReceived(message, QDateTime::currentDateTime());
}

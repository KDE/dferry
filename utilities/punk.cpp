#include "argumentlist.h"
#include "epolleventdispatcher.h"
#include "itransceiverclient.h"
#include "localsocket.h"
#include "message.h"
#include "transceiver.h"

#include <iostream>
#include <string>

using namespace std;


void fillHelloMessage(Message *hello)
{
    hello->setType(Message::MethodCallMessage);
    hello->setDestination(string("org.freedesktop.DBus"));
    hello->setInterface(string("org.freedesktop.DBus"));
    hello->setPath(string("/org/freedesktop/DBus"));
    hello->setMethod(string("Hello"));
}

void fillEavesdropMessage(Message *spyEnable, const char *messageType)
{
    spyEnable->setType(Message::MethodCallMessage);
    spyEnable->setDestination(string("org.freedesktop.DBus"));
    spyEnable->setInterface(string("org.freedesktop.DBus"));
    spyEnable->setPath(string("/org/freedesktop/DBus"));
    spyEnable->setMethod(string("AddMatch"));
    ArgumentList argList;
    ArgumentList::WriteCursor writer = argList.beginWrite();
    std::string str = "eavesdrop=true,type=";
    str += messageType;
    writer.writeString(cstring(str.c_str()));
    writer.finish();
    spyEnable->setArgumentList(argList);
}

class ReplyPrinter : public ITransceiverClient
{
    // reimplemented from ITransceiverClient
    virtual void messageReceived(Message *m);
};

void ReplyPrinter::messageReceived(Message *m)
{
    cout << '\n' << m->prettyPrint();
    delete m;
}

int main(int argc, char *argv[])
{
    EpollEventDispatcher dispatcher;

    Transceiver transceiver(&dispatcher);
    int serial = 1;
    ReplyPrinter receiver;
    transceiver.setClient(&receiver);
    {
        Message *hello = new Message(serial++);
        fillHelloMessage(hello);
        transceiver.sendAsync(hello);

        static const int messageTypeCount = 4;
        const char *messageType[messageTypeCount] = {
            "signal",
            "method_call",
            "method_return",
            "error"
        };
        for (int i = 0; i < messageTypeCount; i++) {
            Message *spyEnable = new Message(serial++);
            fillEavesdropMessage(spyEnable, messageType[i]);
            transceiver.sendAsync(spyEnable);
        }
    }

    while (true) {
        dispatcher.poll();
    }

    return 0;
}

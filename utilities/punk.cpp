#include "argumentlist.h"
#include "epolleventdispatcher.h"
#include "itransceiverclient.h"
#include "localsocket.h"
#include "message.h"
#include "transceiver.h"

#include <iostream>

using namespace std;


void fillHelloMessage(Message *hello)
{
    hello->setType(Message::MethodCallMessage);
    hello->setDestination(string("org.freedesktop.DBus"));
    hello->setInterface(string("org.freedesktop.DBus"));
    hello->setPath(string("/org/freedesktop/DBus"));
    hello->setMethod(string("Hello"));
}

class ReplyPrinter : public ITransceiverClient
{
    // reimplemented from ITransceiverClient
    virtual void messageReceived(Message *m);
};

void ReplyPrinter::messageReceived(Message *m)
{
    cout << "\nReceived:\n" << m->prettyPrint();
    delete m;
}

int main(int argc, char *argv[])
{

    EpollEventDispatcher dispatcher;

    Transceiver transceiver(&dispatcher);
    ReplyPrinter receiver;
    transceiver.setClient(&receiver);
    {
        Message hello(1);
        fillHelloMessage(&hello);
        cout << "Sending:\n" << hello.prettyPrint() << '\n';
        transceiver.sendAsync(&hello);

        Message spyEnable(2);
        spyEnable.setType(Message::MethodCallMessage);
        spyEnable.setDestination(string("org.freedesktop.DBus"));
        spyEnable.setInterface(string("org.freedesktop.DBus"));
        spyEnable.setPath(string("/org/freedesktop/DBus"));
        spyEnable.setMethod(string("AddMatch"));
        ArgumentList argList;
        ArgumentList::WriteCursor writer = argList.beginWrite();
        writer.writeString(cstring("eavesdrop=true,type='method_call'"));
        writer.finish();
        spyEnable.setArgumentList(argList);
        transceiver.sendAsync(&spyEnable);
        while (true) {
            dispatcher.poll();
        }
    }


    return 0;
}

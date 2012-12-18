#include "argumentlist.h"
#include "authnegotiator.h"
#include "epolleventdispatcher.h"
#include "localsocket.h"
#include "message.h"
#include "pathfinder.h"

#include <iostream>

using namespace std;

void printArguments(ArgumentList::ReadCursor reader )
{
    // TODO - this is well-known though
}

int main(int argc, char *argv[])
{
    // TODO find session bus
    SessionBusInfo sessionBusInfo = PathFinder::sessionBusInfo();
    cout << "session bus address type: " << sessionBusInfo.addressType << '\n';
    cout << "session bus path: " << sessionBusInfo.path << '\n';

    LocalSocket socket(sessionBusInfo.path);
    cout << "connection is " << (socket.isOpen() ? "open" : "closed") << ".\n";

    EpollEventDispatcher dispatcher;
    socket.setEventDispatcher(&dispatcher);

    {
        AuthNegotiator authNegotiator(&socket);
        while (socket.isOpen() && !authNegotiator.isAuthenticated()) {
            dispatcher.poll();
        }

        if (!authNegotiator.isAuthenticated()) {
            return 1;
        }
    }

    {
        Message hello(1);
        hello.setType(Message::MethodCallMessage);
        hello.setDestination(string("org.freedesktop.DBus"));
        hello.setInterface(string("org.freedesktop.DBus"));
        hello.setPath(string("/org/freedesktop/DBus"));
        hello.setMethod(string("Hello"));
        hello.writeTo(&socket);
        while (socket.isOpen() && hello.isWriting()) {
            dispatcher.poll();
        }
        cout << "Hello sent(?)\n";
    }

    {
        Message helloReply;
        helloReply.readFrom(&socket);
        while (socket.isOpen() && helloReply.isReading()) {
            dispatcher.poll();
        }
        cout << "Hello received(?)\n";
    }

    return 0;
}

#include "argumentlist.h"
#include "authnegotiator.h"
#include "epolleventdispatcher.h"
#include "localsocket.h"
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

    AuthNegotiator authNegotiator(&socket);

    while (socket.isOpen()) {
        dispatcher.poll();
#if 0
        // TODO
        if (!socket.hasMessage()) {
            continue;
        }
        Message msg = socket.takeMessage();
        ArgumentList argList = msg.argumentList();
        ArgumentList::ReadCursor reader = argList.beginRead();
        printArguments(reader);
#endif
    }
    return 0;
}

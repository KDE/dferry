#include "argumentlist.h"
#include "epolleventdispatcher.h"
#include "localsocket.h"
#include "stringtools.h"
#include "pathfinder.h"

#include <iostream>
#include <sstream>

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

    LocalSocket sock(sessionBusInfo.path);
    cout << "connection is " << (sock.isOpen() ? "open" : "closed") << ".\n";

    int uid = 1000; // H4X
    stringstream uidDecimal;
    uidDecimal << uid;
    cout << uidDecimal.str() << ':' << hexEncode(uidDecimal.str()) << '\n';

    // TODO authentication ping-pong
    // some findings:
    // - the string after the server OK is its UUID that also appears in the address string
    // - the string after the client "EXTERNAL" is the hex-encoded UID

    // only for API testing so far! make this look better with better API!
    EpollEventDispatcher dispatcher;

    sock.setEventDispatcher(&dispatcher);
#if 0
    // TODO
    LocalSocket ls(....);
    dispatcher.add(&cnx)
    while (true) {
        dispatcher.poll();
        if (!cnx.hasMessage()) {
            continue;
        }
        Message msg = cnx.takeMessage();
        ArgumentList argList = msg.argumentList();
        ArgumentList::ReadCursor reader = argList.beginRead();
        printArguments(reader);
    }
#endif
    return 0;
}

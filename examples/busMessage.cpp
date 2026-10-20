#include "arguments.h"
#include "argumentswriter.h"
#include "connectaddress.h"
#include "connection.h"
#include "eventdispatcher.h"
#include "error.h"
#include "imessagereceiver.h"
#include "message.h"

#include <iostream>

class MyReceiver : public IMessageReceiver
{
    void handleSpontaneousMessageReceived(Message msg, Connection *conn) override
    {
        std::cout << "Message received: " << msg.method() << "\n" << msg.arguments().prettyPrint();
        if (msg.method() == "helloWithData") {
            conn->eventDispatcher()->interrupt(); // exit run()
        }
    }
};

int main(int argc, char *argv[])
{
    EventDispatcher dispatcher;
    Connection conn(&dispatcher, ConnectAddress::StandardBus::Session);

    // The following is only necessary to know our unique bus name before sending messages
    // to ourselves. Otherwise, messages sent now are queued up until ready to send.
    conn.waitForConnectionEstablished();

    MyReceiver receiver;
    conn.setSpontaneousMessageReceiver(&receiver);

    // Send a message without arguments
    {
        // Message to: object path, interface, method name
        Message msg = Message::createCall("/", "org.bar.foo", "hello");
        // Send it to ourself using the session bus
        msg.setDestination(conn.uniqueName());
        conn.sendNoReply(std::move(msg));
    }

    // Send a message with arguments
    {
        Message msg = Message::createCall("/", "org.bar.foo", "helloWithData");
        msg.setDestination(conn.uniqueName());

        ArgumentsWriter writer;
        writer.writeUint32(1234);
        writer.writeString(argc >= 2 ? argv[1] : "Hello, world!");
        msg.setArguments(writer.finish());

        conn.sendNoReply(std::move(msg));
    }

    dispatcher.run();

    return 0;
}

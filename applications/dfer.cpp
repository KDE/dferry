// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "arguments.h"
#include "argumentsreader.h"
#include "argumentswriter.h"
#include "connectaddress.h"
#include "error.h"
#include "eventdispatcher.h"
#include "imessagereceiver.h"
#include "message.h"
#include "pendingreply.h"
#include "connection.h"

#include <iostream>
#include <string>

#include "setupeavesdropping.h"

class ReplyPrinter : public IMessageReceiver
{
    // reimplemented from IMessageReceiver
    void handleSpontaneousMessageReceived(Message m, Connection *) override;
};

void ReplyPrinter::handleSpontaneousMessageReceived(Message m, Connection *)
{
    std::cout << '\n' << m.prettyPrint();
}

static void printHelp()
{
    std::cout << "dfer options:\n"
                 "  --session-bus  Monitor the session bus [the default]\n"
                 "  --system-bus   Monitor the system bus\n"
                 "  --help         Show this help and exit\n";
}

int main(int argc, char *argv[])
{
    EventDispatcher dispatcher;

    ConnectAddress::StandardBus bus = ConnectAddress::StandardBus::Session;
    for (int i = 1; i < argc; i++) {
        std::string s = argv[i];
        if (s == "--help") {
            printHelp();
            exit(0);
        } else if (s == "--system-bus") {
            bus = ConnectAddress::StandardBus::System;
        } else if (s == "--session-bus") {
            bus = ConnectAddress::StandardBus::Session;
        } else {
            std::cerr << "Unknown option \"" << s << "\".\n";
            printHelp();
            exit(1);
        }
    }

    Connection connection(&dispatcher, bus);

    if (setupEavesdropping(&connection) == FailedEavesdropping) {
        return -1;
    }

    ReplyPrinter receiver;
    connection.setSpontaneousMessageReceiver(&receiver);

    while (true) {
        dispatcher.poll();
    }

    return 0;
}

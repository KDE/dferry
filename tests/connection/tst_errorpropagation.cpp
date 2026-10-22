/*
   Copyright (C) 2018 Andreas Hartmetz <ahartmetz@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LGPL.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.

   Alternatively, this file is available under the Mozilla Public License
   Version 1.1.  You may obtain a copy of the License at
   http://www.mozilla.org/MPL/
*/

#include "argumentswriter.h"
#include "connectaddress.h"
#include "error.h"
#include "eventdispatcher.h"
#include "imessagereceiver.h"
#include "message.h"
#include "pendingreply.h"
#include "testutil.h"
#include "connection.h"

#include <iostream>

static const char *s_testMethod = "dferryTestingMethod";

class ReplierReceiver : public IMessageReceiver
{
public:
    void handleSpontaneousMessageReceived(Message msg, Connection *connection) override
    {
        std::cerr << "   Replier here. Yo, got it!" << std::endl;
        // we're on the session bus, so we'll receive all kinds of notifications we don't care about here
        if (msg.type() != Message::MethodCallMessage || msg.method() != s_testMethod) {
            return;
        }
        // TODO also generate a malformed reply and see what happens
        Message reply = Message::createReplyTo(msg);
        connection->sendNoReply(std::move(reply));
    }
};

enum { StepsCount = 10 };

void test_errorPropagation()
{
    EventDispatcher eventDispatcher;

    // TODO do everything also with sendNoReply()

    for (int errorAtStep = 0; errorAtStep < StepsCount; errorAtStep++) {

        Connection conn(&eventDispatcher, ConnectAddress::StandardBus::Session);
        conn.setDefaultReplyTimeout(500);
        conn.waitForConnectionEstablished();
        TEST(conn.state() == Connection::Connected);

        ReplierReceiver replier;
        conn.setSpontaneousMessageReceiver(&replier);

        ArgumentsWriter writer;
        writer.beginVariant();

        // If errorAtStep == 0, we do NOT introduce an error, just to check that the intentional
        // errors are the only ones

        // The following pattern will repeat for every step where an error can be introduced
        if (errorAtStep != 1) {
            // Do it right
            writer.writeUint32(0);
            writer.endVariant();
        } else {
            // Introduce an error
            writer.endVariant(); // a variant may not be empty
        }

        if (errorAtStep == 2) {
            // too many file descriptors, we "magically" know that the max number of allowed file
            // descriptors is 16. TODO it should be possible to ask the Connection about it(?)
            for (int i = 0; i < 17; i++) {
                // bogus file descriptors, shouldn't matter: the error should occur before they might
                // possibly need to be valid
                writer.writeUnixFd(100000);
            }
        }

        Message msg;
        if (errorAtStep != 3) {
            msg.setType(Message::MethodCallMessage);
        }

        // not adding arguments to produce an error won't work - a call without arguments is fine!
        msg.setArguments(writer.finish());

        if (errorAtStep != 4) {
            msg.setDestination(conn.uniqueName());
        }
        if (errorAtStep != 5) {
            msg.setPath("/foo/bar/dferry/testing");
        }
        if (errorAtStep != 6) {
            msg.setMethod(s_testMethod);
        }
        // Note interface is optional, so we can't introduce an error by omitting it (except with a signal,
        // but we don't test signals)

        if (errorAtStep == 7) {
            conn.close();
        }

        PendingReply reply = conn.send(std::move(msg));

        if (errorAtStep == 8) {
            // Since we haven't sent any (non-internal) messages yet, we rely on the send going through
            // immediately, but the receive should fail due to this disconnect.
            conn.close();
        }

        while (!reply.isFinished()) {
            eventDispatcher.poll();
        }

/*
Sources of error yet to do:
Message too large, other untested important Message properties?
Error reply from other side
Timeout
Malformed reply?
Malformed reply arguments?

 */

        static const Error::Code expectedErrors[StepsCount] = {
            Error::NoError,
            Error::NotSingleCompleteTypeInVariant,
            Error::SendingTooManyUnixFds,
            Error::MessageType,
            Error::NoError, // TODO: probably wrong, message with no destination?!
            Error::MessagePath,
            Error::MessageMethod,
            Error::LocalDisconnect,
            Error::LocalDisconnect,
            // TODO also test remote disconnect - or is that covered in another test?
            Error::NoError // TODO: actually inject an error in that case
        };

        std::cerr << "Error at step " << errorAtStep << ": error = " << reply.error().string()
                  << std::endl;
        if (reply.reply()) {
            std::cerr << "    reply msg error = " << reply.reply()->error().string()
                      << ", reply msg args error = " << reply.reply()->arguments().error().string()
                      << std::endl;
        }

        TEST(reply.error().code() == expectedErrors[errorAtStep]);
        if (reply.reply()) {
            // ### Strange that no destination is different from no path or no method. They are all invalid
            //     in a similar way.
            if (errorAtStep == 4) {
                TEST(reply.reply()->type() == Message::ErrorMessage);
            } else {
                TEST(reply.reply()->type() == Message::MethodReplyMessage);
            }
            TEST(reply.reply()->error().code() == expectedErrors[errorAtStep]);
            TEST(reply.reply()->arguments().error().code() == expectedErrors[errorAtStep]);
        } else {
            Message replyMsg = reply.takeReply();
            TEST(replyMsg.type() == Message::InvalidMessage);
            TEST(replyMsg.error().code() == reply.error().code());
        }
    }
}

void test_errorEnumToString()
{
    // Doesn't really belong here, but a separate test file for this? Nah.

    // Test first and last enum values of all error ranges to check that the indexing shenanigans
    // in Error::message() work. This did detect errors when the code was new.
    // Obviously, we don't want to essentially duplicate the entire logic of message(), so glass-box
    // testing of known difficult cases only.
    TEST(Error(Error::NoError).string() == std::string("NoError"));
    TEST(Error(Error::MaxArgumentsError).string() == std::string("MaxArgumentsError"));
    TEST(Error(Error::DetachedPendingReply).string() == std::string("DetachedPendingReply"));
    TEST(Error(Error::MaxMessageError).string() == std::string("MaxMessageError"));
    TEST(Error(Error::AuthenticationFailed).string() == std::string("AuthenticationFailed"));
    TEST(Error(Error::MaxConnectionError).string() == std::string("MaxConnectionError"));

    // These are technically undefined behavior due to invalid static_casts to nonexistent enum value...
    // but let's try to check test invalid index detection a little anyway.
    TEST(Error(static_cast<Error::Code>(-1)).string() == std::string("<unknown error code>"));
    TEST(Error(static_cast<Error::Code>(-1)).string() == std::string("<unknown error code>"));
    TEST(Error(static_cast<Error::Code>(Error::MaxConnectionError + 1)).string() ==
               std::string("<unknown error code>"));
}

int main(int, char *[])
{
    test_errorPropagation();
    test_errorEnumToString();
    std::cout << "Passed!\n";
}

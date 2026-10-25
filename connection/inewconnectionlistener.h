// SPDX-FileCopyrightText: 2017 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef INEWCONNECTIONLISTENER_H
#define INEWCONNECTIONLISTENER_H

#include "export.h"

class Server;

class DFERRY_EXPORT INewConnectionListener
{
public:
    virtual ~INewConnectionListener();
    /// This is called when a new client has connected.
    /// Usually, you want to call server->takeNextConnection() in a loop until it returns nullptr in order
    /// to accept all pending client connections.
    virtual void handleNewConnection(Server *server) = 0;
};

#endif // INEWCONNECTIONLISTENER_H

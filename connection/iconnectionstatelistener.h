// SPDX-FileCopyrightText: 2018 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef ICONNECTIONSTATELISTENER_H
#define ICONNECTIONSTATELISTENER_H

#include "connection.h"

/// An interface to listen for changes of Connection state
class DFERRY_EXPORT IConnectionStateListener
{
public:
    virtual ~IConnectionStateListener();
    /// Reimplement this to get notified about state changes of \p connection
    virtual void handleConnectionChanged(Connection *connection,
                                         Connection::State oldState, Connection::State newState) = 0;
};

#endif // ICONNECTIONSTATELISTENER_H

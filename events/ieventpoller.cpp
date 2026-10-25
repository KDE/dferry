// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef RUNNING_DOXYGEN

#include "ieventpoller_p.h"

IEventPoller::IEventPoller(EventDispatcher *dispatcher)
   : m_dispatcher(dispatcher)
{
}

IEventPoller::~IEventPoller()
{
    m_dispatcher = nullptr;
}

#endif // RUNNING_DOXYGEN

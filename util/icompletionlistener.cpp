// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "icompletionlistener.h"

/** \class ICompletionListener
    Interface to listen to task completion.
*/

ICompletionListener::~ICompletionListener()
{
}

/** \class CompletionFunc
    Listen to task completion with an std::function.
*/

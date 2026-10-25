// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef ICOMPLETIONLISTENER_H
#define ICOMPLETIONLISTENER_H

#include "export.h"

#include <functional>

/// An interface to listen for completion of tasks
class DFERRY_EXPORT ICompletionListener
{
public:
    virtual ~ICompletionListener();
    virtual void handleCompletion(void *task) = 0; ///< Reimplement this to get notified of task completion
};

/// An adapter between tasks that call an ICompletionListener and std::function
class DFERRY_EXPORT CompletionFunc : public ICompletionListener
{
public:
    /// Creates a CompletionFunc that will call \p func when it is notified.
    CompletionFunc(std::function<void(void *)> func) : m_func(func) {}
    ~CompletionFunc() override {}
    /// Reimplemented to call m_func
    void handleCompletion(void *task) override { if (m_func) { m_func(task); } }

    std::function<void(void *)> m_func; ///< This function will be called upon task completion
};

#endif // ICOMPLETIONLISTENER_H

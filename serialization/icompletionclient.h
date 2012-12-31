#ifndef ICOMPLETIONCLIENT_H
#define ICOMPLETIONCLIENT_H

class ICompletionClient
{
public:
    virtual ~ICompletionClient();
    virtual void notifyCompletion(void *task) = 0;
};

#endif // ICOMPLETIONCLIENT_H

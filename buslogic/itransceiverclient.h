#ifndef ITRANSCEIVERCLIENT_H
#define ITRANSCEIVERCLIENT_H

class Message;

class ITransceiverClient
{
public:
    virtual ~ITransceiverClient();
    virtual void messageReceived(Message *m) = 0;
};

#endif // ITRANSCEIVERCLIENT_H

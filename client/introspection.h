#ifndef INTROSPECTION_H
#define INTROSPECTION_H

#include "message.h"

#include <map>
#include <string>
#include <vector>

struct DFERRYCLIENT_EXPORT Argument
{
    std::string name;
    std::string type;
    bool isDirectionOut; // in otherwise
};

struct DFERRYCLIENT_EXPORT Method
{
    Message::Type type; // allowed: MethodCallMessage, SignalMessage
    std::string name;
    std::vector<Argument> arguments;
};

struct DFERRYCLIENT_EXPORT Property
{
    enum Access {
        Invalid = 0,
        Read = 1,
        Write = 2,
        ReadWrite = Read | Write
    };
    std::string name;
    std::string type;
    Access access;
};

struct DFERRYCLIENT_EXPORT Interface
{
    std::string name;
    std::map<std::string, Method> methods;
    std::map<std::string, Property> properties;
};

struct DFERRYCLIENT_EXPORT IntrospectionNode
{
    // IntrospectionNode *parent() const;
    std::string path() const; // returns all parents' names plus this node's like "/grand/parent/this"

    IntrospectionNode *parent; // null for toplevel
    std::string name;
    std::map<std::string, IntrospectionNode *> children; // nodes by name
    std::map<std::string, Interface> interfaces;
};

namespace tinyxml2 { class XMLElement; }
namespace tx2 = tinyxml2;

class DFERRYCLIENT_EXPORT IntrospectionTree
{
public:
    IntrospectionTree();
    ~IntrospectionTree();

    bool mergeXml(const char *documentText, const char *path);
    // do we need this? void removePath(std::string path); / removeNode(IntrospectionNode *);
    IntrospectionNode *rootNode() const;
private:
    IntrospectionNode *findOrCreateParent(const char *path, std::string *leafName = 0);
    void pruneBranch(IntrospectionNode *node);
    void removeNode(IntrospectionNode *node);
    void deleteChildren(IntrospectionNode *node);

    bool addNode(IntrospectionNode *parent, const tx2::XMLElement *el);
    bool addInterface(IntrospectionNode *node, const tx2::XMLElement *el);
    bool addMethod(Interface *iface, const tx2::XMLElement *el, Message::Type messageType);
    bool addArgument(Method *method, const tx2::XMLElement *el, Message::Type messageType);
    bool addProperty(Interface *iface, const tx2::XMLElement *el);

public:

    IntrospectionNode *m_rootNode;
};

#endif // INTROSPECTION_H

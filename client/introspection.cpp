#include "introspection.h"

#include "argumentlist.h"
#include "stringtools.h"

#include <tinyxml2.h>

#include <cassert>
#include <cstring> // for strcmp...
#include <memory> // for auto_ptr - yes it sucks in general, but it's suitable here

namespace tx2 = tinyxml2;

// strcmp()'s return value is easy to misinterpret, especially for equality...
static bool strequal(const char *c1, const char *c2)
{
    return !strcmp(c1, c2);
}

IntrospectionTree::IntrospectionTree()
   : m_rootNode(new IntrospectionNode)
{
    m_rootNode->parent = 0;
    // name stays empty for the root
}

IntrospectionTree::~IntrospectionTree()
{
    delete m_rootNode;
    m_rootNode = 0;
}

bool IntrospectionTree::mergeXml(const char *xmlData, const char *_path)
{
    // TODO use path to replace an empty root node name in the XML document
    // TODO is it OK to add interfaces to the root node? if so, review/test the code to ensure that it works!
    // TODO what about adding interfaces to other existing nodes? (this is not taken care of in current code!)
    // TODO check the path names (absolute / relative according to spec)
    // If things turn out to be complicated, it might be better to first create a detached tree from the XML
    // document, then check if it can be merged without conflicts, then merge it. That requires not rollback.
    tx2::XMLDocument doc;
    if (doc.Parse(xmlData) != tx2::XML_SUCCESS) {
        return false;
    }
    tx2::XMLElement *el = doc.RootElement();
    if (!el || !strequal(el->Name(), "node")) {
        return false;
    }
    std::string path = _path;
    const tx2::XMLAttribute *attr = el->FirstAttribute();
    if (attr && strequal(attr->Name(), "name"))  {
        std::string intrinsicPath = attr->Value();
        if (!path.empty() && path != intrinsicPath) {
            return false;
        }
        path = intrinsicPath;
    }

    // ### Should we do this? if (path.empty()) { path = "/"; }

    std::string leafName;
    IntrospectionNode *parent = findOrCreateParent(path.c_str(), &leafName);
    if (!parent || parent->children.count(leafName)) {
        // the second condition makes sure that we don't overwrite existing nodes
        return false;
    }

    if (!addNode(parent, el)) {
        // TODO undo creation of parent nodes by findOrCreateParent() here

        return false;
    }
    return true;
}

IntrospectionNode *IntrospectionTree::findOrCreateParent(const char *path, std::string *leafName)
{
    cstring csPath(path);
    if (!ArgumentList::isObjectPathValid(csPath)) {
        return 0;
    }
    std::string strPath(path, csPath.length); // prevent another strlen()
    std::vector<std::string> elements = split(strPath, '/', false);

    IntrospectionNode *node = m_rootNode;
    // the leaf node is to be created later, hence we omit the last path element
    for (int i = 0; i < elements.size() - 1; i++) {
        std::map<std::string, IntrospectionNode *>::iterator it = node->children.find(elements[i]);
        if (it != node->children.end()) {
            node = it->second;
        } else {
            IntrospectionNode *newNode = new IntrospectionNode;
            newNode->name = elements[i];
            node->children[elements[i]] = newNode;
            node = newNode;
        }
    }
    if (leafName) {
        if (elements.empty()) {
            leafName->clear();
        } else {
            *leafName = elements.back();
        }
    }
    return node;
}

// careful with this one when not calling it from findOrCreateParent() or mergeXml() - if applied to a node
// that was created earlier, it might delete children that shouldn't be deleted.
void IntrospectionTree::pruneBranch(IntrospectionNode *node)
{
    IntrospectionNode *const parent = node->parent;
    // remove the node itself, including any children
    removeNode(node);

    // remove all now empty parents (except the root node)
    for (node = parent; node != m_rootNode; ) {
        assert(node->parent->children.count(node->name) == 1);
        if (node->children.size() == 1 && node->interfaces.empty()) {
            // we must have deleted that child while ascending; just remove the current node
            IntrospectionNode *exNode = node;
            node = node->parent;
            delete exNode;
        } else {
            // this node has other children, so we are going to keep it and update its children map
            // (we want to do this for the root node, too, so break here and do it after the loop)
            break;
        }
    }
}

void IntrospectionTree::removeNode(IntrospectionNode *node)
{
    std::map<std::string, IntrospectionNode *>::iterator it = node->children.begin();
    for (; it != node->children.end(); ++it) {
        removeNode(it->second);
    }
    delete node;
}

bool IntrospectionTree::addNode(IntrospectionNode *parent, const tx2::XMLElement *el)
{
    const tx2::XMLAttribute *attr = el->FirstAttribute();
    if (!attr || !strequal(attr->Name(), "name") || attr->Next())  {
        return false;
    }

    const bool isRootOfDocument = el == el->GetDocument()->RootElement();

    std::auto_ptr<IntrospectionNode> node(new IntrospectionNode);
    node->parent = parent;
    node->name = attr->Value();

    for (const tx2::XMLElement *child = el->FirstChildElement(); child; child = child->NextSiblingElement()) {
        if (strequal(child->Name(), "node")) {
            if (!addNode(node.get(), child)) {
                return false;
            }
        } else if (strequal(child->Name(), "interface")) {
            if (!addInterface(node.get(), child)) {
                return false;
            }
        }
    }
    parent->children[node->name] = node.release();
    return true;
}

bool IntrospectionTree::addInterface(IntrospectionNode *node, const tx2::XMLElement *el)
{
    const tx2::XMLAttribute *attr = el->FirstAttribute();
    if (!attr || !strequal(attr->Name(), "name") || attr->Next())  {
        return false;
    }

    Interface iface;
    iface.name = attr->Value();

    for (const tx2::XMLElement *child = el->FirstChildElement(); child; child = child->NextSiblingElement()) {
        if (strequal(child->Name(), "method")) {
            if (!addMethod(&iface, child, Message::MethodCallMessage)) {
                return false;
            }
        } else if (strequal(child->Name(), "signal")) {
            if (!addMethod(&iface, child, Message::SignalMessage)) {
                return false;
            }
        } else if (strequal(child->Name(), "property")) {
            if (!addProperty(&iface, child)) {
                return false;
            }
        } else {
            return false;
        }
    }
    node->interfaces[iface.name] = iface;
    return true;
}

bool IntrospectionTree::addMethod(Interface *iface, const tx2::XMLElement *el, Message::Type messageType)
{
    const tx2::XMLAttribute *attr = el->FirstAttribute();
    if (!attr || !strequal(attr->Name(), "name") || attr->Next())  {
        return false;
    }

    Method method;
    method.type = messageType;
    method.name = attr->Value();

    for (const tx2::XMLElement *child = el->FirstChildElement(); child; child = child->NextSiblingElement()) {
        if (strequal(child->Name(), "arg")) {
            if (!addArgument(&method, child, messageType)) {
                return false;
            }
        } else if (strequal(child->Name(), "annotation")) {
            // annotations are allowed, but we don't use them
            continue;
        } else {
            return false;
        }
    }
    iface->methods[method.name] = method;
    return true;
}

bool IntrospectionTree::addArgument(Method *method, const tx2::XMLElement *el, Message::Type messageType)
{
    Argument arg;
    arg.isDirectionOut = messageType == Message::SignalMessage;
    for (const tx2::XMLAttribute *attr = el->FirstAttribute(); attr; attr = attr->Next()) {
        if (strequal(attr->Name(), "name")) {
            arg.name = attr->Value();
        } else if (strequal(attr->Name(), "type")) {
            arg.type = attr->Value();
            // TODO validate (single complete type!)
        } else if (strequal(attr->Name(), "direction")) {
            if (strequal(attr->Value(), "in")) {
                if (messageType == Message::SignalMessage) {
                    return false;
                }
                arg.isDirectionOut = false;
            } else if (strequal(attr->Value(), "out")) {
                arg.isDirectionOut = true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    if (arg.type.empty()) {
        return false;
    }
    method->arguments.push_back(arg);
    return true;
}

bool IntrospectionTree::addProperty(Interface *iface, const tx2::XMLElement *el)
{
    Property prop;
    prop.access = Property::Invalid;
    for (const tx2::XMLAttribute *attr = el->FirstAttribute(); attr; attr = attr->Next()) {
        if (strequal(attr->Name(), "name")) {
            // TODO validate
            prop.name = attr->Value();
        } else if (strequal(attr->Name(), "type")) {
            // TODO validate - don't forget to check it's a single complete type
            prop.type = attr->Value();
        } else if (strequal(attr->Name(), "access")) {
            if (strequal(attr->Value(), "readwrite")) {
                prop.access = Property::ReadWrite;
            } else if (strequal(attr->Value(), "read")) {
                prop.access = Property::Read;
            } else if (strequal(attr->Value(), "write")) {
                prop.access = Property::Write;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }
    if (prop.name.empty() || prop.type.empty() || prop.access == Property::Invalid) {
        return false;
    }
    iface->properties[prop.name] = prop;
    return true;
}

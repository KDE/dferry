/*
   Copyright (C) 2013 Andreas Hartmetz <ahartmetz@gmail.com>

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

#include "introspection.h"

#include "../testutil.h"

#include <tinyxml2.h>

#include <fstream>
#include <string>
#include <iostream>

namespace tx2 = tinyxml2;
using namespace std;

// Extract the doctype declaration to insert into the re-synthesized XML. It could just be
// omitted from the reference results, but I'd rather test with completely "real" data.

static void addDoctypeDecl(tx2::XMLDocument *doc)
{
    tx2::XMLDocument d;
    d.LoadFile(TEST_DATADIR "/introspect1.xml");
    tx2::XMLNode *doctypeDecl = d.FirstChild()->ShallowClone(doc);
    doc->InsertFirstChild(doctypeDecl);
}

static string readFile(const char *filename)
{
    // Dear STL, you must be kidding...
    ifstream ifs(TEST_DATADIR "/introspect1.xml");
    return string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

///////////////////////////////////////////////////////////////////////////
// Functions to turn introspection data structure back into XML

// the first argument can't be a XMLElement because an XMLDocument isn't an XMLElement
static void xmlizeNode(tx2::XMLNode *el, const IntrospectionNode *node);
static void xmlizeInterface(tx2::XMLElement *el, const Interface *iface);
static void xmlizeMethod(tx2::XMLElement *el, const Method *method);
static void xmlizeArgument(tx2::XMLElement *el, const Argument *arg, bool isSignal);
static void xmlizeProperty(tx2::XMLElement *el, const Property *property);

static tx2::XMLElement *addElement(tx2::XMLNode *xmlNode, const char *type, const std::string &name)
{
    tx2::XMLElement *el = xmlNode->GetDocument()->NewElement(type);
    el->SetAttribute("name", name.c_str());
    xmlNode->InsertEndChild(el);
    return el;
}

static void xmlizeNode(tx2::XMLNode *el, const IntrospectionNode *node)
{
    tx2::XMLElement *nodeEl = addElement(el, "node", node->name);

    map<string, Interface>::const_iterator iIt = node->interfaces.begin();
    for (; iIt != node->interfaces.end(); ++iIt) {
        xmlizeInterface(nodeEl, &iIt->second);
    }

    map<string, IntrospectionNode *>::const_iterator nIt = node->children.begin();
    for (; nIt != node->children.end(); ++nIt) {
        xmlizeNode(nodeEl, nIt->second);
    }

}

static void xmlizeInterface(tx2::XMLElement *el, const Interface *iface)
{
    tx2::XMLElement *ifaceEl = addElement(el, "interface", iface->name);

    map<string, Method>::const_iterator mIt = iface->methods.begin();
    for (; mIt != iface->methods.end(); ++mIt) {
        xmlizeMethod(ifaceEl, &mIt->second);
    }

    map<string, Property>::const_iterator pIt = iface->properties.begin();
    for (; pIt != iface->properties.end(); ++pIt) {
        xmlizeProperty(ifaceEl, &pIt->second);
    }
}

static void xmlizeMethod(tx2::XMLElement *el, const Method *method)
{
    bool isSignal = method->type == Message::SignalMessage;
    tx2::XMLElement *methodEl = addElement(el, isSignal ? "signal" : "method", method->name);

    for (const Argument &arg : method->arguments) {
        xmlizeArgument(methodEl, &arg, isSignal);
    }
}

static void xmlizeArgument(tx2::XMLElement *el, const Argument *arg, bool isSignal)
{
    tx2::XMLElement *argEl = addElement(el, "arg", arg->name);
    if (!arg->name.empty()) {
        argEl->SetAttribute("name", arg->name.c_str());
    }
    argEl->SetAttribute("type", arg->type.c_str());
    argEl->SetAttribute("direction", arg->isDirectionOut ? "out" : "in");
}

static void xmlizeProperty(tx2::XMLElement *el, const Property *property)
{
    tx2::XMLElement *propEl = addElement(el, "property", property->name);
    propEl->SetAttribute("type", property->type.c_str());
    propEl->SetAttribute("type", property->type.c_str());

    const char *access = property->access == Property::Read ? "read" :
                         property->access == Property::Write ? "write" : "readwrite";
    propEl->SetAttribute("access", access);
}



///////////////////////////////////////////////////////////////////////////

static void testBasicRoundtrip()
{
    const char *filename = TEST_DATADIR "/introspect1.xml";
    IntrospectionTree tree;
    TEST(tree.mergeXml(readFile(filename).c_str(), ""));

    tx2::XMLDocument doc;
    addDoctypeDecl(&doc);
    xmlizeNode(&doc, tree.m_rootNode);

    {
        tx2::XMLDocument normalized;
        normalized.LoadFile(TEST_DATADIR "/introspect1.xml");
        tx2::XMLPrinter printer;
        normalized.Print(&printer);
        std::cout << printer.CStr() << '\n';
    }

    tx2::XMLPrinter printer;
    doc.Print(&printer);
    std::cout << printer.CStr() << '\n';
}

int main(int argc, char *argv[])
{
    testBasicRoundtrip();
    std::cout << "Passed!\n";
}

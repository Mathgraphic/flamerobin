/*
  Copyright (c) 2004-2010 The FlameRobin Development Team

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


  $Id$

*/

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWindows headers
#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/wfstream.h>
#include <wx/xml/xml.h>

#include <fstream>
#include <sstream>

#include <boost/function.hpp>

#include "config/Config.h"
#include "core/StringUtils.h"
#include "metadata/database.h"
#include "metadata/MetadataItemVisitor.h"
#include "metadata/root.h"
#include "metadata/server.h"
//-----------------------------------------------------------------------------
//! access to the singleton root of the DBH.
Root& getGlobalRoot()
{
    static Root globalRoot;
    return globalRoot;
}
//-----------------------------------------------------------------------------
static const wxString getNodeContent(wxXmlNode* node, const wxString& defvalue)
{
    for (wxXmlNode* n = node->GetChildren(); (n); n = n->GetNext())
    {
        if (n->GetType() == wxXML_TEXT_NODE
            || n->GetType() == wxXML_CDATA_SECTION_NODE)
        {
            return n->GetContent();
        }
    }
    return defvalue;
}
//-----------------------------------------------------------------------------
Root::Root()
    : MetadataItem(ntRoot), fileNameM(wxT("")), unregLocalDatabasesM(0)
{
    setName_(wxT("Home"));
    setChildrenLoaded(true);
}
//-----------------------------------------------------------------------------
void Root::disconnectAllDatabases()
{
    for (SharedServers::iterator its = serversM.begin();
        its != serversM.end(); ++its)
    {
        std::for_each((*its)->begin(), (*its)->end(),
            boost::mem_fn(&Database::disconnect));
    }
}
//-----------------------------------------------------------------------------
Root::~Root()
{
}
//-----------------------------------------------------------------------------
//! loads fr_databases.conf file and:
//! creates server nodes, fills their properties
//! creates database nodes for server nodes, fills their properties
//! returns: false if file cannot be loaded, true otherwise
//
bool Root::load()
{
    wxXmlDocument doc;
    wxFileName fileName = getFileName();
    if (fileName.FileExists())
    {
        wxFileInputStream stream(fileName.GetFullPath());
        if (stream.Ok())
            doc.Load(stream);
    }
    if (!doc.IsOk())
        return false;

    wxXmlNode* xmlr = doc.GetRoot();
    if (xmlr->GetName() != wxT("root"))
        return false;

    SubjectLocker locker(this);
    for (wxXmlNode* xmln = doc.GetRoot()->GetChildren();
        (xmln); xmln = xmln->GetNext())
    {
        if (xmln->GetType() != wxXML_ELEMENT_NODE)
            continue;
        if (xmln->GetName() == wxT("server"))
            parseServer(xmln);
        if (xmln->GetName() == wxT("nextId"))
        {
            wxString value(getNodeContent(xmln, wxEmptyString));
            unsigned long l;
            // nextIdM may have been written already (database id)
            if (!value.IsEmpty() && value.ToULong(&l))
            {
                if (Database::getUIDGeneratorValue() < l)
                    Database::setUIDGeneratorValue(l);
            }
        }
    }
    return true;
}
//-----------------------------------------------------------------------------
bool Root::parseDatabase(SharedServerPtr server, wxXmlNode* xmln)
{
    wxASSERT(server);
    wxASSERT(xmln);
    SharedDatabasePtr database = server->addDatabase();
    SubjectLocker locker(database.get());

    for (xmln = xmln->GetChildren(); (xmln); xmln = xmln->GetNext())
    {
        if (xmln->GetType() != wxXML_ELEMENT_NODE)
            continue;

        wxString value(getNodeContent(xmln, wxEmptyString));
        if (xmln->GetName() == wxT("name"))
            database->setName_(value);
        else if (xmln->GetName() == wxT("path"))
            database->setPath(value);
        else if (xmln->GetName() == wxT("charset"))
            database->setConnectionCharset(value);
        else if (xmln->GetName() == wxT("username"))
            database->setUsername(value);
        else if (xmln->GetName() == wxT("password"))
            database->setRawPassword(value);
        else if (xmln->GetName() == wxT("encrypted") && value == wxT("1"))
            database->getAuthenticationMode().setStoreEncryptedPassword();
        else if (xmln->GetName() == wxT("authentication"))
            database->getAuthenticationMode().setConfigValue(value);
        else if (xmln->GetName() == wxT("role"))
            database->setRole(value);
        else if (xmln->GetName() == wxT("id"))
        {
            unsigned long id;
            if (value.ToULong(&id))
                database->setId(id);
        }
    }

    // make sure the database has an Id before Root::save() is called,
    // otherwise a new Id will be generated then, but the generator value
    // will not be stored because it's at the beginning of the file.
    database->getId();
    // backward compatibility with FR < 0.3.0
    if (database->getName_().IsEmpty())
        database->setName_(database->extractNameFromConnectionString());
    return true;
}
//-----------------------------------------------------------------------------
bool Root::parseServer(wxXmlNode* xmln)
{
    wxASSERT(xmln);
    SharedServerPtr server = addServer();
    SubjectLocker locker(server.get());

    for (xmln = xmln->GetChildren(); (xmln); xmln = xmln->GetNext())
    {
        if (xmln->GetType() != wxXML_ELEMENT_NODE)
            continue;

        wxString value(getNodeContent(xmln, wxEmptyString));
        if (xmln->GetName() == wxT("name"))
            server->setName_(value);
        else if (xmln->GetName() == wxT("host"))
            server->setHostname(value);
        else if (xmln->GetName() == wxT("port"))
            server->setPort(value);
        else if (xmln->GetName() == wxT("database"))
        {
            if (!parseDatabase(server, xmln))
                return false;
        }
    }
    // backward compatibility with FR < 0.3.0
    if (server->getName_().IsEmpty())
        server->setName_(server->getConnectionString());
    server->setChildrenLoaded(true);
    return true;
}
//-----------------------------------------------------------------------------
SharedServerPtr Root::addServer()
{
    SharedServerPtr server(new Server());
    addServer(server);
    return server;
}
//-----------------------------------------------------------------------------
void Root::addServer(SharedServerPtr server)
{
    if (server)
    {
        serversM.push_back(server);
        server->setParent(this);
        notifyObservers();
    }
}
//-----------------------------------------------------------------------------
template<class T>
struct IsSharedPtrTo
{
    T* _t;
public:
    IsSharedPtrTo<T>(T* t) : _t(t) {};
    bool operator()(boost::shared_ptr<T> pt) { return pt.get() == _t; };
};
//-----------------------------------------------------------------------------
void Root::removeServer(Server* server)
{
    IsSharedPtrTo<Server> isThisServer(server);
    SharedServers::iterator itRemove = std::remove_if(serversM.begin(),
        serversM.end(), isThisServer);
    if (itRemove != serversM.end())
    {
        serversM.erase(itRemove, serversM.end());
        if (unregLocalDatabasesM == server)
            unregLocalDatabasesM = 0;
        notifyObservers();
    }
}
//-----------------------------------------------------------------------------
void Root::addUnregisteredDatabase(SharedDatabasePtr database)
{
    // on-demand creation of parent node for unregistered databases
    if (!unregLocalDatabasesM)
    {
        SharedServerPtr server(new Server());
        serversM.push_back(server);
        server->setName_(_("Unregistered local databases"));
        server->setHostname(wxT("localhost"));
        server->setParent(this);

        unregLocalDatabasesM = server.get();
        notifyObservers();
    }

    unregLocalDatabasesM->addDatabase(database);
}
//-----------------------------------------------------------------------------
// helper for Root::save()
void rsAddChildNode(wxXmlNode* parentNode, const wxString nodeName,
    const wxString nodeContent)
{
    if (!nodeContent.IsEmpty())
    {
        wxXmlNode* propn = new wxXmlNode(wxXML_ELEMENT_NODE, nodeName);
        parentNode->AddChild(propn);
        propn->AddChild(new wxXmlNode(wxXML_TEXT_NODE, wxEmptyString,
            nodeContent));
    } 
}
//-----------------------------------------------------------------------------
// browses the server nodes, and their database nodes
// saves everything to fr_databases.conf file
// returns: false if file cannot be opened for writing, true otherwise
//
bool Root::save()
{
    // create directory if it doesn't exist yet.
    wxString dir = wxPathOnly(getFileName());
    if (!wxDirExists(dir))
        wxMkdir(dir);

    wxXmlDocument doc;
#if !wxUSE_UNICODE
    doc.SetFileEncoding(getHtmlCharset());
#endif
    wxXmlNode* rn = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("root"));
    doc.SetRoot(rn);

    rsAddChildNode(rn, wxT("nextId"),
        wxString::Format(wxT("%d"), Database::getUIDGeneratorValue()));

    for (SharedServers::iterator its = serversM.begin();
        its != serversM.end(); ++its)
    {
        // do not save the dummy server node for databases that were opened
        // either via command line switch or via drag and drop
        if ((*its).get() == unregLocalDatabasesM)
            continue;

        wxXmlNode* srvn = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("server"));
        rn->AddChild(srvn);
        
        rsAddChildNode(srvn, wxT("name"), (*its)->getName_());
        rsAddChildNode(srvn, wxT("host"), (*its)->getHostname());
        rsAddChildNode(srvn, wxT("port"), (*its)->getPort());

        for (SharedDatabases::iterator itdb = (*its)->begin();
            itdb != (*its)->end(); ++itdb)
        {
            (*itdb)->resetCredentials();    // clean up eventual extra credentials

            wxXmlNode* dbn = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("database"));
            srvn->AddChild(dbn);

            rsAddChildNode(dbn, wxT("id"), (*itdb)->getId());
            rsAddChildNode(dbn, wxT("name"), (*itdb)->getName_());
            rsAddChildNode(dbn, wxT("path"), (*itdb)->getPath());
            rsAddChildNode(dbn, wxT("charset"), (*itdb)->getConnectionCharset());
            rsAddChildNode(dbn, wxT("username"), (*itdb)->getUsername());
            rsAddChildNode(dbn, wxT("password"), (*itdb)->getRawPassword());
            rsAddChildNode(dbn, wxT("role"), (*itdb)->getRole());
            rsAddChildNode(dbn, wxT("authentication"),
                (*itdb)->getAuthenticationMode().getConfigValue());
        }
    }
    return doc.Save(getFileName());
}
//-----------------------------------------------------------------------------
bool Root::getChildren(std::vector<MetadataItem *>& temp)
{
    if (serversM.empty())
        return false;
    MetadataItemFromShared<Server> getMetadataItem;
    std::transform(serversM.begin(), serversM.end(), std::back_inserter(temp),
        getMetadataItem);
    return true;
}
//-----------------------------------------------------------------------------
SharedServers::iterator Root::begin()
{
    return serversM.begin();
}
//-----------------------------------------------------------------------------
SharedServers::iterator Root::end()
{
    return serversM.end();
}
//-----------------------------------------------------------------------------
SharedServers::const_iterator Root::begin() const
{
    return serversM.begin();
}
//-----------------------------------------------------------------------------
SharedServers::const_iterator Root::end() const
{
    return serversM.end();
}
//-----------------------------------------------------------------------------
void Root::lockChildren()
{
    std::for_each(serversM.begin(), serversM.end(),
        boost::mem_fn(&Server::lockSubject));
}
//-----------------------------------------------------------------------------
void Root::unlockChildren()
{
    std::for_each(serversM.begin(), serversM.end(),
        boost::mem_fn(&Server::unlockSubject));
}
//-----------------------------------------------------------------------------
const wxString Root::getItemPath() const
{
    // Root is root, don't make the path strings any longer than needed.
    return wxT("");
}
//-----------------------------------------------------------------------------
wxString Root::getFileName()
{
    if (fileNameM.empty())
        fileNameM = config().getDBHFileName();
    return fileNameM;
}
//-----------------------------------------------------------------------------
void Root::acceptVisitor(MetadataItemVisitor* visitor)
{
    visitor->visitRoot(*this);
}
//-----------------------------------------------------------------------------

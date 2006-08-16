/*
Copyright (c) 2004, 2005, 2006 The FlameRobin Development Team

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

#include "config/Config.h"
#include "metadata/database.h"
#include "metadata/procedure.h"
#include "metadata/relation.h"
#include "sql/Identifier.h"
#include "sql/IncompleteStatement.h"
#include "sql/MultiStatement.h"
#include "sql/SqlTokenizer.h"
//-----------------------------------------------------------------------------
IncompleteStatement::IncompleteStatement(Database *db, const wxString& sql)
    :databaseM(db), sqlM(sql)
{
}
//-----------------------------------------------------------------------------
// position is offset at which user typed the dot character
wxString IncompleteStatement::getObjectColumns(const wxString& table,
    int position)
{
    MultiStatement ms(sqlM);
    int offset;
    SingleStatement st = ms.getStatementAt(position, &offset);
    if (!st.isValid())
        return wxEmptyString;

    // feed st to tokenizer, find UNION section we're in
    wxString sql = st.getSql();
    SqlTokenizer stok(sql);
    int pstart = 0, pend = sql.length();
    do
    {
        if (stok.getCurrentToken() == kwUNION)
        {
            int upos = stok.getCurrentTokenPosition();
            if (position - offset > upos)    // before cursor position
                pstart = upos;
            if (position - offset < upos)    // after cursor position
            {
                pend = upos;
                break;
            }
        }
    }
    while (stok.nextToken());
    position -= (offset + pstart);
    sql = sql.Mid(pstart, pend-pstart);
    return getColumnsForObject(sql, table, position);
}
//-----------------------------------------------------------------------------
template <class T>
T* IncompleteStatement::findObject(std::multimap<wxString,wxString>& aliases,
    const wxString& alias, NodeType type)
{
    for (std::multimap<wxString, wxString>::iterator i =
        aliases.lower_bound(alias); i != aliases.upper_bound(alias); ++i)
    {
        T* t = dynamic_cast<T *>(databaseM->findByNameAndType(type,
            (*i).second));
        if (t)
            return t;
    }
    // find by NAME in case user doesn't have from/into/etc. clause but
    // is using FULL_RELATION_NAME.column syntax
    Identifier id;
    id.setFromSql(alias);
    return dynamic_cast<T *>(databaseM->findByNameAndType(type, id.get()));
}
//-----------------------------------------------------------------------------
Relation *IncompleteStatement::getCreateTriggerRelation(const wxString& sql)
{
    Relation* r = 0;
    SqlTokenType search[] = { kwCREATE, kwTRIGGER, tkIDENTIFIER, kwFOR };
    SqlTokenizer tokenizer(sql);
    SqlTokenType stt;
    wxString relName;
    while (true)
    {
        int i=0;
        for (; i < 4; ++i)
        {
            stt = tokenizer.getCurrentToken();
            if (!tokenizer.jumpToken(false))
                return 0;
            if (stt != search[i])
                break;
        }
        if (i == 4)     // we have a match
        {
            if (tkIDENTIFIER == tokenizer.getCurrentToken())
            {
                relName = tokenizer.getCurrentTokenString();
                break;
            }
        }
    }
    if (!relName.IsEmpty())
    {
        Identifier id;
        id.setFromSql(relName);
        r = dynamic_cast<Relation *>(databaseM->findByNameAndType(ntTable,
            id.get()));
        if (!r)
        {
            r = dynamic_cast<Relation *>(databaseM->findByNameAndType(ntView,
                id.get()));
        }
    }
    return r;
}
//-----------------------------------------------------------------------------
Relation *IncompleteStatement::getAlterTriggerRelation(const wxString& sql)
{
    Relation* r = 0;
    SqlTokenType search[] = { kwALTER, kwTRIGGER };
    SqlTokenizer tokenizer(sql);
    SqlTokenType stt;
    wxString trigName;
    while (true)
    {
        int i=0;
        for (; i < 2; ++i)
        {
            stt = tokenizer.getCurrentToken();
            if (!tokenizer.jumpToken(false))
                return 0;
            if (stt != search[i])
                break;
        }
        if (i == 2)     // we have a match
        {
            if (tkIDENTIFIER == tokenizer.getCurrentToken())
            {
                trigName = tokenizer.getCurrentTokenString();
                break;
            }
        }
    }
    if (!trigName.IsEmpty())
    {
        Identifier id;
        id.setFromSql(trigName);
        Trigger *t = dynamic_cast<Trigger *>(databaseM->findByNameAndType(
            ntTrigger, id.get()));
        wxString relName;
        if (t && t->getRelation(relName))
        {
            r = dynamic_cast<Relation *>(databaseM->findByNameAndType(ntTable,
                relName));
            if (!r)
            {
                r = dynamic_cast<Relation *>(databaseM->findByNameAndType(
                    ntView, relName));
            }
        }
    }
    return r;
}
//-----------------------------------------------------------------------------
wxString IncompleteStatement::extractBlockAtPosition(const wxString& sql,
    int pos) const
{
    // search for FOR..DO and BEGIN..END blocks
    // find block where cursorPos is and extract the sql from it
    SqlTokenizer tk(sql);
    int start = 0, end = sql.length();
    while (true)
    {
        SqlTokenType stt = tk.getCurrentToken();
        if (stt == tkEOF)
            break;
        int cpos = tk.getCurrentTokenPosition();
        if (stt == kwFOR || stt == kwBEGIN || stt == kwEND || stt == kwDO)
        {
            if (cpos > pos)
            {
                end = cpos;
                break;
            }
            else
                start = cpos;
        }
        tk.jumpToken(false);
    }
    wxString s;
    if (start != 0 || end != (int)sql.length())
        s = sql.Mid(start, end - start);
    else
        s = sql;
    // take the SQL and split by statements
    MultiStatement mst(s);
    return mst.getStatementAt(pos - start).getSql();
}
//-----------------------------------------------------------------------------
wxString IncompleteStatement::getColumnsForObject(const wxString& sql,
    const wxString& objectAlias, int cursorPos)
{
    Relation *r = 0;
    if (objectAlias.Upper() == wxT("OLD") || objectAlias.Upper() == wxT("NEW"))
    {
        r = getCreateTriggerRelation(sql);
        if (!r)
            r = getAlterTriggerRelation(sql);
        if (!r)
            return wxEmptyString;
    }

    std::multimap<wxString, wxString> aliases;
    if (!r)
    {
        SqlTokenizer tokenizer(extractBlockAtPosition(sql, cursorPos));
        SqlTokenType search[] = { kwFROM, kwJOIN, kwUPDATE, kwINSERT };
        while (true)
        {
            SqlTokenType stt = tokenizer.getCurrentToken();
            if (stt == tkEOF)
                break;

            //wxMessageBox(wxString::Format(wxT("Tok: %d, String: %s"), stt,
            //  tokenizer.getCurrentTokenString().c_str()), wxT("TOKEN"));

            // find all [DELETE] FROM, JOIN, UPDATE, INSERT INTO tokens
            for (int i=0; i < sizeof(search)/sizeof(SqlTokenType); ++i)
            {
                if (search[i] == stt)
                {
                    if (stt == kwINSERT)    // find INTO
                    {
                        tokenizer.jumpToken(false);
                        if (kwINTO != tokenizer.getCurrentToken())
                            break;
                    }
                    tokenizer.jumpToken(false);  // table/view/procedure name
                    if (tkIDENTIFIER != tokenizer.getCurrentToken())
                        break;
                    Identifier id;
                    id.setFromSql(tokenizer.getCurrentTokenString());
                    wxString alias;
                    tokenizer.jumpToken(true);
                    if (tkIDENTIFIER == tokenizer.getCurrentToken())
                        alias = tokenizer.getCurrentTokenString();
                    else
                        alias = id.get();
                    //wxMessageBox(id.get(), alias);
                    aliases.insert(std::pair<wxString, wxString>(alias,
                        id.get()));
                    break;
                }
            }
            if (tkEOF == tokenizer.getCurrentToken())
                break;
            tokenizer.jumpToken(false);
        }

        // find TABLE or VIEW in list of ALIASES
        r = findObject<Relation>(aliases, objectAlias, ntTable);
        if (!r)
            r = findObject<Relation>(aliases, objectAlias, ntView);
    }
    std::list<wxString> cols;
    if (r)
    {
        if (r->begin() == r->end())   // no columns, load if needed
        {
            if (config().get(wxT("autoCompleteLoadColumns"), true))
                r->loadColumns();
            else
                return wxEmptyString;
        }
        for (MetadataCollection<Column>::const_iterator c = r->begin();
            c != r->end(); ++c)
        {
            cols.push_back((*c).getName_().Upper());
        }
    }
    else    // find STORED PROCEDURE in list of ALIASES
    {
        Procedure* p = findObject<Procedure>(aliases, objectAlias,
            ntProcedure);
        if (!p) // give up, we couldn't match anything
            return wxEmptyString;
        if (p->begin() == p->end())
        {
            if (config().get(wxT("autoCompleteLoadColumns"), true))
                p->checkAndLoadParameters();
            else
                return wxEmptyString;
        }
        for (MetadataCollection<Parameter>::const_iterator c = p->begin();
            c != p->end(); ++c)
        {
            if ((*c).getParameterType() == ptOutput)
                cols.push_back((*c).getName_().Upper());
        }
    }

    cols.sort();
    wxString columns;
    for (std::list<wxString>::iterator i = cols.begin(); i != cols.end(); ++i)
        columns += (*i) + wxT(" ");
    return columns.Strip();     // remove trailing space
}
//-----------------------------------------------------------------------------
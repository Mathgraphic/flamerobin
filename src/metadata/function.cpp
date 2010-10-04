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

#include <ibpp.h>

#include "core/StringUtils.h"
#include "engine/MetadataLoader.h"
#include "metadata/database.h"
#include "metadata/domain.h"
#include "metadata/function.h"
#include "metadata/MetadataItemVisitor.h"
//-----------------------------------------------------------------------------
Function::Function()
    : MetadataItem(ntFunction), infoLoadedM(false)
{
}
//-----------------------------------------------------------------------------
wxString Function::getCreateSql()
{
    loadInfo();
    wxString ret(wxT("DECLARE EXTERNAL FUNCTION "));
    ret += getQuotedName() + wxT("\n") + paramListM
        + wxT("\nRETURNS ") + retstrM + wxT("\nENTRY_POINT '") + entryPointM
        + wxT("'\nMODULE_NAME '") + libraryNameM + wxT("';\n");
    return ret;
}
//-----------------------------------------------------------------------------
wxString Function::getCreateSqlTemplate() const
{
    return wxT("DECLARE EXTERNAL FUNCTION name [datatype | CSTRING (int) [, datatype | CSTRING (int) ...]]\n")
           wxT("RETURNS {datatype [BY VALUE] | CSTRING (int)} [FREE_IT]\n")
           wxT("ENTRY_POINT 'entryname'\n")
           wxT("MODULE_NAME 'modulename';\n");
}
//-----------------------------------------------------------------------------
const wxString Function::getTypeName() const
{
    return wxT("FUNCTION");
}
//-----------------------------------------------------------------------------
wxString Function::getDropSqlStatement() const
{
    return wxT("DROP EXTERNAL FUNCTION ") + getQuotedName() + wxT(";");
}
//-----------------------------------------------------------------------------
wxString Function::getDefinition()
{
    loadInfo();
    return definitionM;
}
//-----------------------------------------------------------------------------
wxString Function::getLibraryName()
{
    loadInfo();
    return libraryNameM;
}
//-----------------------------------------------------------------------------
wxString Function::getEntryPoint()
{
    loadInfo();
    return entryPointM;
}
//-----------------------------------------------------------------------------
void Function::loadInfo(bool force)
{
    if (infoLoadedM && !force)
        return;

    bool first = true;
    paramListM = wxEmptyString;
    wxString retstr;
    definitionM = getName_() + wxT("(\n");
        
    wxString mechanismNames[] = { wxT("value"), wxT("reference"),
        wxT("descriptor"), wxT("blob descriptor"), wxT("scalar array"),
        wxT("null"), wxEmptyString };
    wxString mechanismDDL[] = { wxT(" BY VALUE "), wxEmptyString,
        wxT(" BY DESCRIPTOR "), wxEmptyString, wxT(" BY SCALAR ARRAY "),
        wxT(" NULL "), wxEmptyString };

    Database* d = getDatabase(wxT("Function::loadInfo"));
    MetadataLoader* loader = d->getMetadataLoader();
    MetadataLoaderTransaction tr(loader);

    IBPP::Statement& st1 = loader->getStatement(
        "SELECT f.RDB$RETURN_ARGUMENT, a.RDB$MECHANISM, a.RDB$ARGUMENT_POSITION, "
        " a.RDB$FIELD_TYPE, a.RDB$FIELD_SCALE, a.RDB$FIELD_LENGTH, a.RDB$FIELD_SUB_TYPE, a.RDB$FIELD_PRECISION,"
        " f.RDB$MODULE_NAME, f.RDB$ENTRYPOINT, c.RDB$CHARACTER_SET_NAME "
        " FROM RDB$FUNCTIONS f"
        " LEFT OUTER JOIN RDB$FUNCTION_ARGUMENTS a ON f.RDB$FUNCTION_NAME = a.RDB$FUNCTION_NAME"
        " LEFT OUTER JOIN RDB$CHARACTER_SETS c ON a.RDB$CHARACTER_SET_ID = c.RDB$CHARACTER_SET_ID"
        " WHERE f.RDB$FUNCTION_NAME = ? "
        " ORDER BY a.RDB$ARGUMENT_POSITION"
    );
    st1->Set(1, wx2std(getName_(), d->getCharsetConverter()));
    st1->Execute();
    while (st1->Fetch())
    {
        short returnarg, mechanism, type, scale, length, subtype, precision, retpos;
        std::string libraryName, entryPoint, charset;
        st1->Get(1, returnarg);
        st1->Get(2, mechanism);
        st1->Get(3, retpos);
        st1->Get(4, type);
        st1->Get(5, scale);
        st1->Get(6, length);
        st1->Get(7, subtype);
        st1->Get(8, precision);
        st1->Get(9, libraryName);
        libraryNameM = std2wx(libraryName).Strip();
        st1->Get(10, entryPoint);
        entryPointM = std2wx(entryPoint).Strip();
        wxString datatype = Domain::dataTypeToString(type, scale,
            precision, subtype, length);
        if (!st1->IsNull(11))
        {
            st1->Get(11, charset);
            wxString chset(std2wx(charset).Strip());
            if (d->getDatabaseCharset() != chset)
                datatype += wxT(" CHARACTER SET ") + chset;
        }
        if (type == 261)    // avoid subtype information for BLOB
            datatype = wxT("blob");

        int mechIndex = (mechanism < 0 ? -mechanism : mechanism);
        if (mechIndex >= (sizeof(mechanismNames)/sizeof(wxString)))
            mechIndex = (sizeof(mechanismNames)/sizeof(wxString)) - 1;
        wxString param = wxT("    ") + datatype + wxT(" by ")
            + mechanismNames[mechIndex];
        if (mechanism < 0)
            param += wxT(" FREE_IT");
        if (returnarg == retpos)    // output
        {
            retstr = param;
            retstrM = datatype + mechanismDDL[mechIndex];
            if (retpos != 0)
            {
                retstrM = wxT("PARAMETER ");
                retstrM << retpos;
                if (!paramListM.IsEmpty())
                    paramListM += wxT(", ");
                paramListM += datatype + mechanismDDL[mechIndex];
            }
        }
        else
        {
            if (first)
                first = false;
            else
                definitionM += wxT(",\n");
            definitionM += param;
            if (!paramListM.IsEmpty())
                paramListM += wxT(", ");
            paramListM += datatype + mechanismDDL[mechIndex];
        }
    }
    definitionM += wxT("\n)\nreturns:\n") + retstr;
    infoLoadedM = true;
    if (force)
        notifyObservers();
}
//-----------------------------------------------------------------------------
void Function::acceptVisitor(MetadataItemVisitor* visitor)
{
    visitor->visitFunction(*this);
}
//-----------------------------------------------------------------------------
// Functions collection
void Functions::acceptVisitor(MetadataItemVisitor* visitor)
{
    visitor->visitFunctions(*this);
}
//-----------------------------------------------------------------------------
void Functions::load(ProgressIndicator* progressIndicator)
{
    Database* db = getDatabase(wxT("Functions::load"));

    std::string stmt = "select rdb$function_name from rdb$functions"
        " where (rdb$system_flag = 0 or rdb$system_flag is null)"
        " order by 1";
    setItems(db, ntFunction, db->loadIdentifiers(progressIndicator, stmt));
}
//-----------------------------------------------------------------------------
void Functions::loadChildren()
{
    load(0);
}
//-----------------------------------------------------------------------------

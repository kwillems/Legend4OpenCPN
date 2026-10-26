// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// Two functions of std::vector<wxString> that the wxWidgets 3.3 DLL lacks,
// for Visual Studio only.
//
// In wxWidgets 3.3 wxArrayString derives from std::vector<wxString>, so in
// every file that sees wx/arrstr.h Visual Studio takes std::vector<wxString>
// from the wxWidgets DLL. That DLL is built with Visual Studio 2015, whose
// standard library has no _Unchecked_begin() and _Unchecked_end(), while
// newer Visual Studio calls them in a range-based for loop over the vector.
// This file does not see wx/arrstr.h, so here they are ordinary functions of
// the plugin, and the linker takes them from here (warning LNK4217).

#include <wx/string.h>

#if defined(_MSC_VER) && wxUSE_STD_CONTAINERS

#ifdef _WX_ARRSTR_H
#error "wx/arrstr.h makes std::vector<wxString> come from the DLL here too"
#endif

#include <vector>

template wxString *std::vector<wxString>::_Unchecked_begin() noexcept;
template const wxString *std::vector<wxString>::_Unchecked_begin() const noexcept;
template wxString *std::vector<wxString>::_Unchecked_end() noexcept;
template const wxString *std::vector<wxString>::_Unchecked_end() const noexcept;

#endif

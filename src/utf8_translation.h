// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#ifndef _UTF8_TRANSLATION_H_
#define _UTF8_TRANSLATION_H_

// Included before every source on Android and Windows (Plugin.cmake). The
// sources are UTF-8, but there wxString reads a char string in the C locale
// or the Windows code page, so "‹  Previous" became "â€¹  Previous" and
// the translation was not found. _() reads its text as UTF-8.
//
// Visual Studio also includes it before the C sources of md4c, since it
// ignores the C++-only condition in Plugin.cmake; those skip it.
#ifdef __cplusplus
#define WXINTL_NO_GETTEXT_MACRO
#include <wx/string.h>
#include <wx/translation.h>

#define _(s) wxGetTranslation(wxString::FromUTF8(s))
#endif

#endif

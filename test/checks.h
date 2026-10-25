// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// The checks of the unit tests. A failed check prints its line and what
// was expected; main() returns non-zero when there is one.

#ifndef _TEST_CHECKS_H_
#define _TEST_CHECKS_H_

#include <cmath>
#include <cstdio>

#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/string.h>

inline int g_checks = 0;
inline int g_failures = 0;

// The name of a source file without its folder.
inline const char *BaseName(const char *path)
{
    const char *name = path;
    for (const char *at = path; *at; ++at) {
        if (*at == '/' || *at == '\\')
            name = at + 1;
    }
    return name;
}

inline void Check(bool ok, const char *what, const char *file, int line)
{
    ++g_checks;
    if (ok)
        return;
    ++g_failures;
    std::printf("%s:%d: failed: %s\n", BaseName(file), line, what);
}

inline void CheckEqual(const wxString &actual, const wxString &expected, const char *what,
                       const char *file, int line)
{
    ++g_checks;
    if (actual == expected)
        return;
    ++g_failures;
    std::printf("%s:%d: %s\n    expected: \"%s\"\n    actual:   \"%s\"\n", BaseName(file),
                line, what, expected.utf8_str().data(), actual.utf8_str().data());
}

inline void CheckNear(double actual, double expected, double tolerance, const char *what,
                      const char *file, int line)
{
    ++g_checks;
    if (std::fabs(actual - expected) <= tolerance)
        return;
    ++g_failures;
    std::printf("%s:%d: %s\n    expected: %g (within %g)\n    actual:   %g\n", BaseName(file),
                line, what, expected, tolerance, actual);
}

#define CHECK(cond) Check((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(actual, expected) \
    CheckEqual((actual), (expected), #actual, __FILE__, __LINE__)
#define CHECK_NEAR(actual, expected, tolerance) \
    CheckNear((actual), (expected), (tolerance), #actual, __FILE__, __LINE__)

// Texts with characters outside ASCII, such as degrees and Morse dots.
inline wxString U(const char *text)
{
    return wxString::FromUTF8(text);
}

// A folder of its own in the temporary folder, for files a test reads;
// removed again with everything in it.
class TempFolder
{
public:
    TempFolder()
    {
        m_path = wxFileName::CreateTempFileName("legend-test");
        wxRemoveFile(m_path);
        wxFileName::Mkdir(m_path);
    }

    ~TempFolder() { wxFileName::Rmdir(m_path, wxPATH_RMDIR_RECURSIVE); }

    const wxString &Path() const { return m_path; }

    // Writes a file in the folder (UTF-8) and returns its path.
    wxString Write(const wxString &name, const wxString &text) const
    {
        const wxString path = m_path + wxFileName::GetPathSeparator() + name;
        wxFile file(path, wxFile::write);
        file.Write(text, wxConvUTF8);
        return path;
    }

private:
    wxString m_path;
};

// The groups of tests, one per file.
void TestLightCharacters();
void TestVhfAreas();
void TestNotePlaces();
void TestSymbolMeanings();
void TestLayerWaypoints();
void TestGeo();

#endif

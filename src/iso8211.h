// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// ISO 8211, the file format of S-57 charts: records made of tagged fields
// with raw contents. Only what reading S-57 needs.

#ifndef _ISO8211_H_
#define _ISO8211_H_

#include <string>
#include <vector>

#include <wx/string.h>

namespace iso8211 {

const unsigned char kFieldTerminator = 0x1E;
const unsigned char kUnitTerminator = 0x1F;

struct Field
{
    std::string tag;
    std::string data; // without the field terminator
};

struct Record
{
    std::vector<Field> fields;

    const Field *Find(const char *tag) const
    {
        for (const Field &field : fields) {
            if (field.tag == tag)
                return &field;
        }
        return nullptr;
    }
};

// The number written in ASCII digits at [at, at + length), or -1 when
// there is something else.
int AsciiInt(const std::string &text, size_t at, size_t length);

// Every record of a file's contents. The first record (the data
// descriptive record) is skipped; S-57 field layouts are fixed and known.
// True only when the whole file could be read: a damaged file, even one
// damaged near the end, is not used at all, so a cell or update is never
// applied in part.
bool ParseRecords(const std::string &data, std::vector<Record> *records);

// ParseRecords() of a file.
bool ReadRecords(const wxString &path, std::vector<Record> *records);

}  // namespace iso8211

#endif

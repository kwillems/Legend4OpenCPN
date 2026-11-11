// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

#include "iso8211.h"

#include <wx/ffile.h>

namespace iso8211 {

int AsciiInt(const std::string &text, size_t at, size_t length)
{
    if (at > text.size() || length > text.size() - at)
        return -1;
    int value = 0;
    for (size_t i = at; i < at + length; ++i) {
        const char c = text[i];
        if (c < '0' || c > '9')
            return -1;
        value = value * 10 + (c - '0');
    }
    return value;
}

namespace {

bool Parse(const std::string &data, std::vector<Record> *records)
{
    size_t pos = 0;
    bool first = true;

    while (pos < data.size()) {
        // What is left must be a whole record: a leader at least.
        if (data.size() - pos < 24)
            return false;

        const int length = AsciiInt(data, pos, 5);
        const int base = AsciiInt(data, pos + 12, 5);
        const int lengthSize = AsciiInt(data, pos + 20, 1);
        const int positionSize = AsciiInt(data, pos + 21, 1);
        const int tagSize = AsciiInt(data, pos + 23, 1);

        // The record lies in the file, and its field area in the record.
        if (length < 24 || base < 24 || base > length || lengthSize <= 0 ||
            positionSize <= 0 || tagSize <= 0 ||
            static_cast<size_t>(length) > data.size() - pos)
            return false;

        const size_t recordEnd = pos + static_cast<size_t>(length);
        const size_t fieldArea = pos + static_cast<size_t>(base);

        if (!first) {
            Record record;
            const size_t entrySize =
                static_cast<size_t>(tagSize + lengthSize + positionSize);
            size_t entry = pos + 24;

            while (entry + entrySize <= fieldArea &&
                   static_cast<unsigned char>(data[entry]) != kFieldTerminator) {
                Field field;
                field.tag = data.substr(entry, tagSize);
                const int fieldLength = AsciiInt(data, entry + tagSize, lengthSize);
                const int fieldPos =
                    AsciiInt(data, entry + tagSize + lengthSize, positionSize);
                entry += entrySize;

                if (fieldLength <= 0 || fieldPos < 0)
                    return false;

                const size_t start = fieldArea + static_cast<size_t>(fieldPos);
                if (start > recordEnd ||
                    static_cast<size_t>(fieldLength) > recordEnd - start)
                    return false;

                field.data = data.substr(start, fieldLength);
                if (!field.data.empty() &&
                    static_cast<unsigned char>(field.data.back()) == kFieldTerminator)
                    field.data.pop_back();
                record.fields.push_back(field);
            }

            records->push_back(record);
        }

        first = false;
        pos = recordEnd;
    }

    return !records->empty();
}

}  // namespace

bool ParseRecords(const std::string &data, std::vector<Record> *records)
{
    records->clear();
    if (Parse(data, records))
        return true;
    records->clear();
    return false;
}

bool ReadRecords(const wxString &path, std::vector<Record> *records)
{
    records->clear();
    wxFFile file(path, "rb");
    if (!file.IsOpened())
        return false;

    std::string data;
    const wxFileOffset size = file.Length();
    if (size <= 24 || size > 512 * 1024 * 1024)
        return false;
    data.resize(static_cast<size_t>(size));
    if (file.Read(&data[0], data.size()) != data.size())
        return false;

    return ParseRecords(data, records);
}

}  // namespace iso8211

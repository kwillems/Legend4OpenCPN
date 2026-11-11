// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// Unit tests of reading ISO 8211 records, the file format of S-57 charts:
// a sound file is read whole, a damaged one not at all and never beyond
// its end.

#include "checks.h"
#include "iso8211.h"

#include <string>
#include <utility>
#include <vector>

namespace {

std::string Digits(size_t value, size_t width)
{
    std::string text = std::to_string(value);
    return std::string(width - text.size(), '0') + text;
}

// A record with these fields: tags of 4, lengths of 3 and positions of 4
// digits, as in S-57 files.
std::string MakeRecord(const std::vector<std::pair<std::string, std::string>> &fields)
{
    std::string directory;
    std::string area;
    for (const auto &field : fields) {
        const std::string data = field.second + '\x1e';
        directory += field.first + Digits(data.size(), 3) + Digits(area.size(), 4);
        area += data;
    }
    directory += '\x1e';
    const size_t base = 24 + directory.size();
    const size_t length = base + area.size();
    const std::string leader =
        Digits(length, 5) + "3LE1 09" + Digits(base, 5) + " ! " + "3404";
    return leader + directory + area;
}

std::string MakeFile()
{
    // The first record describes the fields and is skipped.
    return MakeRecord({{"0000", "descriptive"}}) +
           MakeRecord({{"0001", "1"}, {"FRID", "feature one"}}) +
           MakeRecord({{"0001", "2"}, {"FRID", "feature two"}});
}

void TestSoundFile()
{
    std::vector<iso8211::Record> records;
    CHECK(iso8211::ParseRecords(MakeFile(), &records));
    CHECK(records.size() == 2);
    if (records.size() == 2) {
        const iso8211::Field *frid = records[1].Find("FRID");
        CHECK(frid != nullptr);
        if (frid)
            CHECK(frid->data == "feature two");
    }
}

void TestDamagedFiles()
{
    std::vector<iso8211::Record> records;
    const std::string sound = MakeFile();

    // Cut off in the last record: not a cell with one feature less, but
    // no cell.
    CHECK(!iso8211::ParseRecords(sound.substr(0, sound.size() - 5), &records));
    CHECK(records.empty());

    // A few bytes after the last record.
    CHECK(!iso8211::ParseRecords(sound + "xyz", &records));

    // The field area of a record starts beyond the record (base larger
    // than length): the directory is not read past the file's end.
    std::string damaged = sound;
    const size_t second = MakeRecord({{"0000", "descriptive"}}).size();
    damaged.replace(second + 12, 5, "99999");
    CHECK(!iso8211::ParseRecords(damaged, &records));

    // A field that lies beyond its record.
    damaged = sound;
    damaged.replace(second + 24 + 4 + 3, 4, "0900");
    CHECK(!iso8211::ParseRecords(damaged, &records));

    // A record longer than the file.
    damaged = sound;
    damaged.replace(second, 5, "99999");
    CHECK(!iso8211::ParseRecords(damaged, &records));

    // Letters where a number belongs.
    damaged = sound;
    damaged.replace(second, 5, "12a45");
    CHECK(!iso8211::ParseRecords(damaged, &records));

    // Nothing but a descriptive record, or nothing at all.
    CHECK(!iso8211::ParseRecords(MakeRecord({{"0000", "descriptive"}}), &records));
    CHECK(!iso8211::ParseRecords(std::string(), &records));
}

void TestAsciiInt()
{
    CHECK(iso8211::AsciiInt("00123", 0, 5) == 123);
    CHECK(iso8211::AsciiInt("00123", 3, 2) == 23);
    // Beyond the end of the text is not a number.
    CHECK(iso8211::AsciiInt("00123", 3, 5) == -1);
    CHECK(iso8211::AsciiInt("00123", 9, 1) == -1);
}

}  // namespace

void TestIso8211()
{
    TestSoundFile();
    TestDamagedFiles();
    TestAsciiInt();
}

// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// Unit tests of the class check by name, used for o-charts objects.

#include "checks.h"
#include "type_check.h"

namespace {

struct Root
{
    virtual ~Root() = default;
};
struct Middle : Root
{
};
struct Leaf : Middle
{
};
struct Other
{
    virtual ~Other() = default;
};
struct Unrelated : Root
{
};
// Middle first: a static_cast from Middle reaches it unchanged.
struct BothFirst : Middle, Other
{
};
// Middle second: at an offset, so it does not count.
struct BothSecond : Other, Middle
{
};
struct ViaVirtual : virtual Middle
{
};

using type_check::DerivesFrom;

}  // namespace

void TestTypeCheck()
{
    CHECK(DerivesFrom(typeid(Middle), typeid(Middle)));
    CHECK(DerivesFrom(typeid(Leaf), typeid(Middle)));
    CHECK(DerivesFrom(typeid(Leaf), typeid(Root)));
    CHECK(!DerivesFrom(typeid(Middle), typeid(Leaf)));
    CHECK(!DerivesFrom(typeid(Unrelated), typeid(Middle)));
    CHECK(!DerivesFrom(typeid(Other), typeid(Root)));

    // Of an object, as for o-charts: its dynamic type.
    Leaf leaf;
    Root *root = &leaf;
    CHECK(DerivesFrom(typeid(*root), typeid(Middle)));

#if defined(__GNUC__) || defined(__clang__)
    CHECK(DerivesFrom(typeid(BothFirst), typeid(Middle)));
    CHECK(DerivesFrom(typeid(BothFirst), typeid(Root)));
    CHECK(!DerivesFrom(typeid(BothSecond), typeid(Middle)));
    CHECK(!DerivesFrom(typeid(ViaVirtual), typeid(Middle)));
#endif
}

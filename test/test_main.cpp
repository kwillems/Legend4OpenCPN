// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// The unit tests of the parts of the plugin that do without OpenCPN. Prints
// every failed check and returns non-zero when there is one.

#include "checks.h"

#include <wx/init.h>

int main()
{
    wxInitializer initializer;
    if (!initializer.IsOk()) {
        std::printf("wxWidgets could not be initialised\n");
        return 2;
    }

    TestLightCharacters();
    TestVhfAreas();
    TestNotePlaces();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

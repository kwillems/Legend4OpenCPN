// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Koen Willems
//
// Legend4OpenCPN - legends and notes for OpenCPN.
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2 of the License, or (at your
// option) any later version. See the LICENSE file for details.

// A class check for objects made by another plugin, where dynamic_cast can
// fail although the class is right: each plugin may carry its own copy of
// a class's type information, and some C++ runtimes (OpenCPN on Android)
// compare those by address.

#ifndef _TYPE_CHECK_H_
#define _TYPE_CHECK_H_

#include <cstring>
#include <string>
#include <typeinfo>

namespace type_check {

#if defined(__GNUC__) || defined(__clang__)
// The type information of classes as the Itanium C++ ABI lays it out
// (section 2.9.5), used by every compiler but MSVC. libc++ does not
// declare these classes; the layout is fixed by the ABI.
struct AbiSiClassTypeInfo : std::type_info
{
    const std::type_info *base;
};
struct AbiBaseClassInfo
{
    const std::type_info *base;
    long offsetFlags;  // offset << 8 | public 2 | virtual 1
};
struct AbiVmiClassTypeInfo : std::type_info
{
    unsigned flags;
    unsigned baseCount;
    AbiBaseClassInfo bases[1];
};
#endif

// Whether type is base or derives from it, comparing the names of the
// classes rather than the addresses of their type information. Single and
// multiple inheritance (all but MSVC, where dynamic_cast already compares
// names). Only bases a static_cast reaches unchanged count: public, not
// virtual, at the start of the object.
inline bool DerivesFrom(const std::type_info &type, const std::type_info &base, int depth = 0)
{
#if defined(__GNUC__) || defined(__clang__)
    if (std::strcmp(type.name(), base.name()) == 0)
        return true;
    if (depth > 32)
        return false;
    // The kind of type information, from its own type.
    const char *kind = typeid(type).name();
    if (std::strcmp(kind, "N10__cxxabiv120__si_class_type_infoE") == 0) {
        const auto &single = static_cast<const AbiSiClassTypeInfo &>(type);
        return single.base && DerivesFrom(*single.base, base, depth + 1);
    }
    if (std::strcmp(kind, "N10__cxxabiv121__vmi_class_type_infoE") == 0) {
        const auto &multiple = static_cast<const AbiVmiClassTypeInfo &>(type);
        for (unsigned i = 0; i < multiple.baseCount && i < 64; ++i) {
            const AbiBaseClassInfo &entry = multiple.bases[i];
            const bool isPublic = (entry.offsetFlags & 2) != 0;
            const bool isVirtual = (entry.offsetFlags & 1) != 0;
            const long offset = entry.offsetFlags >> 8;
            if (entry.base && isPublic && !isVirtual && offset == 0 &&
                DerivesFrom(*entry.base, base, depth + 1))
                return true;
        }
    }
#else
    (void)type;
    (void)base;
    (void)depth;
#endif
    return false;
}

// The classes of type and their bases as text, for the log: "name (kind)
// < base ...". Empty parts where the type information cannot be read.
inline std::string Describe(const std::type_info &type, int depth = 0)
{
    std::string text = type.name();
#if defined(__GNUC__) || defined(__clang__)
    const char *kind = typeid(type).name();
    text += " (";
    text += kind;
    text += ")";
    if (depth > 8)
        return text;
    if (std::strcmp(kind, "N10__cxxabiv120__si_class_type_infoE") == 0) {
        const auto &single = static_cast<const AbiSiClassTypeInfo &>(type);
        if (single.base)
            text += " < " + Describe(*single.base, depth + 1);
    }
    else if (std::strcmp(kind, "N10__cxxabiv121__vmi_class_type_infoE") == 0) {
        const auto &multiple = static_cast<const AbiVmiClassTypeInfo &>(type);
        for (unsigned i = 0; i < multiple.baseCount && i < 8; ++i) {
            const AbiBaseClassInfo &entry = multiple.bases[i];
            text += " < [" + std::to_string(entry.offsetFlags) + "] ";
            if (entry.base)
                text += Describe(*entry.base, depth + 1);
        }
    }
#endif
    return text;
}

}  // namespace type_check

#endif

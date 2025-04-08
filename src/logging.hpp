// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 The Foundry Visionmongers Ltd
#pragma once

#include <algorithm>
#include <iterator>
#include <string>
#include <vector>

#include <FnAsset/plugin/FnAsset.h>
#include <FnAttribute/FnAttribute.h>
#include <FnLogging/FnLogging.h>
#include <pystring/pystring.h>

FnLogSetup("OpenAssetIO");

namespace logging
{
/**
 * Convert a StringMap to a single string.
 *
 * Format is Python dict-like, e.g. "{'a': 'b', 'c': 'd'}".
 */
inline std::string toString(const FnKat::Asset::StringMap& stringMap)
{
    std::string out;
    out += "{";
    std::vector<std::string> keysAndValues;
    std::transform(cbegin(stringMap),
                   cend(stringMap),
                   back_inserter(keysAndValues),
                   [](const auto& keyAndValue)
                   {
                       std::string keyAndValueStr = "'";
                       keyAndValueStr += keyAndValue.first;
                       keyAndValueStr += "': '";
                       keyAndValueStr += keyAndValue.second;
                       keyAndValueStr += "'";
                       return keyAndValueStr;
                   });
    out += pystring::join(", ", keysAndValues);
    out += "}";
    return out;
}

/**
 * Convert a StringVector to a single string.
 *
 * Format is Python list-like, e.g. "['a', 'b', 'c']".
 */
inline std::string toString(const FnKat::Asset::StringVector& stringVec)
{
    std::string out;
    out += "[";
    std::vector<std::string> values;
    std::transform(cbegin(stringVec),
                   cend(stringVec),
                   back_inserter(values),
                   [](const auto& value)
                   {
                       std::string valueStr = "'";
                       valueStr += value;
                       valueStr += "'";
                       return valueStr;
                   });
    out += pystring::join(", ", values);
    out += "]";
    return out;
}

/**
 * Convert a char array to a string, unmodified.
 *
 * Used for non-value strings (e.g. function names) passed as string
 * literals when logging.
 */
inline std::string toString(const char* str)
{
    return str;
}

/**
 * Surround a string in single quotes.
 *
 * Used for string values (e.g. function parameters) when logging.
 */
inline std::string toString(const std::string& str)
{
    std::string out;
    out += "'";
    out += str;
    out += "'";
    return out;
}

inline std::string toString(const FnAttribute::GroupAttribute& attr)
{
    return attr.getXML();
}

/**
 * Fallback string conversion using std::to_string.
 *
 * E.g. float, int, bool.
 */
template <class T>
std::string toString(const T& val)
{
    return std::to_string(val);
}

/**
 * Convert all parameters to a string representation and concatenate
 * them into a single string.
 */
template <typename... Ts>
std::string concatAsStr(const Ts&... vals)
{
    std::string out;
    // NOLINTNEXTLINE(*-bounds-array-to-pointer-decay)
    (out += ... += toString(vals));
    return out;
}
}  // namespace logging

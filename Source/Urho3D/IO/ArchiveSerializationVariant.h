//
// Copyright (c) 2017-2020 the rbfx project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#pragma once

#include "../IO/ArchiveSerializationBasic.h"
#include "../Core/Context.h"
#include "../Core/StringUtils.h"
#include "../Core/Variant.h"

namespace Urho3D
{

/// Serialize type of the Variant.
inline void SerializeValue(Archive& archive, const char* name, VariantType& value)
{
    SerializeEnum(archive, name, value, Variant::GetTypeNameList());
}

/// Serialize value of the Variant.
URHO3D_API void SerializeVariantAsType(Archive& archive, const char* name, Variant& value, VariantType variantType);

/// Serialize Variant in existing block.
inline void SerializeVariantInBlock(Archive& archive, Variant& value)
{
    VariantType variantType = value.GetType();
    SerializeValue(archive, "type", variantType);
    SerializeVariantAsType(archive, "value", value, variantType);
}

/// Serialize Variant.
inline void SerializeValue(Archive& archive, const char* name, Variant& value)
{
    ArchiveBlock block = archive.OpenUnorderedBlock(name);
    SerializeVariantInBlock(archive, value);
}

/// Serialize variant types.
/// @{
URHO3D_API void SerializeValue(Archive& archive, const char* name, StringVector& value);
URHO3D_API void SerializeValue(Archive& archive, const char* name, VariantVector& value);
URHO3D_API void SerializeValue(Archive& archive, const char* name, VariantMap& value);
URHO3D_API void SerializeValue(Archive& archive, const char* name, ResourceRef& value);
URHO3D_API void SerializeValue(Archive& archive, const char* name, ResourceRefList& value);
/// @}

}

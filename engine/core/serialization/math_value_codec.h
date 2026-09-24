#pragma once

#include "math/matrix3.h"
#include "math/matrix4.h"
#include "math/quaternion.h"
#include "math/transform.h"
#include "math/vector2.h"
#include "math/vector3.h"
#include "math/vector4.h"
#include "serialization/value_codec.h"

namespace toy3d
{
    ValueStatus encode_value(ValueWriter& writer, const Vector2& value);
    ValueStatus decode_value(ValueReader& reader, Vector2& value);
    ValueStatus encode_value(ValueWriter& writer, const Vector3& value);
    ValueStatus decode_value(ValueReader& reader, Vector3& value);
    ValueStatus encode_value(ValueWriter& writer, const Vector4& value);
    ValueStatus decode_value(ValueReader& reader, Vector4& value);
    ValueStatus encode_value(ValueWriter& writer, const Matrix3& value);
    ValueStatus decode_value(ValueReader& reader, Matrix3& value);
    ValueStatus encode_value(ValueWriter& writer, const Matrix4& value);
    ValueStatus decode_value(ValueReader& reader, Matrix4& value);
    ValueStatus encode_value(ValueWriter& writer, const Quaternion& value);
    ValueStatus decode_value(ValueReader& reader, Quaternion& value);
    ValueStatus encode_value(ValueWriter& writer, const Transform& value);
    ValueStatus decode_value(ValueReader& reader, Transform& value);
} // namespace toy3d

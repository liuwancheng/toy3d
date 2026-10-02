#include "serialization/math_value_codec.h"

namespace toy3d
{
    namespace
    {
        ValueStatus write_floats(ValueWriter& writer, const float* values, std::size_t count)
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                const ValueStatus status = writer.write_float32(values[index]);
                if (!status.succeeded())
                {
                    return status;
                }
            }
            return ValueStatus::success();
        }

        ValueStatus read_floats(ValueReader& reader, float* values, std::size_t count)
        {
            for (std::size_t index = 0; index < count; ++index)
            {
                const ValueStatus status = reader.read_float32(values[index]);
                if (!status.succeeded())
                {
                    return status;
                }
            }
            return ValueStatus::success();
        }
    } // namespace

    ValueStatus encode_value(ValueWriter& writer, const Vector2& value)
    {
        return write_floats(writer, value.data(), 2);
    }

    ValueStatus decode_value(ValueReader& reader, Vector2& value)
    {
        Vector2 candidate;
        const ValueStatus status = read_floats(reader, candidate.data(), 2);
        if (status.succeeded())
        {
            value = candidate;
        }
        return status;
    }

    ValueStatus encode_value(ValueWriter& writer, const Vector3& value)
    {
        return write_floats(writer, value.data(), 3);
    }

    ValueStatus decode_value(ValueReader& reader, Vector3& value)
    {
        Vector3 candidate;
        const ValueStatus status = read_floats(reader, candidate.data(), 3);
        if (status.succeeded())
        {
            value = candidate;
        }
        return status;
    }

    ValueStatus encode_value(ValueWriter& writer, const Vector4& value)
    {
        return write_floats(writer, value.data(), 4);
    }

    ValueStatus decode_value(ValueReader& reader, Vector4& value)
    {
        Vector4 candidate;
        const ValueStatus status = read_floats(reader, candidate.data(), 4);
        if (status.succeeded())
        {
            value = candidate;
        }
        return status;
    }

    ValueStatus encode_value(ValueWriter& writer, const Matrix3& value)
    {
        return write_floats(writer, value.data(), Matrix3::k_element_count);
    }

    ValueStatus decode_value(ValueReader& reader, Matrix3& value)
    {
        Matrix3 candidate;
        const ValueStatus status = read_floats(reader, candidate.data(), Matrix3::k_element_count);
        if (status.succeeded())
        {
            value = candidate;
        }
        return status;
    }

    ValueStatus encode_value(ValueWriter& writer, const Matrix4& value)
    {
        return write_floats(writer, value.data(), Matrix4::k_element_count);
    }

    ValueStatus decode_value(ValueReader& reader, Matrix4& value)
    {
        Matrix4 candidate;
        const ValueStatus status = read_floats(reader, candidate.data(), Matrix4::k_element_count);
        if (status.succeeded())
        {
            value = candidate;
        }
        return status;
    }

    ValueStatus encode_value(ValueWriter& writer, const Quaternion& value)
    {
        return write_floats(writer, value.data(), 4);
    }

    ValueStatus decode_value(ValueReader& reader, Quaternion& value)
    {
        Quaternion candidate;
        const ValueStatus status = read_floats(reader, candidate.data(), 4);
        if (status.succeeded())
        {
            value = candidate;
        }
        return status;
    }

    ValueStatus encode_value(ValueWriter& writer, const Transform& value)
    {
        ValueStatus status = encode_value(writer, value.translation);
        if (!status.succeeded())
        {
            return status;
        }
        status = encode_value(writer, value.rotation);
        if (!status.succeeded())
        {
            return status;
        }
        return encode_value(writer, value.scale);
    }

    ValueStatus decode_value(ValueReader& reader, Transform& value)
    {
        Transform candidate;
        ValueStatus status = decode_value(reader, candidate.translation);
        if (!status.succeeded())
        {
            return status;
        }
        status = decode_value(reader, candidate.rotation);
        if (!status.succeeded())
        {
            return status;
        }
        status = decode_value(reader, candidate.scale);
        if (status.succeeded())
        {
            value = candidate;
        }
        return status;
    }
} // namespace toy3d

#include "serialization/value_codec.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include "misc/utf8.h"

namespace toy3d
{
    namespace
    {
        std::uint32_t decode_uint32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
        {
            // The wire format is little endian; do not rely on host endianness
            // or native object layout when decoding dense numeric streams.
            return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
                   (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
                   (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
        }
    } // namespace

    bool ValueStatus::succeeded() const
    {
        return code == ValueErrorCode::None;
    }

    ValueStatus ValueStatus::success()
    {
        return {};
    }

    ValueWriter::ValueWriter(ValueLimits limits) : limits_(limits)
    {
    }

    const std::vector<std::uint8_t>& ValueWriter::bytes() const
    {
        return bytes_;
    }

    ValueLimits ValueWriter::child_limits() const
    {
        ValueLimits child = limits_;
        child.max_depth = depth_ < limits_.max_depth ? limits_.max_depth - depth_ : 0;
        return child;
    }

    const std::string& ValueWriter::property_path() const
    {
        return property_path_;
    }

    ValueStatus ValueWriter::failure(ValueErrorCode code, const char* message) const
    {
        return error(code, message);
    }

    void ValueWriter::set_property_path(std::string path)
    {
        property_path_ = std::move(path);
    }

    ValueStatus ValueWriter::error(ValueErrorCode code, const char* message) const
    {
        return {code, bytes_.size(), property_path_, message};
    }

    ValueStatus ValueWriter::enter_depth()
    {
        if (depth_ >= limits_.max_depth)
        {
            return error(ValueErrorCode::DepthExceeded, "value nesting depth exceeds limit");
        }
        ++depth_;
        return ValueStatus::success();
    }

    void ValueWriter::leave_depth()
    {
        if (depth_ != 0)
        {
            --depth_;
        }
    }

    ValueStatus ValueWriter::write_unsigned(std::uint64_t value, std::size_t width)
    {
        if (bytes_.size() > limits_.max_bytes || width > limits_.max_bytes - bytes_.size())
        {
            return error(ValueErrorCode::TooLarge, "encoded value exceeds byte limit");
        }
        for (std::size_t index = 0; index < width; ++index)
        {
            bytes_.push_back(static_cast<std::uint8_t>((value >> (index * 8u)) & 0xffu));
        }
        return ValueStatus::success();
    }

    ValueStatus ValueWriter::write_bool(bool value)
    {
        return write_unsigned(value ? 1u : 0u, 1);
    }

    ValueStatus ValueWriter::write_int8(std::int8_t value)
    {
        return write_unsigned(static_cast<std::uint64_t>(value), 1);
    }

    ValueStatus ValueWriter::write_uint8(std::uint8_t value)
    {
        return write_unsigned(value, 1);
    }

    ValueStatus ValueWriter::write_int16(std::int16_t value)
    {
        return write_unsigned(static_cast<std::uint64_t>(value), 2);
    }

    ValueStatus ValueWriter::write_uint16(std::uint16_t value)
    {
        return write_unsigned(value, 2);
    }

    ValueStatus ValueWriter::write_int32(std::int32_t value)
    {
        return write_unsigned(static_cast<std::uint64_t>(value), 4);
    }

    ValueStatus ValueWriter::write_uint32(std::uint32_t value)
    {
        return write_unsigned(value, 4);
    }

    ValueStatus ValueWriter::write_int64(std::int64_t value)
    {
        return write_unsigned(static_cast<std::uint64_t>(value), 8);
    }

    ValueStatus ValueWriter::write_uint64(std::uint64_t value)
    {
        return write_unsigned(value, 8);
    }

    ValueStatus ValueWriter::write_float32(float value)
    {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
                      "Asset float32 requires IEEE 754 binary32");
        if (!std::isfinite(value))
        {
            return error(ValueErrorCode::InvalidValue, "float32 must be finite");
        }
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return write_uint32(bits);
    }

    ValueStatus ValueWriter::write_float64(double value)
    {
        static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
                      "Asset float64 requires IEEE 754 binary64");
        if (!std::isfinite(value))
        {
            return error(ValueErrorCode::InvalidValue, "float64 must be finite");
        }
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        return write_uint64(bits);
    }

    ValueStatus ValueWriter::write_utf8(const std::string& value)
    {
        if (!is_valid_utf8(value))
        {
            return error(ValueErrorCode::InvalidUtf8, "string is not valid UTF-8");
        }
        if (value.size() > limits_.max_string_bytes || value.size() > std::numeric_limits<std::uint32_t>::max() ||
            bytes_.size() > limits_.max_bytes || limits_.max_bytes - bytes_.size() < 4 ||
            value.size() > limits_.max_bytes - bytes_.size() - 4)
        {
            return error(ValueErrorCode::TooLarge, "string exceeds length or byte limit");
        }
        const ValueStatus length = write_uint32(static_cast<std::uint32_t>(value.size()));
        if (!length.succeeded())
        {
            return length;
        }
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return ValueStatus::success();
    }

    ValueStatus ValueWriter::write_array_length(std::uint32_t count)
    {
        if (count > limits_.max_array_elements)
        {
            return error(ValueErrorCode::TooLarge, "array exceeds element limit");
        }
        return write_uint32(count);
    }

    ValueStatus ValueWriter::write_blob(const std::vector<std::uint8_t>& value)
    {
        if (value.size() > std::numeric_limits<std::uint32_t>::max() || bytes_.size() > limits_.max_bytes ||
            limits_.max_bytes - bytes_.size() < 4 || value.size() > limits_.max_bytes - bytes_.size() - 4)
        {
            return error(ValueErrorCode::TooLarge, "blob exceeds byte limit");
        }
        const ValueStatus length = write_uint32(static_cast<std::uint32_t>(value.size()));
        if (!length.succeeded())
        {
            return length;
        }
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return ValueStatus::success();
    }

    ValueReader::ValueReader(const std::vector<std::uint8_t>& bytes, ValueLimits limits)
        : bytes_(bytes), limits_(limits)
    {
    }

    std::size_t ValueReader::offset() const
    {
        return offset_;
    }

    bool ValueReader::at_end() const
    {
        return offset_ == bytes_.size();
    }

    ValueLimits ValueReader::child_limits() const
    {
        ValueLimits child = limits_;
        child.max_depth = depth_ < limits_.max_depth ? limits_.max_depth - depth_ : 0;
        return child;
    }

    const std::string& ValueReader::property_path() const
    {
        return property_path_;
    }

    ValueStatus ValueReader::failure(ValueErrorCode code, const char* message) const
    {
        return error(code, message);
    }

    void ValueReader::set_property_path(std::string path)
    {
        property_path_ = std::move(path);
    }

    ValueStatus ValueReader::error(ValueErrorCode code, const char* message) const
    {
        return {code, offset_, property_path_, message};
    }

    ValueStatus ValueReader::enter_depth()
    {
        if (depth_ >= limits_.max_depth)
        {
            return error(ValueErrorCode::DepthExceeded, "value nesting depth exceeds limit");
        }
        ++depth_;
        return ValueStatus::success();
    }

    void ValueReader::leave_depth()
    {
        if (depth_ != 0)
        {
            --depth_;
        }
    }

    ValueStatus ValueReader::read_unsigned(std::size_t width, std::uint64_t& value)
    {
        if (bytes_.size() > limits_.max_bytes)
        {
            return error(ValueErrorCode::TooLarge, "input exceeds byte limit");
        }
        if (offset_ > bytes_.size() || width > bytes_.size() - offset_)
        {
            return error(ValueErrorCode::Truncated, "input ends inside value");
        }
        std::uint64_t decoded = 0;
        for (std::size_t index = 0; index < width; ++index)
        {
            decoded |= static_cast<std::uint64_t>(bytes_[offset_ + index]) << (index * 8u);
        }
        offset_ += width;
        value = decoded;
        return ValueStatus::success();
    }

    ValueStatus ValueReader::read_signed(std::size_t width, std::int64_t& value)
    {
        std::uint64_t bits = 0;
        const ValueStatus status = read_unsigned(width, bits);
        if (!status.succeeded())
        {
            return status;
        }
        if (width < 8 && (bits & (std::uint64_t{1} << (width * 8u - 1u))) != 0)
        {
            bits |= (~std::uint64_t{0}) << (width * 8u);
        }
        // Supported targets use two's-complement integers; copying sign-extended
        // bits avoids an implementation-defined unsigned-to-signed cast.
        std::memcpy(&value, &bits, sizeof(value));
        return ValueStatus::success();
    }

    ValueStatus ValueReader::read_bool(bool& value)
    {
        std::uint64_t decoded = 0;
        const ValueStatus status = read_unsigned(1, decoded);
        if (!status.succeeded())
        {
            return status;
        }
        if (decoded > 1)
        {
            return error(ValueErrorCode::InvalidValue, "bool must be 0 or 1");
        }
        value = decoded == 1;
        return ValueStatus::success();
    }

    ValueStatus ValueReader::read_int8(std::int8_t& value)
    {
        std::int64_t decoded = 0;
        const ValueStatus status = read_signed(1, decoded);
        if (status.succeeded())
        {
            value = static_cast<std::int8_t>(decoded);
        }
        return status;
    }

    ValueStatus ValueReader::read_uint8(std::uint8_t& value)
    {
        // Dense mesh streams contain millions of scalars. Check the same limits
        // directly to avoid constructing intermediate status strings per byte.
        if (bytes_.size() > limits_.max_bytes)
        {
            return error(ValueErrorCode::TooLarge, "input exceeds byte limit");
        }
        if (offset_ >= bytes_.size())
        {
            return error(ValueErrorCode::Truncated, "input ends inside value");
        }
        value = bytes_[offset_++];
        return ValueStatus::success();
    }

    ValueStatus ValueReader::read_int16(std::int16_t& value)
    {
        std::int64_t decoded = 0;
        const ValueStatus status = read_signed(2, decoded);
        if (status.succeeded())
        {
            value = static_cast<std::int16_t>(decoded);
        }
        return status;
    }

    ValueStatus ValueReader::read_uint16(std::uint16_t& value)
    {
        std::uint64_t decoded = 0;
        const ValueStatus status = read_unsigned(2, decoded);
        if (status.succeeded())
        {
            value = static_cast<std::uint16_t>(decoded);
        }
        return status;
    }

    ValueStatus ValueReader::read_int32(std::int32_t& value)
    {
        std::int64_t decoded = 0;
        const ValueStatus status = read_signed(4, decoded);
        if (status.succeeded())
        {
            value = static_cast<std::int32_t>(decoded);
        }
        return status;
    }

    ValueStatus ValueReader::read_uint32(std::uint32_t& value)
    {
        if (bytes_.size() > limits_.max_bytes)
        {
            return error(ValueErrorCode::TooLarge, "input exceeds byte limit");
        }
        if (offset_ > bytes_.size() || 4u > bytes_.size() - offset_)
        {
            return error(ValueErrorCode::Truncated, "input ends inside value");
        }
        value = decode_uint32(bytes_, offset_);
        offset_ += 4u;
        return ValueStatus::success();
    }

    ValueStatus ValueReader::read_int64(std::int64_t& value)
    {
        return read_signed(8, value);
    }

    ValueStatus ValueReader::read_uint64(std::uint64_t& value)
    {
        return read_unsigned(8, value);
    }

    ValueStatus ValueReader::read_float32(float& value)
    {
        if (bytes_.size() > limits_.max_bytes)
        {
            return error(ValueErrorCode::TooLarge, "input exceeds byte limit");
        }
        if (offset_ > bytes_.size() || 4u > bytes_.size() - offset_)
        {
            return error(ValueErrorCode::Truncated, "input ends inside value");
        }
        const std::uint32_t bits = decode_uint32(bytes_, offset_);
        offset_ += 4u;
        float decoded = 0.0f;
        std::memcpy(&decoded, &bits, sizeof(decoded));
        if (!std::isfinite(decoded))
        {
            return error(ValueErrorCode::InvalidValue, "float32 must be finite");
        }
        value = decoded;
        return ValueStatus::success();
    }

    ValueStatus ValueReader::read_float64(double& value)
    {
        std::uint64_t bits = 0;
        const ValueStatus status = read_uint64(bits);
        if (!status.succeeded())
        {
            return status;
        }
        double decoded = 0.0;
        std::memcpy(&decoded, &bits, sizeof(decoded));
        if (!std::isfinite(decoded))
        {
            return error(ValueErrorCode::InvalidValue, "float64 must be finite");
        }
        value = decoded;
        return ValueStatus::success();
    }

    ValueStatus ValueReader::read_utf8(std::string& value)
    {
        std::uint32_t length = 0;
        const ValueStatus status = read_uint32(length);
        if (!status.succeeded())
        {
            return status;
        }
        if (length > limits_.max_string_bytes)
        {
            return error(ValueErrorCode::TooLarge, "string exceeds length limit");
        }
        if (length > bytes_.size() - offset_)
        {
            return error(ValueErrorCode::Truncated, "input ends inside string");
        }
        const std::string decoded(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                                  bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + length));
        if (!is_valid_utf8(decoded))
        {
            return error(ValueErrorCode::InvalidUtf8, "string is not valid UTF-8");
        }
        offset_ += length;
        value = decoded;
        return ValueStatus::success();
    }

    ValueStatus ValueReader::read_array_length(std::uint32_t& count)
    {
        std::uint32_t decoded = 0;
        const ValueStatus status = read_uint32(decoded);
        if (!status.succeeded())
        {
            return status;
        }
        if (decoded > limits_.max_array_elements)
        {
            return error(ValueErrorCode::TooLarge, "array exceeds element limit");
        }
        count = decoded;
        return ValueStatus::success();
    }

    ValueStatus ValueReader::read_blob(std::vector<std::uint8_t>& value)
    {
        std::uint32_t length = 0;
        const ValueStatus status = read_uint32(length);
        if (!status.succeeded())
        {
            return status;
        }
        if (length > bytes_.size() - offset_)
        {
            return error(ValueErrorCode::Truncated, "input ends inside blob");
        }
        std::vector<std::uint8_t> decoded(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                                          bytes_.begin() + static_cast<std::ptrdiff_t>(offset_ + length));
        offset_ += length;
        value = std::move(decoded);
        return ValueStatus::success();
    }
} // namespace toy3d

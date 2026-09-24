#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace toy3d
{
    enum class ValueErrorCode
    {
        None,
        Truncated,
        TooLarge,
        InvalidValue,
        InvalidUtf8,
        DepthExceeded,
        UnknownOptionalField
    };

    struct ValueStatus
    {
        ValueErrorCode code = ValueErrorCode::None;
        std::size_t offset = 0;
        std::string property_path;
        std::string message;

        bool succeeded() const;
        static ValueStatus success();
    };

    struct ValueLimits
    {
        std::size_t max_bytes = 64u * 1024u * 1024u;
        std::size_t max_string_bytes = 1024u * 1024u;
        std::uint32_t max_array_elements = 1000000u;
        std::uint32_t max_depth = 64u;
    };

    class ValueWriter
    {
      public:
        explicit ValueWriter(ValueLimits limits = {});

        const std::vector<std::uint8_t>& bytes() const;
        ValueLimits child_limits() const;
        const std::string& property_path() const;
        ValueStatus failure(ValueErrorCode code, const char* message) const;
        void set_property_path(std::string path);
        ValueStatus enter_depth();
        void leave_depth();

        ValueStatus write_bool(bool value);
        ValueStatus write_int8(std::int8_t value);
        ValueStatus write_uint8(std::uint8_t value);
        ValueStatus write_int16(std::int16_t value);
        ValueStatus write_uint16(std::uint16_t value);
        ValueStatus write_int32(std::int32_t value);
        ValueStatus write_uint32(std::uint32_t value);
        ValueStatus write_int64(std::int64_t value);
        ValueStatus write_uint64(std::uint64_t value);
        ValueStatus write_float32(float value);
        ValueStatus write_float64(double value);
        ValueStatus write_utf8(const std::string& value);
        ValueStatus write_blob(const std::vector<std::uint8_t>& value);
        ValueStatus write_array_length(std::uint32_t count);

      private:
        ValueStatus write_unsigned(std::uint64_t value, std::size_t width);
        ValueStatus error(ValueErrorCode code, const char* message) const;

        ValueLimits limits_;
        std::vector<std::uint8_t> bytes_;
        std::string property_path_;
        std::uint32_t depth_ = 0;
    };

    class ValueReader
    {
      public:
        // The caller owns bytes and must keep it alive for the reader's lifetime.
        ValueReader(const std::vector<std::uint8_t>& bytes, ValueLimits limits = {});

        std::size_t offset() const;
        bool at_end() const;
        ValueLimits child_limits() const;
        const std::string& property_path() const;
        ValueStatus failure(ValueErrorCode code, const char* message) const;
        void set_property_path(std::string path);
        ValueStatus enter_depth();
        void leave_depth();

        ValueStatus read_bool(bool& value);
        ValueStatus read_int8(std::int8_t& value);
        ValueStatus read_uint8(std::uint8_t& value);
        ValueStatus read_int16(std::int16_t& value);
        ValueStatus read_uint16(std::uint16_t& value);
        ValueStatus read_int32(std::int32_t& value);
        ValueStatus read_uint32(std::uint32_t& value);
        ValueStatus read_int64(std::int64_t& value);
        ValueStatus read_uint64(std::uint64_t& value);
        ValueStatus read_float32(float& value);
        ValueStatus read_float64(double& value);
        ValueStatus read_utf8(std::string& value);
        ValueStatus read_blob(std::vector<std::uint8_t>& value);
        ValueStatus read_array_length(std::uint32_t& count);

      private:
        ValueStatus read_unsigned(std::size_t width, std::uint64_t& value);
        ValueStatus read_signed(std::size_t width, std::int64_t& value);
        ValueStatus error(ValueErrorCode code, const char* message) const;

        const std::vector<std::uint8_t>& bytes_;
        ValueLimits limits_;
        std::size_t offset_ = 0;
        std::string property_path_;
        std::uint32_t depth_ = 0;
    };

    // Keeps nesting limits balanced when generated codecs return on a failed field.
    template <typename Stream> class ValueDepthScope
    {
      public:
        explicit ValueDepthScope(Stream& stream) : stream_(stream), status_(stream.enter_depth()) {}
        ~ValueDepthScope()
        {
            if (status_.succeeded())
            {
                stream_.leave_depth();
            }
        }

        ValueDepthScope(const ValueDepthScope&) = delete;
        ValueDepthScope& operator=(const ValueDepthScope&) = delete;

        const ValueStatus& status() const { return status_; }

      private:
        Stream& stream_;
        ValueStatus status_;
    };
} // namespace toy3d

#include "serialization/value_codec.h"
#include "serialization/math_value_codec.h"
#include "serialization/schema_migration.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(1);
        }
    }
} // namespace

int main()
{
    // Fixed wire bytes exercise endian conversion, empty blocks and transactional failures.
    const std::vector<std::uint8_t> block = {0x78, 0x56, 0x34, 0x12, 0xef, 0xcd, 0xab, 0x90};
    toy3d::ValueReader block_reader(block);
    std::uint32_t words[2]{0, 0};
    check(block_reader.read_uint32_array(words, 2).succeeded() && words[0] == 0x12345678u && words[1] == 0x90abcdefu &&
              block_reader.at_end(),
          "bulk uint32 endian conversion failed");
    check(block_reader.read_float32_array(nullptr, 0).succeeded() &&
              block_reader.read_uint32_array(nullptr, 0).succeeded() &&
              block_reader.read_uint8_array(nullptr, 0).succeeded(),
          "empty bulk read requires destination");
    check(block_reader.read_uint32_array(words, 1).code == toy3d::ValueErrorCode::Truncated &&
              words[0] == 0x12345678u && block_reader.offset() == block.size(),
          "truncated bulk read changed state");
    toy3d::ValueReader byte_reader(block);
    std::uint8_t raw[8]{};
    check(byte_reader.read_uint8_array(nullptr, 1).code == toy3d::ValueErrorCode::InvalidValue &&
              byte_reader.offset() == 0 && byte_reader.read_uint8_array(raw, 8).succeeded() &&
              std::vector<std::uint8_t>(raw, raw + 8) == block,
          "bulk byte destination validation failed");
    // Bulk uint16 shares the dense-array contract with uint32: little endian,
    // transactional failure and the same element/byte budgets.
    const std::vector<std::uint8_t> halves = {0x34, 0x12, 0xcd, 0xab};
    toy3d::ValueReader half_reader(halves);
    std::uint16_t half_values[2]{0, 0};
    check(half_reader.read_uint16_array(half_values, 2).succeeded() && half_values[0] == 0x1234u &&
              half_values[1] == 0xabcdu && half_reader.at_end(),
          "bulk uint16 endian conversion failed");
    check(half_reader.read_uint16_array(nullptr, 0).succeeded() &&
              half_reader.read_uint16_array(half_values, 1).code == toy3d::ValueErrorCode::Truncated &&
              half_values[0] == 0x1234u && half_reader.offset() == halves.size(),
          "bulk uint16 empty read or failure state changed the destination");
    const std::vector<std::uint8_t> odd_halves = {0x01, 0x02, 0x03};
    toy3d::ValueReader odd_half_reader(odd_halves);
    check(odd_half_reader.read_uint16_array(half_values, 1).succeeded() && half_values[0] == 0x0201u &&
              odd_half_reader.read_uint16_array(half_values, 1).code == toy3d::ValueErrorCode::Truncated &&
              odd_half_reader.offset() == 2,
          "bulk uint16 misaligned tail was accepted");
    toy3d::ValueReader null_half_reader(halves);
    check(null_half_reader.read_uint16_array(nullptr, 1).code == toy3d::ValueErrorCode::InvalidValue &&
              null_half_reader.offset() == 0,
          "bulk uint16 destination validation failed");
    toy3d::ValueLimits half_limits;
    half_limits.max_array_elements = 1;
    toy3d::ValueReader element_limited_half(halves, half_limits);
    check(element_limited_half.read_uint16_array(half_values, 2).code == toy3d::ValueErrorCode::TooLarge &&
              element_limited_half.offset() == 0,
          "bulk uint16 element limit was ignored");
    const std::vector<std::uint8_t> floats = {0, 0, 0x80, 0x3f, 0, 0, 0x80, 0xbf};
    toy3d::ValueReader float_reader(floats);
    float values[2]{42, 43};
    check(float_reader.read_float32_array(values, 2).succeeded() && values[0] == 1 && values[1] == -1 &&
              float_reader.at_end(),
          "bulk float conversion failed");
    for (const std::uint8_t exponent : {std::uint8_t{0x80}, std::uint8_t{0xc0}})
    {
        const std::vector<std::uint8_t> nonfinite_block = {7, 0, 0, 0x80, 0x3f, 0, 0, exponent, 0x7f};
        toy3d::ValueReader failure_reader(nonfinite_block);
        failure_reader.set_property_path("vertices[7].position");
        std::uint8_t prefix = 0;
        check(failure_reader.read_uint8(prefix).succeeded(), "bulk error prefix fixture failed");
        values[0] = 42;
        values[1] = 43;
        const auto failure = failure_reader.read_float32_array(values, 2);
        check(failure.code == toy3d::ValueErrorCode::InvalidValue && failure.offset == 5 &&
                  failure.property_path == "vertices[7].position" && failure_reader.offset() == 1 && values[0] == 42 &&
                  values[1] == 43,
              "non-finite bulk read partially published data");
    }
    toy3d::ValueLimits block_limits;
    block_limits.max_array_elements = 1;
    toy3d::ValueReader element_limited(block, block_limits);
    check(element_limited.read_uint32_array(words, 2).code == toy3d::ValueErrorCode::TooLarge &&
              element_limited.read_uint8_array(raw, std::numeric_limits<std::size_t>::max()).code ==
                  toy3d::ValueErrorCode::TooLarge &&
              element_limited.offset() == 0,
          "bulk element bounds or count overflow ignored");
    block_limits.max_bytes = 1;
    toy3d::ValueReader byte_limited(block, block_limits);
    check(byte_limited.read_uint8_array(raw, 1).code == toy3d::ValueErrorCode::TooLarge && byte_limited.offset() == 0,
          "bulk total byte limit ignored");

    toy3d::ValueWriter writer;
    check(writer.write_uint16(0x1234u).succeeded(), "uint16 write failed");
    check(writer.write_int32(-2).succeeded(), "int32 write failed");
    check(writer.write_float32(1.0f).succeeded(), "float32 write failed");
    check(writer.write_bool(true).succeeded(), "bool write failed");
    check(writer.write_utf8("A").succeeded(), "UTF-8 write failed");
    const std::vector<std::uint8_t> expected = {0x34u, 0x12u, 0xfeu, 0xffu, 0xffu, 0xffu, 0x00u, 0x00u,
                                                0x80u, 0x3fu, 0x01u, 0x01u, 0x00u, 0x00u, 0x00u, 0x41u};
    check(writer.bytes() == expected, "value bytes are not canonical little endian");

    toy3d::ValueReader reader(expected);
    std::uint16_t uint16_value = 0;
    std::int32_t int32_value = 0;
    float float_value = 0.0f;
    bool bool_value = false;
    std::string text;
    check(reader.read_uint16(uint16_value).succeeded() && uint16_value == 0x1234u, "uint16 roundtrip failed");
    check(reader.read_int32(int32_value).succeeded() && int32_value == -2, "int32 roundtrip failed");
    check(reader.read_float32(float_value).succeeded() && float_value == 1.0f, "float32 roundtrip failed");
    check(reader.read_bool(bool_value).succeeded() && bool_value, "bool roundtrip failed");
    check(reader.read_utf8(text).succeeded() && text == "A" && reader.at_end(), "UTF-8 roundtrip failed");

    toy3d::ValueWriter wide;
    check(wide.write_int8(-128).succeeded() && wide.write_uint8(255).succeeded() &&
              wide.write_int16(-32768).succeeded() && wide.write_uint32(0xffffffffu).succeeded() &&
              wide.write_int64(std::numeric_limits<std::int64_t>::min()).succeeded() &&
              wide.write_uint64(std::numeric_limits<std::uint64_t>::max()).succeeded() &&
              wide.write_float64(-1.5).succeeded(),
          "fixed width value write failed");
    toy3d::ValueReader wide_reader(wide.bytes());
    std::int8_t int8_value = 0;
    std::uint8_t uint8_value = 0;
    std::int16_t int16_value = 0;
    std::uint32_t uint32_value = 0;
    std::int64_t int64_value = 0;
    std::uint64_t uint64_value = 0;
    double double_value = 0.0;
    check(wide_reader.read_int8(int8_value).succeeded() && int8_value == -128 &&
              wide_reader.read_uint8(uint8_value).succeeded() && uint8_value == 255 &&
              wide_reader.read_int16(int16_value).succeeded() && int16_value == -32768 &&
              wide_reader.read_uint32(uint32_value).succeeded() && uint32_value == 0xffffffffu &&
              wide_reader.read_int64(int64_value).succeeded() &&
              int64_value == std::numeric_limits<std::int64_t>::min() &&
              wide_reader.read_uint64(uint64_value).succeeded() &&
              uint64_value == std::numeric_limits<std::uint64_t>::max() &&
              wide_reader.read_float64(double_value).succeeded() && double_value == -1.5 && wide_reader.at_end(),
          "fixed width value roundtrip failed");

    toy3d::ValueWriter invalid_writer;
    invalid_writer.set_property_path("tracks[12].keys[8].time");
    const toy3d::ValueStatus nonfinite = invalid_writer.write_float32(std::numeric_limits<float>::infinity());
    check(nonfinite.code == toy3d::ValueErrorCode::InvalidValue &&
              nonfinite.property_path == "tracks[12].keys[8].time" && invalid_writer.bytes().empty(),
          "non-finite writer error lost field context or changed output");
    check(invalid_writer.write_utf8("\xc0\xaf").code == toy3d::ValueErrorCode::InvalidUtf8,
          "invalid UTF-8 was accepted by writer");

    const std::vector<std::uint8_t> invalid_float = {0x00u, 0x00u, 0x80u, 0x7fu};
    toy3d::ValueReader invalid_float_reader(invalid_float);
    float unchanged = 42.0f;
    check(invalid_float_reader.read_float32(unchanged).code == toy3d::ValueErrorCode::InvalidValue &&
              unchanged == 42.0f,
          "non-finite reader changed caller value");
    const std::vector<std::uint8_t> invalid_text = {0x02u, 0x00u, 0x00u, 0x00u, 0xc0u, 0xafu};
    toy3d::ValueReader invalid_text_reader(invalid_text);
    std::string unchanged_text = "old";
    check(invalid_text_reader.read_utf8(unchanged_text).code == toy3d::ValueErrorCode::InvalidUtf8 &&
              unchanged_text == "old",
          "invalid UTF-8 reader changed caller value");

    const std::vector<std::uint8_t> truncated = {0x01u};
    toy3d::ValueReader truncated_reader(truncated);
    truncated_reader.set_property_path("tracks[12].keys[8].time");
    std::uint32_t unchanged_number = 99u;
    const toy3d::ValueStatus truncated_status = truncated_reader.read_uint32(unchanged_number);
    check(truncated_status.code == toy3d::ValueErrorCode::Truncated && unchanged_number == 99u &&
              truncated_status.property_path == "tracks[12].keys[8].time",
          "truncated value was accepted or changed caller value");
    check(truncated_reader.offset() == 0 &&
              truncated_reader.read_float32(unchanged).code == toy3d::ValueErrorCode::Truncated && unchanged == 42.0f &&
              truncated_reader.offset() == 0,
          "truncated float changed value or consumed input");
    check(truncated_reader.read_uint8(uint8_value).succeeded() && uint8_value == 1 &&
              truncated_reader.read_uint8(uint8_value).code == toy3d::ValueErrorCode::Truncated && uint8_value == 1 &&
              truncated_reader.offset() == 1,
          "byte reader did not preserve its value and cursor at end of input");

    toy3d::ValueLimits limits;
    limits.max_bytes = 4;
    limits.max_string_bytes = 2;
    limits.max_array_elements = 2;
    limits.max_depth = 1;
    toy3d::ValueWriter limited(limits);
    check(limited.write_array_length(3).code == toy3d::ValueErrorCode::TooLarge && limited.bytes().empty(),
          "array limit was ignored");
    check(limited.enter_depth().succeeded() && limited.enter_depth().code == toy3d::ValueErrorCode::DepthExceeded,
          "writer depth limit was ignored");
    limited.leave_depth();
    check(limited.write_utf8("abc").code == toy3d::ValueErrorCode::TooLarge && limited.bytes().empty(),
          "string limit was ignored");
    check(limited.write_uint32(1u).succeeded() && limited.write_uint8(2u).code == toy3d::ValueErrorCode::TooLarge,
          "byte limit was ignored");
    toy3d::ValueReader oversized_reader(expected, limits);
    check(oversized_reader.read_uint16(uint16_value).code == toy3d::ValueErrorCode::TooLarge,
          "reader total byte limit was ignored");
    check(oversized_reader.read_uint8(uint8_value).code == toy3d::ValueErrorCode::TooLarge && uint8_value == 1 &&
              oversized_reader.read_uint32(unchanged_number).code == toy3d::ValueErrorCode::TooLarge &&
              unchanged_number == 99u &&
              oversized_reader.read_float32(unchanged).code == toy3d::ValueErrorCode::TooLarge && unchanged == 42.0f &&
              oversized_reader.offset() == 0,
          "dense scalar readers bypassed byte limits or changed caller state");
    const std::vector<std::uint8_t> too_many = {0x03u, 0x00u, 0x00u, 0x00u};
    toy3d::ValueReader array_reader(too_many, limits);
    std::uint32_t unchanged_count = 7;
    check(array_reader.read_array_length(unchanged_count).code == toy3d::ValueErrorCode::TooLarge &&
              unchanged_count == 7,
          "reader array limit was ignored");
    toy3d::ValueReader depth_reader(too_many, limits);
    check(depth_reader.enter_depth().succeeded() &&
              depth_reader.enter_depth().code == toy3d::ValueErrorCode::DepthExceeded,
          "reader depth limit was ignored");

    toy3d::ValueWriter old_half_extent;
    check(old_half_extent.write_float32(2.0f).succeeded(), "old field fixture failed");
    toy3d::ValueWriter old_schema;
    check(old_schema.write_array_length(1).succeeded() && old_schema.write_utf8("half_extent").succeeded() &&
              old_schema.write_uint8(1u).succeeded() && old_schema.write_blob(old_half_extent.bytes()).succeeded(),
          "old schema fixture failed");
    toy3d::SchemaMigrationRegistry migrations;
    check(migrations.add_step(
              "toy3d.CollisionBox", 1,
              [](toy3d::SchemaFields& fields)
              {
                  toy3d::ValueStatus status = toy3d::rename_schema_field(fields, "half_extent", "half_extents");
                  if (!status.succeeded())
                  {
                      return status;
                  }
                  return toy3d::convert_schema_field(
                      fields, "half_extents",
                      [](const std::vector<std::uint8_t>& old_bytes, std::vector<std::uint8_t>& new_bytes)
                      {
                          toy3d::ValueReader old_reader(old_bytes);
                          float scalar = 0.0f;
                          toy3d::ValueStatus status = old_reader.read_float32(scalar);
                          if (!status.succeeded())
                          {
                              return status;
                          }
                          if (!old_reader.at_end())
                          {
                              return old_reader.failure(toy3d::ValueErrorCode::InvalidValue,
                                                        "old field has trailing bytes");
                          }
                          toy3d::ValueWriter new_writer;
                          status = toy3d::encode_value(new_writer, toy3d::Vector3{scalar, scalar, scalar});
                          if (status.succeeded())
                          {
                              new_bytes = new_writer.bytes();
                          }
                          return status;
                      });
              }),
          "migration registration failed");
    check(!migrations.add_step("toy3d.CollisionBox", 1,
                               [](toy3d::SchemaFields&)
                               {
                                   return toy3d::ValueStatus::success();
                               }),
          "duplicate migration step was accepted");
    std::vector<std::uint8_t> migrated;
    check(migrations.migrate("toy3d.CollisionBox", 1, 2, old_schema.bytes(), migrated).succeeded(),
          "schema migration failed");
    toy3d::ValueReader migrated_reader(migrated);
    std::uint32_t migrated_count = 0;
    std::string migrated_name;
    std::uint8_t flags = 0;
    std::vector<std::uint8_t> migrated_field;
    check(migrated_reader.read_array_length(migrated_count).succeeded() && migrated_count == 1 &&
              migrated_reader.read_utf8(migrated_name).succeeded() && migrated_name == "half_extents" &&
              migrated_reader.read_uint8(flags).succeeded() && flags == 1 &&
              migrated_reader.read_blob(migrated_field).succeeded() && migrated_reader.at_end(),
          "migration did not write current field identity");
    toy3d::ValueReader extent_reader(migrated_field);
    toy3d::Vector3 half_extents;
    check(toy3d::decode_value(extent_reader, half_extents).succeeded() && extent_reader.at_end() &&
              half_extents == toy3d::Vector3{2.0f, 2.0f, 2.0f},
          "field type conversion failed");
    const std::vector<std::uint8_t> published = migrated;
    check(migrations.migrate("toy3d.CollisionBox", 2, 1, old_schema.bytes(), migrated).code ==
                  toy3d::ValueErrorCode::InvalidValue &&
              migrated == published,
          "newer schema changed published output");
    check(migrations.migrate("toy3d.Unknown", 1, 2, old_schema.bytes(), migrated).code ==
                  toy3d::ValueErrorCode::InvalidValue &&
              migrated == published,
          "missing migration changed published output");
    return 0;
}

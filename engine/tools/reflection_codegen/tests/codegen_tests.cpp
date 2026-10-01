#include "generated/fixture_reflection.h"
#include "asset/asset_file.h"
#include "asset/property_path.h"
#include "asset/edit_session.h"
#include "file_system/directory_file_store.h"
#include "file_system/native_platform_file.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <type_traits>
#include <thread>
#include <utility>

namespace
{
    class ZeroWriteHandle final : public toy3d::FileHandle
    {
      public:
        explicit ZeroWriteHandle(std::unique_ptr<toy3d::FileHandle> inner) : inner_(std::move(inner)) {}
        toy3d::FileResult<std::uint64_t> size() const override { return inner_->size(); }
        toy3d::FileResult<std::size_t> read(std::uint8_t* destination, std::size_t count) override
        {
            return inner_->read(destination, count);
        }
        toy3d::FileResult<std::size_t> write(const std::uint8_t*, std::size_t) override
        {
            return toy3d::FileResult<std::size_t>(std::size_t{0});
        }
        toy3d::FileResult<std::uint64_t> tell() const override { return inner_->tell(); }
        toy3d::FileStatus seek(std::uint64_t offset) override { return inner_->seek(offset); }
        toy3d::FileResult<std::size_t> read_at(std::uint64_t offset, std::uint8_t* destination,
                                              std::size_t count) const override
        {
            return inner_->read_at(offset, destination, count);
        }
        toy3d::FileStatus flush() override { return inner_->flush(); }
        toy3d::FileStatus close() override { return inner_->close(); }

      private:
        std::unique_ptr<toy3d::FileHandle> inner_;
    };

    class FaultStore final : public toy3d::FileStore
    {
      public:
        enum class Fault { ZeroWrite, Replace };
        FaultStore(std::shared_ptr<toy3d::FileStore> inner, Fault fault)
            : inner_(std::move(inner)), fault_(fault) {}
        toy3d::FileStoreCapabilities capabilities() const override { return inner_->capabilities(); }
        toy3d::FileResult<toy3d::FileStat> stat(const toy3d::StorePath& path) const override
        {
            return inner_->stat(path);
        }
        toy3d::FileResult<std::unique_ptr<toy3d::FileHandle>> open(const toy3d::StorePath& path,
                                                                    toy3d::FileOpenMode mode) override
        {
            auto opened = inner_->open(path, mode);
            if (!opened.succeeded()) return opened;
            if (fault_ == Fault::ZeroWrite && mode != toy3d::FileOpenMode::Read)
                return toy3d::FileResult<std::unique_ptr<toy3d::FileHandle>>(
                    std::make_unique<ZeroWriteHandle>(std::move(opened.value())));
            return opened;
        }
        toy3d::FileResult<std::vector<toy3d::StoreDirectoryEntry>> enumerate(
            const toy3d::StorePath& path) const override { return inner_->enumerate(path); }
        toy3d::FileStatus create_directories(const toy3d::StorePath& path) override
        {
            return inner_->create_directories(path);
        }
        toy3d::FileStatus remove_file(const toy3d::StorePath& path) override
        {
            return inner_->remove_file(path);
        }
        toy3d::FileStatus remove_empty_directory(const toy3d::StorePath& path) override
        {
            return inner_->remove_empty_directory(path);
        }
        toy3d::FileStatus rename_no_replace(const toy3d::StorePath& source,
                                            const toy3d::StorePath& destination) override
        {
            return inner_->rename_no_replace(source, destination);
        }
        toy3d::FileStatus replace(const toy3d::StorePath& source,
                                  const toy3d::StorePath& destination) override
        {
            if (fault_ == Fault::Replace)
                return {toy3d::FileErrorCode::IoError, "replace", {}, {}, "injected replace failure", 0};
            return inner_->replace(source, destination);
        }

      private:
        std::shared_ptr<toy3d::FileStore> inner_;
        Fault fault_;
    };

    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(1);
        }
    }
}

int main()
{
    using namespace toy3d;
    static_assert(std::is_same<decltype(encode_value(std::declval<ValueWriter&>(),
                                                     std::declval<const SimpleAsset&>())),
                               ValueStatus>::value,
                  "generated encoder declaration must match the authored type");
    static_assert(std::is_same<decltype(decode_value(std::declval<ValueReader&>(), std::declval<SimpleAsset&>())),
                               ValueStatus>::value,
                  "generated decoder declaration must match the authored type");

    TypeRegistry registry;
    check(register_generated_fixture_types(registry).succeeded(), "generated registration failed");
    check(registry.find("toy3d.SimpleAsset") == nullptr, "unfrozen schema must not be queried");
    check(registry.freeze().succeeded(), "registry freeze failed");
    const TypeDesc* description = registry.find("toy3d.SimpleAsset");
    check(description != nullptr, "generated type is missing");
    check(description->schema_version == 1, "generated schema version is wrong");
    check(description->properties.size() == 5, "unmarked runtime field entered schema");
    check(description->properties[0].name == "count", "properties must have stable order");
    check(description->properties[0].usage == 1u, "Edit usage was lost");
    check(description->properties[0].hint.has_range && description->properties[0].hint.range_max == 10.0,
          "Range hint was lost");
    check(description->properties[0].hint.unit == "items", "Unit hint was lost");
    check(description->properties[1].hint.asset_type == "toy3d.ModelAsset", "AssetType hint was lost");
    check(description->properties[2].usage == 6u, "Visible | Transient usage was lost");
    check(!property_is_persisted(description->properties[2]) && !property_is_editable(description->properties[2]) &&
              property_is_visible(description->properties[2]),
          "derived property semantics are wrong");
    check(description->properties[3].usage == 0u, "hidden persistent property has wrong usage");
    check(property_is_persisted(description->properties[3]) && !property_is_visible(description->properties[3]),
          "hidden persistent property semantics are wrong");
    check(description->properties[4].name == "title", "property name was lost");
    check(description->properties[4].cpp_type == "std::string", "C++ field type was lost");
    check(description->properties[4].hint.category == "General", "Category hint was lost");
    const TypeDesc* nested = registry.find("toy3d.NestedAsset");
    check(nested != nullptr && nested->properties.size() == 3, "nested fixture is missing");
    check(nested->properties[0].value_type.kind == ValueKind::Array &&
              nested->properties[0].value_type.arguments.size() == 1 &&
              nested->properties[0].value_type.arguments[0].stable_name == "toy3d.BoxData",
          "array element description is wrong");
    check(nested->properties[1].value_type.kind == ValueKind::Enum &&
              nested->properties[1].value_type.stable_name == "toy3d.ShapeMode",
          "stable enum description is wrong");
    check(nested->properties[2].value_type.kind == ValueKind::Variant &&
              nested->properties[2].value_type.arguments.size() == 2 &&
              nested->properties[2].value_type.arguments[1].stable_name == "toy3d.CapsuleData",
          "variant branch description is wrong");
    const TypeDesc* shape_mode = registry.find("toy3d.ShapeMode");
    check(shape_mode != nullptr && shape_mode->enum_values.size() == 2 &&
              shape_mode->enum_values[1].name == "Trigger" && shape_mode->enum_values[1].value == 2,
          "explicit enum values were lost");

    SimpleAsset original;
    original.title = "asset";
    original.count = 7;
    original.dependency = "model-id";
    original.derived = 99.0f;
    original.hidden = 42;
    original.runtime_cache = 17;
    ValueWriter simple_writer;
    check(encode_value(simple_writer, original).succeeded(), "simple encode failed");
    SimpleAsset restored;
    restored.derived = 5.0f;
    ValueReader simple_reader(simple_writer.bytes());
    check(decode_value(simple_reader, restored).succeeded() && simple_reader.at_end(), "simple decode failed");
    check(restored.title == original.title && restored.count == original.count &&
              restored.dependency == original.dependency && restored.hidden == original.hidden,
          "persisted simple fields did not round trip");
    check(restored.derived == 0.0f && restored.runtime_cache == 0,
          "transient and unmarked fields must not be persisted");

    NestedAsset nested_original;
    nested_original.mode = ShapeMode::Trigger;
    nested_original.boxes.push_back({Vector3{1.0f, 2.0f, 3.0f}});
    nested_original.boxes.push_back({Vector3{4.0f, 5.0f, 6.0f}});
    nested_original.shape = CapsuleData{2.5f};
    ValueWriter nested_writer;
    check(encode_value(nested_writer, nested_original).succeeded(), "nested encode failed");
    NestedAsset nested_restored;
    ValueReader nested_reader(nested_writer.bytes());
    check(decode_value(nested_reader, nested_restored).succeeded() && nested_reader.at_end(),
          "nested decode failed");
    check(nested_restored.boxes.size() == 2 && nested_restored.boxes[0].size == Vector3{1.0f, 2.0f, 3.0f} &&
              nested_restored.boxes[1].size == Vector3{4.0f, 5.0f, 6.0f} &&
              nested_restored.mode == ShapeMode::Trigger &&
              std::get<CapsuleData>(nested_restored.shape).radius == 2.5f,
          "nested array, enum or variant did not round trip");

    ValueWriter unknown_shape;
    check(unknown_shape.write_array_length(3).succeeded(), "fixture field count failed");
    ValueWriter boxes_field;
    check(boxes_field.write_array_length(0).succeeded(), "fixture boxes failed");
    check(unknown_shape.write_utf8("boxes").succeeded() &&
              unknown_shape.write_uint8(1u).succeeded() &&
              unknown_shape.write_blob(boxes_field.bytes()).succeeded(), "fixture boxes frame failed");
    ValueWriter mode_field;
    check(mode_field.write_int64(1).succeeded(), "fixture mode failed");
    check(unknown_shape.write_utf8("mode").succeeded() &&
              unknown_shape.write_uint8(1u).succeeded() &&
              unknown_shape.write_blob(mode_field.bytes()).succeeded(), "fixture mode frame failed");
    ValueWriter shape_field;
    check(shape_field.write_utf8("toy3d.UnknownShape").succeeded(), "fixture unknown tag failed");
    check(unknown_shape.write_utf8("shape").succeeded() &&
              unknown_shape.write_uint8(1u).succeeded() &&
              unknown_shape.write_blob(shape_field.bytes()).succeeded(), "fixture shape frame failed");
    NestedAsset unchanged;
    unchanged.mode = ShapeMode::Trigger;
    ValueReader unknown_reader(unknown_shape.bytes());
    const ValueStatus unknown_status = decode_value(unknown_reader, unchanged);
    check(unknown_status.code == ValueErrorCode::InvalidValue && unknown_status.property_path == "shape" &&
              unchanged.mode == ShapeMode::Trigger, "unknown required variant must fail without publishing candidate");

    ValueReader known_fields(simple_writer.bytes());
    std::uint32_t known_count = 0;
    check(known_fields.read_array_length(known_count).succeeded() && known_count == 4,
          "simple field fixture count is wrong");
    ValueWriter with_optional;
    check(with_optional.write_array_length(known_count + 1).succeeded(), "optional field count failed");
    for (std::uint32_t index = 0; index < known_count; ++index)
    {
        std::string name;
        std::uint8_t field_flags = 0;
        std::vector<std::uint8_t> payload;
        check(known_fields.read_utf8(name).succeeded() && known_fields.read_uint8(field_flags).succeeded() &&
                  known_fields.read_blob(payload).succeeded() && with_optional.write_utf8(name).succeeded() &&
                  with_optional.write_uint8(field_flags).succeeded() && with_optional.write_blob(payload).succeeded(),
              "copy known field fixture failed");
    }
    check(known_fields.at_end() && with_optional.write_utf8("future_note").succeeded() &&
              with_optional.write_uint8(0u).succeeded() && with_optional.write_blob({1u, 2u}).succeeded(),
          "optional field fixture failed");
    SimpleAsset read_only_candidate;
    read_only_candidate.count = 9;
    ValueReader optional_reader(with_optional.bytes());
    const ValueStatus optional_status = decode_value(optional_reader, read_only_candidate);
    check(optional_status.code == ValueErrorCode::UnknownOptionalField &&
              optional_status.property_path == "future_note" && read_only_candidate.count == 9,
          "unknown optional field must prevent lossy save and preserve caller value");
    std::vector<std::uint8_t> required_bytes = with_optional.bytes();
    required_bytes[required_bytes.size() - 7] = 1u;
    ValueReader required_reader(required_bytes);
    const ValueStatus required_status = decode_value(required_reader, read_only_candidate);
    check(required_status.code == ValueErrorCode::InvalidValue &&
              required_status.property_path == "future_note" && read_only_candidate.count == 9,
          "unknown required field must be rejected");

    ValueWriter canonical_writer;
    check(encode_value(canonical_writer, nested_restored).succeeded() &&
              canonical_writer.bytes() == nested_writer.bytes(), "re-encoding must be canonical");

    TimelineAsset timeline;
    timeline.events = {{"first", 0.25f, "A"}, {"second", 0.75f, "B"}};
    ValueWriter timeline_writer;
    check(encode_value(timeline_writer, timeline).succeeded(), "timeline fixture encode failed");
    const TypeDesc* timeline_type = registry.find("toy3d.TimelineAsset");
    check(timeline_type != nullptr, "timeline schema missing");
    const PropertyPath event_time = {PropertyPathPart::field("events"),
        PropertyPathPart::element_id("id", "second"), PropertyPathPart::field("time")};
    auto time_read = access_property(registry, *timeline_type, timeline_writer.bytes(), event_time);
    check(time_read.succeeded() && time_read.value().value_type.kind == ValueKind::Float32,
          "stable event path could not read a nested value");
    ValueReader event_time_reader(time_read.value().value_bytes);
    float selected_time = 0.0f;
    check(event_time_reader.read_float32(selected_time).succeeded() && selected_time == 0.75f,
          "stable path selected the wrong event");
    timeline.events.insert(timeline.events.begin(), {"inserted", 0.1f, "new"});
    ValueWriter reordered_timeline;
    check(encode_value(reordered_timeline, timeline).succeeded(), "reordered timeline encode failed");
    auto after_insert = access_property(registry, *timeline_type, reordered_timeline.bytes(), event_time);
    ValueReader after_insert_reader(after_insert.value().value_bytes);
    check(after_insert.succeeded() && after_insert_reader.read_float32(selected_time).succeeded() &&
              selected_time == 0.75f, "array insertion changed stable event selection");
    ValueWriter new_time;
    check(new_time.write_float32(0.8f).succeeded(), "new time encode failed");
    auto edited_time = access_property(registry, *timeline_type, reordered_timeline.bytes(), event_time,
                                       &new_time.bytes());
    check(edited_time.succeeded(), "nested event property replacement failed");
    TimelineAsset changed_timeline;
    ValueReader changed_timeline_reader(edited_time.value().root_bytes);
    check(decode_value(changed_timeline_reader, changed_timeline).succeeded() &&
              changed_timeline.events.size() == 3 && changed_timeline.events[2].id == "second" &&
              changed_timeline.events[2].time == 0.8f && changed_timeline.events[1].time == 0.25f,
          "nested replacement changed the wrong event");
    const PropertyPath event_id = {PropertyPathPart::field("events"),
        PropertyPathPart::element_id("id", "second"), PropertyPathPart::field("id")};
    check(access_property(registry, *timeline_type, reordered_timeline.bytes(), event_id,
                          &new_time.bytes()).status().code == AssetErrorCode::ReadOnly,
          "hidden stable event identity was editable");
    const TypeDesc* nested_type = registry.find("toy3d.NestedAsset");
    const PropertyPath radius_path = {PropertyPathPart::field("shape"),
        PropertyPathPart::variant_branch("toy3d.CapsuleData"), PropertyPathPart::field("radius")};
    auto radius_read = access_property(registry, *nested_type, nested_writer.bytes(), radius_path);
    check(radius_read.succeeded(), "active variant branch path failed");
    const PropertyPath wrong_branch = {PropertyPathPart::field("shape"),
        PropertyPathPart::variant_branch("toy3d.BoxData"), PropertyPathPart::field("size")};
    check(access_property(registry, *nested_type, nested_writer.bytes(), wrong_branch).status().code ==
              AssetErrorCode::Value, "inactive variant branch was accepted");

    AssetId edit_id;
    check(AssetId::parse("1234567890abcdef1234567890abcdef", edit_id), "edit fixture ID failed");
    auto edit_path = VirtualPath::parse("/asset/timeline.asset");
    check(edit_path.succeeded(), "edit fixture path failed");
    int preview_count = 0;
    EditChangeKind last_kind = EditChangeKind::Setter;
    auto validate_timeline = [](const TimelineAsset& value)
    {
        for (const EventData& event : value.events)
            if (event.time < 0.0f || event.time > 1.0f)
                return AssetStatus{AssetErrorCode::Value, {}, {}, {}, "events.time",
                                   "event time outside clip", {}};
        return AssetStatus::success();
    };
    EditSession<TimelineAsset> timeline_session(registry, *timeline_type, edit_id, edit_path.value(),
        timeline, validate_timeline, nullptr,
        [](const TimelineAsset& candidate, EditChangeKind)
        {
            return candidate.events[2].time == 0.9f ?
                AssetStatus{AssetErrorCode::Conflict, {}, {}, {}, {}, "preview preparation failed", {}} :
                AssetStatus::success();
        },
        [&](const EditRecord& record) { ++preview_count; last_kind = record.kind; });
    auto edited = timeline_session.apply_edit({{event_time, new_time.bytes(), EditChangeKind::Cook}});
    check(edited.succeeded() && edited.value().changes.size() == 1 &&
              edited.value().kind == EditChangeKind::Cook &&
              timeline_session.value().events[2].time == 0.8f && timeline_session.dirty() &&
              timeline_session.undo_count() == 1 && preview_count == 1 && last_kind == EditChangeKind::Cook,
          "successful transaction did not publish value, undo record and preview event");
    check(timeline_session.undo().succeeded() && timeline_session.value().events[2].time == 0.75f &&
              !timeline_session.dirty() && timeline_session.redo_count() == 1 && preview_count == 2,
          "undo did not restore the typed snapshot");
    check(timeline_session.redo().succeeded() && timeline_session.value().events[2].time == 0.8f &&
              timeline_session.dirty() && preview_count == 3,
          "redo did not restore the edited snapshot");
    ValueWriter invalid_event_time;
    check(invalid_event_time.write_float32(2.0f).succeeded(), "invalid event fixture failed");
    check(!timeline_session.apply_edit({{event_time, invalid_event_time.bytes()}}).succeeded() &&
              timeline_session.value().events[2].time == 0.8f &&
              timeline_session.undo_count() == 1 && preview_count == 3,
          "domain failure changed session or preview");
    ValueWriter preview_rejected_time;
    check(preview_rejected_time.write_float32(0.9f).succeeded(), "preview rejection fixture failed");
    check(!timeline_session.apply_edit({{event_time, preview_rejected_time.bytes()}}).succeeded() &&
              timeline_session.value().events[2].time == 0.8f && preview_count == 3,
          "preview preparation failure changed session or preview");
    check(timeline_session.apply_edit({{event_id, new_time.bytes()}}).status().code ==
              AssetErrorCode::ReadOnly && preview_count == 3,
          "read-only property changed the session");
    AssetStatus wrong_thread;
    std::thread visitor([&] { wrong_thread = timeline_session.undo(); });
    visitor.join();
    check(wrong_thread.code == AssetErrorCode::InvalidState &&
              timeline_session.value().events[2].time == 0.8f && preview_count == 3,
          "non-owner thread changed the edit session");

    const TypeDesc* camera_type = registry.find("toy3d.CameraAsset");
    check(camera_type != nullptr, "camera schema missing");
    auto validate_camera = [](const CameraAsset& value)
    {
        return value.near > 0.0f && value.far > value.near ? AssetStatus::success() :
            AssetStatus{AssetErrorCode::Value, {}, {}, {}, "near/far", "invalid clip planes", {}};
    };
    EditSession<CameraAsset> camera_session(registry, *camera_type, edit_id, edit_path.value(),
                                            CameraAsset{}, validate_camera);
    ValueWriter near_value;
    ValueWriter far_value;
    check(near_value.write_float32(20.0f).succeeded() && far_value.write_float32(30.0f).succeeded(),
          "camera edit fixture failed");
    const PropertyPath near_path = {PropertyPathPart::field("near")};
    const PropertyPath far_path = {PropertyPathPart::field("far")};
    check(!camera_session.apply_edit({{near_path, near_value.bytes()}}).succeeded() &&
              camera_session.value().near == 1.0f && camera_session.undo_count() == 0,
          "single invalid near edit changed the snapshot");
    auto compound = camera_session.apply_edit({{near_path, near_value.bytes()},
                                               {far_path, far_value.bytes()}});
    check(compound.succeeded() && compound.value().changes.size() == 2 &&
              camera_session.value().near == 20.0f && camera_session.value().far == 30.0f &&
              camera_session.undo_count() == 1,
          "compound near/far edit was not one transaction");

    const TypeDesc* ref_type = registry.find("toy3d.RefAsset");
    check(ref_type != nullptr, "reference asset schema missing");
    AssetId referenced_id;
    AssetId absent_id;
    check(AssetId::parse("00112233445566778899aabbccddeeff", referenced_id) &&
              AssetId::parse("ffffffffffffffffffffffffffffffff", absent_id),
          "reference edit IDs failed");
    AssetFileIndex referenced_index;
    referenced_index.asset_id = referenced_id;
    referenced_index.root_type = "toy3d.ModelAsset";
    AssetIndex reference_index;
    auto reference_path = VirtualPath::parse("/asset/models/target.asset");
    check(reference_path.succeeded() &&
              reference_index.add(reference_path.value(), referenced_index).succeeded(),
          "reference edit index failed");
    RefAsset ref_initial;
    ref_initial.target.asset_id = referenced_id;
    ref_initial.target.expected_type = "toy3d.ModelAsset";
    EditSession<RefAsset> ref_session(registry, *ref_type, edit_id, edit_path.value(), ref_initial,
        [](const RefAsset&) { return AssetStatus::success(); }, &reference_index);
    AssetRef absent_reference = ref_initial.target;
    absent_reference.asset_id = absent_id;
    ValueWriter absent_reference_bytes;
    check(encode_value(absent_reference_bytes, absent_reference).succeeded(),
          "absent reference fixture encode failed");
    const PropertyPath target_path = {PropertyPathPart::field("target")};
    const auto bad_reference_edit = ref_session.apply_edit({{target_path, absent_reference_bytes.bytes()}});
    check(bad_reference_edit.status().code == AssetErrorCode::MissingReference &&
              bad_reference_edit.status().property_path == "target" && ref_session.undo_count() == 0,
          "missing asset reference edit changed the session or lost its path");
    AssetRef wrong_type_reference = ref_initial.target;
    wrong_type_reference.expected_type = "toy3d.MaterialAsset";
    ValueWriter wrong_type_bytes;
    check(encode_value(wrong_type_bytes, wrong_type_reference).succeeded() &&
              ref_session.apply_edit({{target_path, wrong_type_bytes.bytes()}}).status().code ==
                  AssetErrorCode::TypeMismatch,
          "reference type mismatch was accepted");

    // C++17 filesystem creates only the isolated integration fixture. Asset
    // reads and publication use the shared FileSystem API.
    namespace fs = std::filesystem;
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    const fs::path fixture_root = fs::temp_directory_path() / ("toy3d_typed_asset_" + std::to_string(stamp));
    check(fs::create_directory(fixture_root), "typed fixture directory failed");
    NativePlatformFile platform;
    DirectoryFileStoreDesc store_desc;
    store_desc.physical_root = PhysicalPath(fixture_root.u8string());
    auto store = DirectoryFileStore::create(platform, store_desc);
    check(store.succeeded(), "typed fixture store failed");
    FileSystem files;
    auto mount_root = VirtualPath::parse("/asset");
    auto asset_path = VirtualPath::parse("/asset/any/folder/simple.asset");
    check(mount_root.succeeded() && asset_path.succeeded(), "typed fixture virtual path failed");
    FileMountDesc mount;
    mount.virtual_root = mount_root.value();
    mount.store = store.value();
    mount.access = MountAccess::ReadWrite;
    check(files.add_mount(mount).succeeded() && files.freeze().succeeded(), "typed fixture mount failed");
    check(files.create_directories(VirtualPath::parse("/asset/any/folder").value()).succeeded(),
          "typed fixture directory creation failed");
    AssetFileIndex asset_index;
    check(AssetId::parse("00112233445566778899aabbccddeeff", asset_index.asset_id), "typed fixture ID failed");
    asset_index.root_type = "toy3d.SimpleAsset";
    asset_index.schema_version = 1;
    const std::vector<std::uint8_t> geometry_blob(1024 * 1024, 0x5au);
    auto seed = encode_asset_file(asset_index, {{"type_data", 1, true, simple_writer.bytes()},
                                                {"geometry", 2, false, geometry_blob}});
    check(seed.succeeded() && files.write_binary(asset_path.value(), seed.value(), FileWriteMode::CreateNew).succeeded(),
          "typed fixture seed failed");
    SchemaMigrationRegistry migrations;
    auto valid_simple = [](const SimpleAsset& value)
    {
        return value.count >= 0 ? AssetStatus::success() :
            AssetStatus{AssetErrorCode::Value, {}, {}, {}, "count", "negative count", {}};
    };
    SimpleAsset loaded;
    check(load_asset(registry, migrations, files, asset_path.value(), "toy3d.SimpleAsset", loaded,
                     valid_simple).succeeded() && loaded.count == original.count,
          "typed asset load failed");
    loaded.count = 8;
    check(save_asset(registry, migrations, files, asset_path.value(), asset_index, loaded, valid_simple).code ==
              AssetErrorCode::ReadOnly, "save silently discarded existing blob");
    check(save_asset(registry, migrations, files, asset_path.value(), asset_index, loaded, valid_simple,
                     {{"geometry", 2, false, geometry_blob}}).succeeded(),
          "typed asset atomic save failed");
    SimpleAsset reopened;
    check(load_asset(registry, migrations, files, asset_path.value(), "toy3d.SimpleAsset", reopened,
                     valid_simple).succeeded() && reopened.count == 8, "typed asset reopen failed");
    auto saved_index = inspect_asset(files, asset_path.value());
    check(saved_index.succeeded(), "saved asset index failed");
    auto saved_bytes = files.read_binary(asset_path.value());
    check(saved_bytes.succeeded(), "saved asset bytes failed");
    const auto blob_segment = std::find_if(saved_index.value().segments.begin(), saved_index.value().segments.end(),
        [](const AssetSegment& segment) { return segment.name == "geometry"; });
    check(blob_segment != saved_index.value().segments.end() &&
              blob_segment->length == geometry_blob.size() &&
              std::equal(geometry_blob.begin(), geometry_blob.end(),
                         saved_bytes.value().begin() + static_cast<std::ptrdiff_t>(blob_segment->offset)),
          "save did not preserve supplied blob bytes");
    loaded.count = -1;
    check(save_asset(registry, migrations, files, asset_path.value(), asset_index, loaded, valid_simple,
                     {{"geometry", 2, false, geometry_blob}}).code ==
              AssetErrorCode::Value, "domain validator did not reject invalid save");
    loaded.count = 9;
    FileSystem read_only_files;
    mount.access = MountAccess::ReadOnly;
    check(read_only_files.add_mount(mount).succeeded() && read_only_files.freeze().succeeded(),
          "read-only fixture mount failed");
    check(save_asset(registry, migrations, read_only_files, asset_path.value(), asset_index, loaded, valid_simple,
                     {{"geometry", 2, false, geometry_blob}}).code == AssetErrorCode::Io,
          "atomic publish failure was not returned");
    SimpleAsset after_failure;
    check(load_asset(registry, migrations, files, asset_path.value(), "toy3d.SimpleAsset", after_failure,
                     valid_simple).succeeded() && after_failure.count == 8,
          "failed save changed the published file");
    const std::vector<std::uint8_t> before_fault = files.read_binary(asset_path.value()).value();
    auto expect_publish_fault = [&](FaultStore::Fault fault)
    {
        FileSystem faulty_files;
        FileMountDesc faulty_mount = mount;
        faulty_mount.access = MountAccess::ReadWrite;
        faulty_mount.store = std::make_shared<FaultStore>(store.value(), fault);
        check(faulty_files.add_mount(faulty_mount).succeeded() && faulty_files.freeze().succeeded(),
              "fault fixture mount failed");
        SimpleAsset changed = after_failure;
        changed.count = 9;
        check(save_asset(registry, migrations, faulty_files, asset_path.value(), asset_index, changed,
                         valid_simple, {{"geometry", 2, false, geometry_blob}}).code == AssetErrorCode::Io &&
                  files.read_binary(asset_path.value()).value() == before_fault,
              "short write or replace failure changed published asset");
    };
    expect_publish_fault(FaultStore::Fault::ZeroWrite);
    expect_publish_fault(FaultStore::Fault::Replace);
    auto unknown_file = encode_asset_file(asset_index, {{"type_data", 1, true, with_optional.bytes()},
                                                         {"geometry", 2, false, geometry_blob}});
    check(unknown_file.succeeded() && files.write_binary(asset_path.value(), unknown_file.value(),
                                                          FileWriteMode::Truncate).succeeded(),
          "unknown optional asset fixture failed");
    after_failure.count = 8;
    check(load_asset(registry, migrations, files, asset_path.value(), "toy3d.SimpleAsset",
                     after_failure, valid_simple).code == AssetErrorCode::ReadOnly &&
              after_failure.count == 8,
          "unknown optional field did not block lossy typed load");
    check(save_asset(registry, migrations, files, asset_path.value(), asset_index, after_failure,
                     valid_simple, {{"geometry", 2, false, geometry_blob}}).code == AssetErrorCode::ReadOnly &&
              files.read_binary(asset_path.value()).value() == unknown_file.value(),
          "unknown optional field was discarded by direct save");

    auto session_path = VirtualPath::parse("/asset/any/folder/session.asset");
    check(session_path.succeeded(), "session fixture path failed");
    AssetFileIndex session_index;
    check(AssetId::parse("fedcbafedcbafedcbafedcbafedcbafe", session_index.asset_id),
          "session fixture ID failed");
    session_index.root_type = "toy3d.SimpleAsset";
    session_index.schema_version = 1;
    auto session_file = encode_asset_file(session_index, {{"type_data", 1, true, simple_writer.bytes()}});
    check(session_file.succeeded() && files.write_binary(session_path.value(), session_file.value(),
                                                          FileWriteMode::CreateNew).succeeded(),
          "session fixture file failed");
    EditSession<SimpleAsset> edit_session(registry, *description, session_index.asset_id,
                                          session_path.value(), original, valid_simple);
    check(edit_session.bind_published(files).succeeded(), "session could not bind published data");
    ValueWriter count_nine;
    check(count_nine.write_int32(9).succeeded(), "count edit bytes failed");
    const PropertyPath count_path = {PropertyPathPart::field("count")};
    check(edit_session.apply_edit({{count_path, count_nine.bytes()}}).succeeded() &&
              edit_session.dirty(), "edit session did not become dirty");
    FileSystem session_fault_files;
    FileMountDesc session_fault_mount = mount;
    session_fault_mount.access = MountAccess::ReadWrite;
    session_fault_mount.store = std::make_shared<FaultStore>(store.value(), FaultStore::Fault::Replace);
    check(session_fault_files.add_mount(session_fault_mount).succeeded() &&
              session_fault_files.freeze().succeeded(), "session save fault mount failed");
    check(edit_session.save(session_fault_files, migrations, session_index).code == AssetErrorCode::Io &&
              edit_session.dirty() && edit_session.undo_count() == 1,
          "failed session save cleared dirty state or undo history");
    check(edit_session.save(files, migrations, session_index).succeeded() && !edit_session.dirty(),
          "successful session save did not clear dirty state");
    check(edit_session.undo().succeeded() && edit_session.dirty() &&
              edit_session.value().count == 7 && edit_session.redo_count() == 1,
          "undo after save did not restore dirty state");
    SimpleAsset external = original;
    external.count = 10;
    check(save_asset(registry, migrations, files, session_path.value(), session_index,
                     external, valid_simple).succeeded(), "external reimport fixture failed");
    check(edit_session.save(files, migrations, session_index).code == AssetErrorCode::Conflict &&
              edit_session.value().count == 7 && edit_session.redo_count() == 1 &&
              edit_session.dirty(), "reimport conflict discarded unsaved edit or undo history");

    auto collision_path = VirtualPath::parse("/asset/any/folder/collision.asset");
    check(collision_path.succeeded(), "collision fixture path failed");
    ValueWriter old_extent;
    ValueWriter old_collision;
    check(old_extent.write_float32(1.5f).succeeded() &&
              old_collision.write_array_length(1).succeeded() &&
              old_collision.write_utf8("half_extent").succeeded() &&
              old_collision.write_uint8(1u).succeeded() &&
              old_collision.write_blob(old_extent.bytes()).succeeded(), "old collision bytes failed");
    AssetFileIndex collision_index;
    check(AssetId::parse("abcdefabcdefabcdefabcdefabcdefab", collision_index.asset_id),
          "collision fixture ID failed");
    collision_index.root_type = "toy3d.CollisionBox";
    collision_index.schema_version = 1;
    auto old_file = encode_asset_file(collision_index, {{"type_data", 1, true, old_collision.bytes()}});
    check(old_file.succeeded() && files.write_binary(collision_path.value(), old_file.value(),
                                                      FileWriteMode::CreateNew).succeeded(),
          "old collision file fixture failed");
    check(migrations.add_step("toy3d.CollisionBox", 1, [](SchemaFields& fields)
    {
        ValueStatus status = rename_schema_field(fields, "half_extent", "half_extents");
        if (!status.succeeded()) return status;
        return convert_schema_field(fields, "half_extents",
            [](const std::vector<std::uint8_t>& old_bytes, std::vector<std::uint8_t>& new_bytes)
        {
            ValueReader reader(old_bytes);
            float scalar = 0.0f;
            ValueStatus status = reader.read_float32(scalar);
            if (!status.succeeded()) return status;
            if (!reader.at_end()) return reader.failure(ValueErrorCode::InvalidValue, "old extent has trailing bytes");
            ValueWriter writer;
            status = encode_value(writer, Vector3{scalar, scalar, scalar});
            if (status.succeeded()) new_bytes = writer.bytes();
            return status;
        });
    }), "collision migration registration failed");
    auto valid_box = [](const CollisionBox& box)
    {
        return box.half_extents.x > 0.0f && box.half_extents.y > 0.0f && box.half_extents.z > 0.0f ?
            AssetStatus::success() : AssetStatus{AssetErrorCode::Value, {}, {}, {}, "half_extents",
                                                "box extents must be positive", {}};
    };
    CollisionBox current_box;
    check(load_asset(registry, migrations, files, collision_path.value(), "toy3d.CollisionBox",
                     current_box, valid_box).succeeded() &&
              current_box.half_extents == Vector3{1.5f, 1.5f, 1.5f},
          "old collision asset did not migrate to current type");
    check(save_asset(registry, migrations, files, collision_path.value(), collision_index, current_box,
                     valid_box).succeeded(), "migrated collision asset save failed");
    auto saved_collision = inspect_asset(files, collision_path.value());
    check(saved_collision.succeeded() && saved_collision.value().schema_version == 2,
          "migrated collision asset was not saved with current schema version");
    collision_index.schema_version = 3;
    auto future_file = encode_asset_file(collision_index, {{"type_data", 1, true, old_collision.bytes()}});
    check(future_file.succeeded() && files.write_binary(collision_path.value(), future_file.value(),
                                                        FileWriteMode::Truncate).succeeded(),
          "future schema fixture failed");
    current_box.half_extents = Vector3{7.0f, 7.0f, 7.0f};
    check(load_asset(registry, migrations, files, collision_path.value(), "toy3d.CollisionBox",
                     current_box, valid_box).code == AssetErrorCode::Schema &&
              current_box.half_extents == Vector3{7.0f, 7.0f, 7.0f},
          "future schema changed caller value");
    auto old_format_path = VirtualPath::parse("/asset/any/folder/old_format.asset");
    check(old_format_path.succeeded(), "old format fixture path failed");
    std::vector<std::uint8_t> old_format_bytes = seed.value();
    old_format_bytes[8] = 0u;
    check(files.write_binary(old_format_path.value(), old_format_bytes, FileWriteMode::CreateNew).succeeded(),
          "old format fixture write failed");
    AssetFormatMigrationRegistry formats;
    check(formats.add_step(0, [](const std::vector<std::uint8_t>& input)
    {
        std::vector<std::uint8_t> next = input;
        next[8] = 1u;
        return AssetResult<std::vector<std::uint8_t>>(std::move(next));
    }), "file format migration registration failed");
    SimpleAsset old_format_loaded;
    check(load_asset(registry, formats, migrations, files, old_format_path.value(),
                     "toy3d.SimpleAsset", old_format_loaded, valid_simple).succeeded() &&
              old_format_loaded.count == original.count &&
              files.read_binary(old_format_path.value()).value() == old_format_bytes,
          "old file format did not migrate without changing source bytes");
    old_format_bytes[8] = 3u;
    check(files.write_binary(old_format_path.value(), old_format_bytes, FileWriteMode::Truncate).succeeded(),
          "future format fixture write failed");
    old_format_loaded.count = 42;
    check(load_asset(registry, formats, migrations, files, old_format_path.value(),
                     "toy3d.SimpleAsset", old_format_loaded, valid_simple).code ==
              AssetErrorCode::UnsupportedVersion && old_format_loaded.count == 42,
          "future file format changed caller value");
    std::error_code cleanup_error;
    fs::remove_all(fixture_root, cleanup_error);
    check(!cleanup_error, "typed fixture cleanup failed");
    return 0;
}

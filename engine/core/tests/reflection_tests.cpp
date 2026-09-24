#include "reflection/type_registry.h"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <utility>
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

    toy3d::TypeDesc make_type()
    {
        toy3d::TypeDesc type;
        type.name = "toy3d.ModelAsset";
        type.schema_version = 1;
        type.properties = {{"source", "std::string", 1u}, {"scale", "float", 1u}};
        type.properties[0].value_type.kind = toy3d::ValueKind::Utf8;
        type.properties[1].value_type.kind = toy3d::ValueKind::Float32;
        return type;
    }
}

int main()
{
    toy3d::TypeRegistry registry;
    check(registry.add(make_type()).succeeded(), "valid type registration failed");
    check(registry.find("toy3d.ModelAsset") == nullptr, "unfrozen type is visible");
    check(registry.freeze().succeeded(), "valid registry failed to freeze");
    const toy3d::TypeDesc* description = registry.find("toy3d.ModelAsset");
    check(description != nullptr && description->properties[0].name == "scale",
          "registration order changed persistent property order");
    toy3d::TypeRegistry reordered_registry;
    toy3d::TypeDesc reordered_type = make_type();
    std::swap(reordered_type.properties[0], reordered_type.properties[1]);
    check(reordered_registry.add(std::move(reordered_type)).succeeded() && reordered_registry.freeze().succeeded(),
          "reordered fields could not register");
    const toy3d::TypeDesc* reordered = reordered_registry.find("toy3d.ModelAsset");
    check(reordered != nullptr && reordered->properties[0].name == description->properties[0].name &&
              reordered->properties[1].name == description->properties[1].name,
          "field order changed persistent identity");
    check(!registry.add(make_type()).succeeded(), "frozen registry accepted a type");

    std::atomic<bool> reads_valid{true};
    std::vector<std::thread> readers;
    for (int index = 0; index < 4; ++index)
    {
        readers.emplace_back([&registry, &reads_valid]()
                             {
                                 for (int iteration = 0; iteration < 1000; ++iteration)
                                 {
                                     const toy3d::TypeDesc* found = registry.find("toy3d.ModelAsset");
                                     if (found == nullptr || found->properties.size() != 2)
                                     {
                                         reads_valid.store(false);
                                     }
                                 }
                             });
    }
    for (std::thread& reader : readers)
    {
        reader.join();
    }
    check(reads_valid.load(), "concurrent readers saw inconsistent descriptions");

    toy3d::TypeRegistry duplicate;
    check(duplicate.add(make_type()).succeeded(), "initial registration failed");
    toy3d::TypeDesc conflicting = make_type();
    conflicting.properties[0].cpp_type = "double";
    const toy3d::ReflectionStatus conflict = duplicate.add(std::move(conflicting));
    check(conflict.code == toy3d::ReflectionErrorCode::DuplicateName, "duplicate type was accepted");
    check(!duplicate.freeze().succeeded(), "failed registration was frozen");
    check(duplicate.find("toy3d.ModelAsset") == nullptr, "failed registration exposed a partial schema");

    toy3d::TypeRegistry invalid_usage;
    toy3d::TypeDesc invalid = make_type();
    invalid.properties[0].usage = 3u;
    check(!invalid_usage.add(std::move(invalid)).succeeded(), "Edit | Visible was accepted");
    check(!invalid_usage.freeze().succeeded(), "invalid usage registry was frozen");

    toy3d::TypeRegistry duplicate_field;
    toy3d::TypeDesc repeated = make_type();
    repeated.properties[1].name = "source";
    check(!duplicate_field.add(std::move(repeated)).succeeded(), "duplicate property was accepted");
    check(!duplicate_field.freeze().succeeded(), "duplicate property registry was frozen");
    return 0;
}

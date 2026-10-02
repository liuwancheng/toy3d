#include "scene/actor_details.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "reflection/type_registry.h"
#include "scene/editor_command_history.h"
#include "serialization/math_value_codec.h"
#include "serialization/schema_migration.h"
#include "logging/logger.h"

namespace toy3d
{
    void draw_actor_details(Actor& actor, EditorCommandHistory& history, const TypeRegistry& types, std::string& error)
    {
        const auto* actor_type = history.actor_types().find(actor);
        const auto* properties = actor_type ? types.find(actor_type->property_type) : nullptr;
        if (!properties || properties->properties.empty() || !actor.root_component()) return;
        ImGui::TextUnformatted(actor_type->display_name.c_str());
        for (const auto& property : properties->properties)
        {
            ReflectedValue candidate;
            SchemaFields fields;
            if (!history.actor_types().capture(actor, candidate) || !decode_schema_fields(candidate.bytes, fields).succeeded()) return;
            const auto field = fields.find(property.name);
            if (field == fields.end()) return;
            ValueReader reader(field->second.bytes);
            ValueWriter writer;
            bool changed = false, supported = true, decoded = true;
            ImGui::PushID(property.name.c_str());
            ImGui::BeginDisabled(!property_is_editable(property) || ImGui::GetDragDropPayload() != nullptr);
            if (property.value_type.kind == ValueKind::Bool)
            { bool value = false; decoded = reader.read_bool(value).succeeded(); if (decoded) { changed = ImGui::Checkbox(property.name.c_str(), &value); writer.write_bool(value); } }
            else if (property.value_type.kind == ValueKind::Float32)
            { float value = 0; decoded = reader.read_float32(value).succeeded(); if (decoded) { changed = ImGui::DragFloat(property.name.c_str(), &value, 0.5f); writer.write_float32(value); } }
            else if (property.value_type.kind == ValueKind::Vector3)
            { Vector3 value; decoded = decode_value(reader, value).succeeded(); if (decoded) { changed = ImGui::DragFloat3(property.name.c_str(), value.data(), 0.01f); encode_value(writer, value); } }
            else { supported = false; ImGui::TextDisabled("%s (%s)", property.name.c_str(), property.cpp_type.c_str()); }
            ImGui::EndDisabled();
            if (!decoded) { ImGui::PopID(); error = "Actor property decode failed: " + property.name; return; }
            if (supported && ImGui::IsItemActivated())
                history.begin(actor.world(), actor.actor_id(), actor.root_component()->local_transform(), EditorTransformSource::Details);
            if (changed)
            {
                field->second.bytes = writer.bytes();
                if (!encode_schema_fields(fields, candidate.bytes).succeeded() ||
                    !history.preview_actor_properties(actor.world(), actor.actor_id(), candidate))
                { error = "Actor rejected invalid properties: " + property.name; TOY_LOG_ERROR("{}", error); }
            }
            if (history.active_for(EditorTransformSource::Details) && ImGui::IsKeyPressed(ImGuiKey_Escape))
            { history.cancel(); ImGui::ClearActiveID(); }
            else if (supported && ImGui::IsItemDeactivated()) history.finish(actor.world(), EditorTransformSource::Details);
            ImGui::PopID();
        }
    }
}

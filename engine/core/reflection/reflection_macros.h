#pragma once

// Build-time markers are consumed by Toy3dReflectionCodegen. They do not add
// runtime fields or change the layout of authored data types.
#define TOY3D_REFLECT_TYPE(persistent_name, schema_version)
#define TOY3D_REFLECT_ENUM(persistent_name)
#define TOY3D_PROPERTY(persistent_name, ...)

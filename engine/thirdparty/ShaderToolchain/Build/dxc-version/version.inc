#pragma once

#ifdef RC_COMPANY_NAME
#undef RC_COMPANY_NAME
#endif
#define RC_COMPANY_NAME "Microsoft(r) Corporation"

#ifdef RC_VERSION_FIELD_1
#undef RC_VERSION_FIELD_1
#endif
#define RC_VERSION_FIELD_1 1

#ifdef RC_VERSION_FIELD_2
#undef RC_VERSION_FIELD_2
#endif
#define RC_VERSION_FIELD_2 8

#ifdef RC_VERSION_FIELD_3
#undef RC_VERSION_FIELD_3
#endif
#define RC_VERSION_FIELD_3 2505

#ifdef RC_VERSION_FIELD_4
#undef RC_VERSION_FIELD_4
#endif
#define RC_VERSION_FIELD_4 1

#ifdef RC_FILE_VERSION
#undef RC_FILE_VERSION
#endif
#define RC_FILE_VERSION "1.8.2505.1"

#ifdef RC_FILE_DESCRIPTION
#undef RC_FILE_DESCRIPTION
#endif
#define RC_FILE_DESCRIPTION "DirectX Compiler - Toy3d locked source build"

#ifdef RC_COPYRIGHT
#undef RC_COPYRIGHT
#endif
#define RC_COPYRIGHT "(c) Microsoft Corporation. All rights reserved."

#ifdef RC_PRODUCT_NAME
#undef RC_PRODUCT_NAME
#endif
#define RC_PRODUCT_NAME "Microsoft DirectX Shader Compiler - Toy3d locked source build"

#ifdef RC_PRODUCT_VERSION
#undef RC_PRODUCT_VERSION
#endif
#define RC_PRODUCT_VERSION "1.8.2505.1 (Toy3d, b106a961d09221b3)"

#ifdef HLSL_TOOL_NAME
#undef HLSL_TOOL_NAME
#endif
#define HLSL_TOOL_NAME "dxc(toy3d)"

#ifdef HLSL_VERSION_MACRO
#undef HLSL_VERSION_MACRO
#endif
#define HLSL_VERSION_MACRO HLSL_TOOL_NAME " " RC_FILE_VERSION

#ifdef HLSL_LLVM_IDENT
#undef HLSL_LLVM_IDENT
#endif
#define HLSL_LLVM_IDENT HLSL_TOOL_NAME " " RC_PRODUCT_VERSION

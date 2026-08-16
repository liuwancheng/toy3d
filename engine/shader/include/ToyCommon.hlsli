#pragma once

#if TOY_NATIVE_FLOAT16
    #define toy_half half
    #define toy_half2 half2
    #define toy_half3 half3
    #define toy_half4 half4
#else
    #define toy_half float
    #define toy_half2 float2
    #define toy_half3 float3
    #define toy_half4 float4
#endif

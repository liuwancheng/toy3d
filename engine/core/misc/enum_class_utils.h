#pragma once

#include <type_traits>

namespace toy3d
{
// Defines all bitwise operators for enum classes so they can be used as
// regular flags enums at call sites.
#define ENUM_CLASS_FLAGS(Enum)                                                                                         \
    inline Enum& operator|=(Enum& lhs, Enum rhs)                                                                       \
    {                                                                                                                  \
        using Underlying = std::underlying_type_t<Enum>;                                                               \
        lhs = static_cast<Enum>(static_cast<Underlying>(lhs) | static_cast<Underlying>(rhs));                          \
        return lhs;                                                                                                    \
    }                                                                                                                  \
    inline Enum& operator&=(Enum& lhs, Enum rhs)                                                                       \
    {                                                                                                                  \
        using Underlying = std::underlying_type_t<Enum>;                                                               \
        lhs = static_cast<Enum>(static_cast<Underlying>(lhs) & static_cast<Underlying>(rhs));                          \
        return lhs;                                                                                                    \
    }                                                                                                                  \
    inline Enum& operator^=(Enum& lhs, Enum rhs)                                                                       \
    {                                                                                                                  \
        using Underlying = std::underlying_type_t<Enum>;                                                               \
        lhs = static_cast<Enum>(static_cast<Underlying>(lhs) ^ static_cast<Underlying>(rhs));                          \
        return lhs;                                                                                                    \
    }                                                                                                                  \
    inline constexpr Enum operator|(Enum lhs, Enum rhs)                                                                \
    {                                                                                                                  \
        using Underlying = std::underlying_type_t<Enum>;                                                               \
        return static_cast<Enum>(static_cast<Underlying>(lhs) | static_cast<Underlying>(rhs));                         \
    }                                                                                                                  \
    inline constexpr Enum operator&(Enum lhs, Enum rhs)                                                                \
    {                                                                                                                  \
        using Underlying = std::underlying_type_t<Enum>;                                                               \
        return static_cast<Enum>(static_cast<Underlying>(lhs) & static_cast<Underlying>(rhs));                         \
    }                                                                                                                  \
    inline constexpr Enum operator^(Enum lhs, Enum rhs)                                                                \
    {                                                                                                                  \
        using Underlying = std::underlying_type_t<Enum>;                                                               \
        return static_cast<Enum>(static_cast<Underlying>(lhs) ^ static_cast<Underlying>(rhs));                         \
    }                                                                                                                  \
    inline constexpr bool operator!(Enum value)                                                                        \
    {                                                                                                                  \
        using Underlying = std::underlying_type_t<Enum>;                                                               \
        return !static_cast<Underlying>(value);                                                                        \
    }                                                                                                                  \
    inline constexpr Enum operator~(Enum value)                                                                        \
    {                                                                                                                  \
        using Underlying = std::underlying_type_t<Enum>;                                                               \
        return static_cast<Enum>(~static_cast<Underlying>(value));                                                     \
    }

// Friends all bitwise operators for enum classes so the definition can be
// kept private or protected.
#define FRIEND_ENUM_CLASS_FLAGS(Enum)                                                                                  \
    friend Enum& operator|=(Enum& lhs, Enum rhs);                                                                      \
    friend Enum& operator&=(Enum& lhs, Enum rhs);                                                                      \
    friend Enum& operator^=(Enum& lhs, Enum rhs);                                                                      \
    friend constexpr Enum operator|(Enum lhs, Enum rhs);                                                               \
    friend constexpr Enum operator&(Enum lhs, Enum rhs);                                                               \
    friend constexpr Enum operator^(Enum lhs, Enum rhs);                                                               \
    friend constexpr bool operator!(Enum value);                                                                       \
    friend constexpr Enum operator~(Enum value);

    template <typename Enum> constexpr bool EnumHasAllFlags(Enum Flags, Enum Contains)
    {
        return (
            (static_cast<std::underlying_type_t<Enum>>(Flags) & static_cast<std::underlying_type_t<Enum>>(Contains)) ==
            static_cast<std::underlying_type_t<Enum>>(Contains));
    }

    template <typename Enum> constexpr bool EnumHasAnyFlags(Enum Flags, Enum Contains)
    {
        return (static_cast<std::underlying_type_t<Enum>>(Flags) &
                static_cast<std::underlying_type_t<Enum>>(Contains)) != 0;
    }

    template <typename Enum> void EnumAddFlags(Enum& Flags, Enum FlagsToAdd)
    {
        Flags |= FlagsToAdd;
    }

    template <typename Enum> void EnumRemoveFlags(Enum& Flags, Enum FlagsToRemove)
    {
        Flags &= ~FlagsToRemove;
    }

} // namespace toy3d

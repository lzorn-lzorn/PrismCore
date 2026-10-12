#pragma once 

#include <cstdint>
#include <atomic>
#include <string_view>
namespace core
{

enum class ESystemType : uint8_t
{
	Global = 0,
	Game = 1,
	Renderer = 2,
	Physics = 3,
	Audio = 4,
	Video = 5,
	Network = 6,
	AI = 7,
	Script = 8,
	Animation = 9,
	Editor = 10
};

using HandleIdType = uint64_t;
using HandleGenerationType  = uint64_t;

inline constexpr HandleIdType         InvalidHandleId         = 0ull;
inline constexpr HandleGenerationType InvalidHandleGeneration = 0ull;
struct Global_t 
{
    constexpr static uint32_t value = 0; // System Number
    constexpr static std::string_view name = "Global"; 
};
struct Game_t 
{
    constexpr static uint32_t value = 1; // System Number
    constexpr static std::string_view name = "Game"; 
};
struct Renderer_t 
{
    constexpr static uint32_t value = 2; // System Number
    constexpr static std::string_view name = "Renderer"; 
};
struct Physics_t 
{
    constexpr static uint32_t value = 3; // System Number
    constexpr static std::string_view name = "Physics"; 
};
struct Audio_t 
{
    constexpr static uint32_t value = 4; // System Number
    constexpr static std::string_view name = "Audio"; 
};
struct Video_t 
{
    constexpr static uint32_t value = 5; // System Number
    constexpr static std::string_view name = "Video"; 
};
struct Network_t 
{
    constexpr static uint32_t value = 6; // System Number
    constexpr static std::string_view name = "Network"; 
};
struct AI_t 
{
    constexpr static uint32_t value = 7; // System Number
    constexpr static std::string_view name = "AI"; 
};
struct Script_t 
{
    constexpr static uint32_t value = 8; // System Number
    constexpr static std::string_view name = "Script"; 
};
struct Animation_t 
{
    constexpr static uint32_t value = 9; // System Number
    constexpr static std::string_view name = "Animation"; 
};
struct Editor_t 
{
    constexpr static uint32_t value = 10; // System Number
    constexpr static std::string_view name = "Editor"; 
};

inline std::atomic<uint64_t> GNextHandleId { 0 };

#define CORE_CONCAT_IMPL(a, b) a##b
#define CORE_CONCAT(a, b)      CORE_CONCAT_IMPL(a, b)

}
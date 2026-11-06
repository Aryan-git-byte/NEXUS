#pragma once
#include <cstdint>
namespace nexus {
enum class AppControl : uint16_t {
    Hub = 1, Calculator, Timer, Game2048, Snake, Pet, Macros,
    CalcFirst = 100, CalcLast = 119,
    GameUp = 200, GameRight, GameDown, GameLeft, GameNew, GameUndo,
    SnakeUp = 220, SnakeRight, SnakeDown, SnakeLeft, SnakeStart, SnakeNew, SnakeSpeed,
    TimerCountdown = 300, TimerFocus, TimerWatch, TimerStart, TimerReset, TimerSet,
    TimerSound, TimerLap, TimerPreset1, TimerPreset5,
    DurationPlusHour = 330, DurationPlusMinute, DurationPlusSecond,
    DurationMinusHour, DurationMinusMinute, DurationMinusSecond, DurationApply, DurationCancel,
    PetPat = 400, PetFeed, PetPlay, PetRest, PetVisible, PetName
};
inline constexpr const char* calculator_keys[] = {
    "C","DEL","%","/","7","8","9","*","4","5","6","-","1","2","3","+","+/-","0",".","="
};
}

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace nexus::apps {
enum class Direction : uint8_t { Up, Right, Down, Left };

class Random {
public:
    explicit Random(uint32_t seed = 1) : value_(seed ? seed : 1) {}
    uint32_t next();
    uint32_t below(uint32_t limit);
private:
    uint32_t value_;
};

// A separate, versioned record keeps the existing device/calibration blobs intact.
struct Preferences {
    uint32_t signature = 0x4e444131;
    uint32_t best_2048 = 0, best_snake = 0, pats = 0;
    uint32_t timer_seconds = 300;
    int64_t pet_epoch = 0;
    char pet_name[17] = "Mochi";
    uint8_t happiness = 80, fullness = 80, energy = 80;
    uint8_t pet_visible = 1, pet_sleeping = 0, timer_sound = 1;
    uint8_t snake_speed = 1, timer_mode = 0;
};
bool valid_preferences(const Preferences& value);

class Game2048 {
public:
    void reset(Random& random);
    bool move(Direction direction, Random& random);
    bool undo();
    bool can_move() const;
    const std::array<uint8_t,16>& tiles() const { return tiles_; }
    uint32_t score() const { return score_; }
    bool reached_2048() const { return reached_; }
private:
    friend class AppsTest;
    void spawn(Random& random);
    std::array<uint8_t,16> tiles_{}, undo_tiles_{};
    uint32_t score_ = 0, undo_score_ = 0;
    bool undo_available_ = false, reached_ = false;
};

class Snake {
public:
    static constexpr uint16_t width = 20, height = 16, capacity = width * height;
    void reset(Random& random);
    bool turn(Direction direction);
    bool step(Random& random);
    bool occupies(uint16_t cell) const;
    uint16_t head() const { return body_[0]; }
    uint16_t food() const { return food_; }
    uint16_t length() const { return length_; }
    uint32_t score() const { return (length_ - 4) * 10; }
    bool over() const { return over_; }
    bool won() const { return won_; }
    Direction direction() const { return direction_; }
private:
    friend class AppsTest;
    void place_food(Random& random);
    std::array<uint16_t,capacity> body_{};
    uint16_t length_ = 4, food_ = 0;
    Direction direction_ = Direction::Right, queued_ = Direction::Right;
    bool turn_queued_ = false, over_ = false, won_ = false;
};

class Calculator {
public:
    Calculator();
    void press(const char* key);
    const char* display() const { return display_.data(); }
    const char* expression() const { return expression_.data(); }
private:
    void clear();
    bool calculate(char operation, double right);
    void show(double value);
    void fail(const char* message);
    std::array<char,32> display_{};
    std::array<char,80> expression_{};
    double accumulator_ = 0, repeat_value_ = 0;
    char operation_ = 0, repeat_operation_ = 0;
    bool replace_ = true, failed_ = false;
};

enum class TimerMode : uint8_t { Countdown, Focus, Stopwatch };
enum class TimerState : uint8_t { Ready, Running, Paused, Finished };
enum class TimerEvent : uint8_t { None, Countdown, Focus, Break };

class Timer {
public:
    void set_mode(TimerMode mode);
    bool set_duration(uint32_t seconds);
    void start(uint64_t now);
    void pause(uint64_t now);
    void reset();
    TimerEvent tick(uint64_t now);
    bool lap(uint64_t now);
    uint64_t milliseconds(uint64_t now) const;
    uint32_t duration_seconds() const;
    uint8_t progress(uint64_t now) const;
    TimerMode mode() const { return mode_; }
    TimerState state() const { return state_; }
    bool on_break() const { return on_break_; }
    uint32_t focus_sessions() const { return focus_sessions_; }
    uint32_t lap_count() const { return lap_count_; }
    uint64_t last_lap() const { return last_lap_; }
private:
    friend class AppsTest;
    uint64_t elapsed(uint64_t now) const;
    TimerMode mode_ = TimerMode::Countdown;
    TimerState state_ = TimerState::Ready;
    uint32_t countdown_seconds_ = 300, focus_sessions_ = 0, lap_count_ = 0;
    uint64_t started_ = 0, accumulated_ = 0, last_lap_ = 0;
    bool on_break_ = false;
};

class Pet {
public:
    void initialize(uint64_t now) { last_tick_ = now; }
    bool tick(Preferences& preferences, uint64_t now, int64_t epoch);
    bool pat(Preferences& preferences, uint64_t now);
    void feed(Preferences& preferences);
    void play(Preferences& preferences);
    void rest(Preferences& preferences);
    bool reacting(uint64_t now) const { return now < reaction_until_; }
    const char* mood(const Preferences& preferences) const;
private:
    uint64_t last_tick_ = 0, last_pat_ = 0, reaction_until_ = 0;
    uint32_t food_minutes_ = 0, happy_minutes_ = 0, energy_minutes_ = 0;
    bool epoch_restored_ = false, patted_ = false, was_sleeping_ = false;
};
}

#include "local_apps.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace nexus::apps {
namespace {
uint32_t add_score(uint32_t score, uint32_t increment) {
    return increment > UINT32_MAX - score ? UINT32_MAX : score + increment;
}
int tile_index(Direction direction, int line, int position) {
    switch (direction) {
    case Direction::Up: return position * 4 + line;
    case Direction::Right: return line * 4 + 3 - position;
    case Direction::Down: return (3 - position) * 4 + line;
    case Direction::Left: return line * 4 + position;
    }
    return 0;
}
uint8_t decrease(uint8_t value, uint32_t delta) {
    return delta >= value ? 0 : value - delta;
}
uint8_t increase(uint8_t value, uint32_t delta) {
    return std::min<uint32_t>(100, value + delta);
}
}

uint32_t Random::next() {
    value_ ^= value_ << 13; value_ ^= value_ >> 17; value_ ^= value_ << 5;
    return value_;
}
uint32_t Random::below(uint32_t limit) {
    if (!limit) return 0;
    const uint32_t threshold = -limit % limit;
    uint32_t value;
    do { value = next(); } while (value < threshold);
    return value % limit;
}
bool valid_preferences(const Preferences& p) {
    if (p.signature != 0x4e444131 || p.timer_seconds < 1 || p.timer_seconds > 86400 ||
        p.happiness > 100 || p.fullness > 100 || p.energy > 100 ||
        p.pet_visible > 1 || p.pet_sleeping > 1 || p.timer_sound > 1 ||
        p.snake_speed > 2 || p.timer_mode > 2 || p.pet_epoch < 0 ||
        p.pet_epoch > 4102444800LL || !std::memchr(p.pet_name,0,sizeof(p.pet_name)) ||
        !p.pet_name[0]) return false;
    for (const unsigned char* c = reinterpret_cast<const unsigned char*>(p.pet_name); *c; ++c)
        if (*c < 32 || *c > 126) return false;
    return true;
}

void Game2048::reset(Random& random) {
    tiles_.fill(0); score_ = 0; undo_available_ = reached_ = false;
    spawn(random); spawn(random);
}
void Game2048::spawn(Random& random) {
    std::array<uint8_t,16> free{};
    uint8_t count = 0;
    for (uint8_t i = 0; i < tiles_.size(); ++i) if (!tiles_[i]) free[count++] = i;
    if (count) tiles_[free[random.below(count)]] = random.below(10) ? 1 : 2;
}
bool Game2048::move(Direction direction, Random& random) {
    if (static_cast<unsigned>(direction) > 3) return false;
    const auto before = tiles_;
    const uint32_t old_score = score_;
    for (int line = 0; line < 4; ++line) {
        std::array<uint8_t,4> compact{}, merged{};
        int count = 0, written = 0;
        for (int position = 0; position < 4; ++position) {
            const uint8_t value = tiles_[tile_index(direction,line,position)];
            if (value) compact[count++] = value;
        }
        for (int i = 0; i < count; ++i) {
            uint8_t exponent = compact[i];
            if (i + 1 < count && exponent < 31 && exponent == compact[i + 1]) {
                ++exponent; ++i;
                score_ = add_score(score_,uint32_t{1} << exponent);
            }
            merged[written++] = exponent;
            if (exponent >= 11) reached_ = true;
        }
        for (int position = 0; position < 4; ++position)
            tiles_[tile_index(direction,line,position)] = merged[position];
    }
    if (tiles_ == before) return false;
    undo_tiles_ = before; undo_score_ = old_score; undo_available_ = true;
    spawn(random); return true;
}
bool Game2048::undo() {
    if (!undo_available_) return false;
    tiles_ = undo_tiles_; score_ = undo_score_; undo_available_ = false;
    reached_ = std::any_of(tiles_.begin(),tiles_.end(),[](uint8_t e) { return e >= 11; });
    return true;
}
bool Game2048::can_move() const {
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
        const uint8_t e = tiles_[y * 4 + x];
        if (!e) return true;
        if (e < 31 && ((x < 3 && e == tiles_[y * 4 + x + 1]) ||
                      (y < 3 && e == tiles_[(y + 1) * 4 + x]))) return true;
    }
    return false;
}

void Snake::reset(Random& random) {
    length_ = 4; direction_ = queued_ = Direction::Right;
    turn_queued_ = over_ = won_ = false;
    for (uint16_t i = 0; i < length_; ++i) body_[i] = 8 * width + 9 - i;
    place_food(random);
}
bool Snake::occupies(uint16_t cell) const {
    return std::find(body_.begin(),body_.begin() + length_,cell) != body_.begin() + length_;
}
void Snake::place_food(Random& random) {
    const uint32_t free = capacity - length_;
    if (!free) { won_ = over_ = true; return; }
    uint32_t pick = random.below(free);
    for (uint16_t cell = 0; cell < capacity; ++cell)
        if (!occupies(cell) && pick-- == 0) { food_ = cell; return; }
}
bool Snake::turn(Direction direction) {
    if (over_ || static_cast<unsigned>(direction) > 3 || turn_queued_ ||
        (static_cast<unsigned>(direction_) + 2) % 4 == static_cast<unsigned>(direction))
        return false;
    if (direction == direction_) return true;
    queued_ = direction; turn_queued_ = true; return true;
}
bool Snake::step(Random& random) {
    if (over_) return false;
    direction_ = queued_; turn_queued_ = false;
    int x = body_[0] % width, y = body_[0] / width;
    switch (direction_) {
    case Direction::Up: --y; break;
    case Direction::Right: ++x; break;
    case Direction::Down: ++y; break;
    case Direction::Left: --x; break;
    }
    if (x < 0 || x >= width || y < 0 || y >= height) { over_ = true; return false; }
    const uint16_t next = y * width + x;
    const bool growing = next == food_;
    const uint16_t occupied = length_ - (growing ? 0 : 1);
    if (std::find(body_.begin(),body_.begin() + occupied,next) != body_.begin() + occupied) {
        over_ = true; return false;
    }
    if (growing) ++length_;
    for (uint16_t i = length_ - 1; i > 0; --i) body_[i] = body_[i - 1];
    body_[0] = next;
    if (growing) place_food(random);
    return true;
}

Calculator::Calculator() { clear(); }
void Calculator::clear() {
    display_.fill(0); expression_.fill(0); std::strcpy(display_.data(),"0");
    accumulator_ = repeat_value_ = 0; operation_ = repeat_operation_ = 0;
    replace_ = true; failed_ = false;
}
void Calculator::fail(const char* message) {
    std::snprintf(display_.data(),display_.size(),"%s",message);
    operation_ = repeat_operation_ = 0; failed_ = replace_ = true;
}
void Calculator::show(double value) {
    if (!std::isfinite(value) || std::abs(value) > 1e100) { fail("Out of range"); return; }
    if (value == 0) value = 0;
    std::snprintf(display_.data(),display_.size(),"%.12g",value);
}
bool Calculator::calculate(char operation, double right) {
    switch (operation) {
    case '+': accumulator_ += right; break;
    case '-': accumulator_ -= right; break;
    case '*': accumulator_ *= right; break;
    case '/':
        if (right == 0) { fail("Cannot divide by 0"); return false; }
        accumulator_ /= right; break;
    default: accumulator_ = right; break;
    }
    show(accumulator_); return !failed_;
}
void Calculator::press(const char* key) {
    if (!key || !key[0]) return;
    const char c = key[0];
    if (std::strcmp(key,"C") == 0) { clear(); return; }
    if (failed_) {
        if ((c >= '0' && c <= '9') || c == '.') clear();
        else return;
    }
    if ((c >= '0' && c <= '9') || c == '.') {
        if (replace_) { std::strcpy(display_.data(),"0"); replace_ = false; }
        if (c == '.' && std::strchr(display_.data(),'.')) return;
        size_t length = std::strlen(display_.data());
        if (length >= 15 || std::strchr(display_.data(),'e')) return;
        if (c != '.' && std::strcmp(display_.data(),"0") == 0) length = 0;
        display_[length] = c; display_[length + 1] = 0;
        repeat_operation_ = 0; return;
    }
    if (std::strcmp(key,"DEL") == 0) {
        if (replace_) return;
        const size_t length = std::strlen(display_.data());
        if (length > 1) display_[length - 1] = 0;
        else std::strcpy(display_.data(),"0");
        if (std::strcmp(display_.data(),"-") == 0) std::strcpy(display_.data(),"0");
        return;
    }
    double value = std::strtod(display_.data(),nullptr);
    if (std::strcmp(key,"+/-") == 0) { show(-value); return; }
    if (c == '%') {
        show((operation_ == '+' || operation_ == '-') ? accumulator_ * value / 100 : value / 100);
        replace_ = false; return;
    }
    if (c == '=' || c == '+' || c == '-' || c == '*' || c == '/') {
        if (c == '=') {
            const char operation = operation_ ? operation_ : repeat_operation_;
            if (!operation) { replace_ = true; return; }
            const double right = operation_ ? value : repeat_value_;
            std::snprintf(expression_.data(),expression_.size(),"%.12g %c %.12g =",accumulator_,operation,right);
            if (!calculate(operation,right)) return;
            repeat_operation_ = operation; repeat_value_ = right; operation_ = 0;
        } else {
            if (operation_ && !replace_) {
                if (!calculate(operation_,value)) return;
            } else if (!operation_) accumulator_ = value;
            operation_ = c; repeat_operation_ = 0;
            std::snprintf(expression_.data(),expression_.size(),"%.12g %c",accumulator_,operation_);
        }
        replace_ = true;
    }
}

void Timer::set_mode(TimerMode mode) {
    if (static_cast<unsigned>(mode) > 2 || mode == mode_) return;
    mode_ = mode; reset();
}
bool Timer::set_duration(uint32_t seconds) {
    if (seconds < 1 || seconds > 86400 || state_ == TimerState::Running) return false;
    countdown_seconds_ = seconds;
    if (mode_ == TimerMode::Countdown) reset();
    return true;
}
uint32_t Timer::duration_seconds() const {
    if (mode_ == TimerMode::Focus) return on_break_ ? (focus_sessions_ % 4 == 0 ? 900 : 300) : 1500;
    return mode_ == TimerMode::Countdown ? countdown_seconds_ : 0;
}
uint64_t Timer::elapsed(uint64_t now) const {
    return accumulated_ + (state_ == TimerState::Running && now >= started_ ? now - started_ : 0);
}
uint64_t Timer::milliseconds(uint64_t now) const {
    const uint64_t elapsed_ms = elapsed(now);
    if (mode_ == TimerMode::Stopwatch) return elapsed_ms;
    const uint64_t duration = uint64_t(duration_seconds()) * 1000;
    return elapsed_ms < duration ? duration - elapsed_ms : 0;
}
uint8_t Timer::progress(uint64_t now) const {
    const uint64_t duration = uint64_t(duration_seconds()) * 1000;
    return duration ? std::min<uint64_t>(100,elapsed(now) * 100 / duration) : 0;
}
void Timer::start(uint64_t now) {
    if (state_ == TimerState::Running) return;
    if (state_ == TimerState::Finished) reset();
    started_ = now; state_ = TimerState::Running;
}
void Timer::pause(uint64_t now) {
    if (state_ != TimerState::Running) return;
    if (mode_ != TimerMode::Stopwatch && elapsed(now) >= uint64_t(duration_seconds()) * 1000) return;
    accumulated_ = elapsed(now); state_ = TimerState::Paused;
}
void Timer::reset() {
    state_ = TimerState::Ready; started_ = accumulated_ = last_lap_ = 0;
    focus_sessions_ = lap_count_ = 0; on_break_ = false;
}
TimerEvent Timer::tick(uint64_t now) {
    if (state_ != TimerState::Running || mode_ == TimerMode::Stopwatch ||
        elapsed(now) < uint64_t(duration_seconds()) * 1000) return TimerEvent::None;
    if (mode_ == TimerMode::Countdown) {
        accumulated_ = uint64_t(duration_seconds()) * 1000; state_ = TimerState::Finished;
        return TimerEvent::Countdown;
    }
    const TimerEvent event = on_break_ ? TimerEvent::Break : TimerEvent::Focus;
    uint64_t overdue = elapsed(now) - uint64_t(duration_seconds()) * 1000;
    if (!on_break_) ++focus_sessions_;
    on_break_ = !on_break_;
    // Coalesce an unusually delayed tick; don't create a burst of old PC sounds.
    for (unsigned i = 0; i < 16 && overdue >= uint64_t(duration_seconds()) * 1000; ++i) {
        overdue -= uint64_t(duration_seconds()) * 1000;
        if (!on_break_) ++focus_sessions_;
        on_break_ = !on_break_;
    }
    accumulated_ = std::min<uint64_t>(overdue,uint64_t(duration_seconds()) * 1000 - 1);
    started_ = now;
    return event;
}
bool Timer::lap(uint64_t now) {
    if (mode_ != TimerMode::Stopwatch || state_ != TimerState::Running) return false;
    last_lap_ = elapsed(now); if (lap_count_ < UINT32_MAX) ++lap_count_;
    return true;
}

bool Pet::tick(Preferences& p, uint64_t now, int64_t epoch) {
    uint64_t minutes = now >= last_tick_ ? (now - last_tick_) / 60000 : 0;
    if (minutes) last_tick_ += minutes * 60000;
    if (epoch >= 1700000000 && !epoch_restored_) {
        epoch_restored_ = true;
        if (p.pet_epoch >= 1700000000 && epoch > p.pet_epoch)
            minutes = std::max<uint64_t>(minutes,std::min<int64_t>(epoch - p.pet_epoch,86400) / 60);
    }
    if (!minutes) return false;
    const uint32_t capped = std::min<uint64_t>(minutes,1440);
    food_minutes_ += capped; happy_minutes_ += capped;
    if (was_sleeping_ != bool(p.pet_sleeping)) { energy_minutes_ = 0; was_sleeping_ = p.pet_sleeping; }
    energy_minutes_ += capped;
    p.fullness = decrease(p.fullness,food_minutes_ / 10); food_minutes_ %= 10;
    p.happiness = decrease(p.happiness,happy_minutes_ / 15); happy_minutes_ %= 15;
    const uint32_t energy_period = p.pet_sleeping ? 2 : 15;
    p.energy = p.pet_sleeping ? increase(p.energy,energy_minutes_ / energy_period) :
                              decrease(p.energy,energy_minutes_ / energy_period);
    energy_minutes_ %= energy_period;
    if (epoch >= 1700000000) p.pet_epoch = epoch;
    return true;
}
bool Pet::pat(Preferences& p, uint64_t now) {
    if (patted_ && now - last_pat_ < 450) return false;
    patted_ = true; last_pat_ = now; reaction_until_ = now + 1300;
    p.happiness = increase(p.happiness,3);
    if (p.pats < UINT32_MAX) ++p.pats;
    return true;
}
void Pet::feed(Preferences& p) { p.fullness = increase(p.fullness,25); p.happiness = increase(p.happiness,3); }
void Pet::play(Preferences& p) {
    p.pet_sleeping = 0; p.happiness = increase(p.happiness,15);
    p.energy = decrease(p.energy,8); p.fullness = decrease(p.fullness,5);
}
void Pet::rest(Preferences& p) { p.pet_sleeping = !p.pet_sleeping; }
const char* Pet::mood(const Preferences& p) const {
    if (p.pet_sleeping) return "Dreaming of treats";
    if (p.fullness < 25) return "Snack time?";
    if (p.energy < 25) return "A cozy nap?";
    if (p.happiness < 25) return "More pats, please";
    return "Happy with you";
}
}

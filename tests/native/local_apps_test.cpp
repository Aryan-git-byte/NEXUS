#include "apps/local_apps.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace nexus::apps {
class AppsTest {
    static void check(bool condition, const char* reason) {
        if (!condition) throw std::runtime_error(reason);
    }
    static double number(const Calculator& c) { return std::strtod(c.display(),nullptr); }
    static void keys(Calculator& c, const char* const* sequence, size_t size) {
        c.press("C");
        for (size_t i = 0; i < size; ++i) c.press(sequence[i]);
    }
public:
    static void game2048() {
        Random random(43);
        Game2048 game;
        game.tiles_ = {1,1,1,1};
        const auto original = game.tiles_;
        check(game.move(Direction::Left,random),"2048 merge was ignored");
        check(game.tiles_[0] == 2 && game.tiles_[1] == 2 && game.score() == 8,
              "A tile merged twice in the same move");
        check(game.undo() && game.tiles_ == original && game.score() == 0,"2048 undo failed");
        check(!game.undo(),"2048 allowed repeated undo");
        game.tiles_ = {1,2,1,2,2,1,2,1,1,2,1,2,2,1,2,1};
        check(!game.can_move(),"A stuck 2048 board was not recognized");
        const auto stuck = game.tiles_;
        check(!game.move(Direction::Down,random) && game.tiles_ == stuck,"A no-op spawned a tile");
        check(!game.move(static_cast<Direction>(9),random) && game.tiles_ == stuck,"Invalid direction changed the board");
        game.tiles_ = {10,10}; game.score_ = 0;
        check(game.move(Direction::Left,random) && game.reached_2048(),"2048 winning merge failed");
        check(game.undo() && !game.reached_2048(),"Winning undo kept a false milestone");
        game.tiles_ = {1,1}; game.score_ = UINT32_MAX - 1;
        game.move(Direction::Left,random);
        check(game.score() == UINT32_MAX,"2048 score overflowed");
        for (unsigned seed = 1; seed <= 30; ++seed) {
            Random rng(seed); game.reset(rng);
            for (unsigned move = 0; move < 500 && game.can_move(); ++move) {
                game.move(static_cast<Direction>(rng.below(4)),rng);
                check(std::all_of(game.tiles_.begin(),game.tiles_.end(),[](uint8_t e) { return e <= 31; }),
                      "2048 tile exponent escaped bounds");
            }
        }
        std::puts("PASS: 2048 merges, undo, stuck boards, bounds and 30 seeded games");
    }
    static void snake() {
        Random random(12); Snake game; game.reset(random);
        game.food_ = 0;
        check(!game.turn(Direction::Left),"Snake accepted immediate reversal");
        check(game.turn(Direction::Up) && !game.turn(Direction::Left),"Snake accepted two turns before a step");
        check(game.step(random) && game.direction() == Direction::Up,"Snake ignored its queued turn");
        game.body_[0] = 21; game.body_[1] = 22; game.body_[2] = 42; game.body_[3] = 41;
        game.direction_ = game.queued_ = Direction::Down; game.food_ = 0; game.over_ = false;
        check(game.step(random) && game.head() == 41,"Snake cannot move into its departing tail");
        game.reset(random); game.body_[0] = 19; game.queued_ = Direction::Right;
        check(!game.step(random) && game.over(),"Snake did not detect a wall");
        game.reset(random); game.body_[0] = 21; game.body_[1] = 22;
        game.queued_ = Direction::Right; game.food_ = 0;
        check(!game.step(random) && game.over(),"Snake did not detect itself");
        game.reset(random); game.length_ = Snake::capacity - 1;
        for (uint16_t i = 0; i < game.length_; ++i) game.body_[i] = i + 1;
        game.queued_ = Direction::Left; game.food_ = 0;
        check(game.step(random) && game.won() && game.over() && game.length() == Snake::capacity,
              "Full-board Snake win overflowed the body array");
        for (unsigned i = 0; i < 3000; ++i) {
            if (game.over()) game.reset(random);
            game.turn(static_cast<Direction>(random.below(4))); game.step(random);
            check(game.length() >= 4 && game.length() <= Snake::capacity,"Snake length escaped bounds");
            check(game.won() || !game.occupies(game.food()),"Snake placed food inside its body");
        }
        std::puts("PASS: Snake turns, tail movement, collisions, food and a full-board win");
    }
    static void calculator() {
        Calculator c;
        const char* repeat[] = {"2","+","2","=","="};
        keys(c,repeat,5); check(number(c) == 6,"Calculator repeated equals failed");
        const char* percent[] = {"2","0","0","+","1","0","%","="};
        keys(c,percent,8); check(number(c) == 220,"Calculator additive percent failed");
        const char* multiply[] = {"2","0","0","*","1","0","%","="};
        keys(c,multiply,8); check(number(c) == 20,"Calculator multiplicative percent failed");
        const char* decimal[] = {".","5","+","1",".","2","5","="};
        keys(c,decimal,8); check(std::abs(number(c) - 1.75) < 1e-9,"Calculator decimal failed");
        const char* chain[] = {"1","+","2","*","3","="};
        keys(c,chain,6); check(number(c) == 9,"Calculator immediate arithmetic failed");
        const char* sign[] = {"5","+/-","+","7","="};
        keys(c,sign,5); check(number(c) == 2,"Calculator sign failed");
        const char* zero[] = {"1","/","0","="};
        keys(c,zero,4); check(std::strstr(c.display(),"divide"),"Divide by zero was not handled");
        c.press("7"); check(number(c) == 7,"Calculator cannot recover from an error");
        c.press("C"); for (unsigned i = 0; i < 100; ++i) c.press("8");
        check(std::strlen(c.display()) <= 15,"Calculator entry overflowed");
        c.press("C"); c.press("1"); c.press("2"); c.press("3"); c.press("DEL");
        check(number(c) == 12,"Calculator backspace failed");
        c.press("C"); c.press("1"); c.press("/");
        for (const char digit : std::string("100000000000000")) {
            const char key[2] = {digit,0}; c.press(key);
        }
        c.press("="); check(std::abs(number(c) - 1e-14) < 1e-26,"Calculator rounded a small valid result to zero");
        std::puts("PASS: calculator arithmetic, percentages, repeated equals and error recovery");
    }
    static void timers() {
        Timer timer;
        check(!timer.set_duration(0) && !timer.set_duration(86401),"Timer accepts invalid durations");
        check(timer.set_duration(60),"Countdown could not be configured");
        timer.start(1000);
        check(!timer.set_duration(1),"A running timer can be reconfigured accidentally");
        timer.pause(31000); check(timer.milliseconds(100000) == 30000,"Paused timer kept running");
        timer.start(100000);
        check(timer.tick(129999) == TimerEvent::None && timer.tick(130000) == TimerEvent::Countdown,
              "Countdown deadline shifted across pause/resume");
        check(timer.state() == TimerState::Finished && timer.milliseconds(200000) == 0 &&
              timer.tick(200000) == TimerEvent::None,"Countdown completed more than once");
        timer.reset(); timer.set_duration(1); timer.start(0); timer.pause(1000);
        check(timer.tick(1000) == TimerEvent::Countdown,"Pause at the deadline swallowed completion");
        timer.set_mode(TimerMode::Focus); timer.start(0);
        uint64_t now = 0;
        for (unsigned session = 1; session <= 4; ++session) {
            now += 1500000;
            check(timer.tick(now) == TimerEvent::Focus && timer.on_break(),"Focus did not start a break");
            check(timer.duration_seconds() == (session == 4 ? 900 : 300),"Focus break duration is wrong");
            now += uint64_t(timer.duration_seconds()) * 1000;
            check(timer.tick(now) == TimerEvent::Break && !timer.on_break(),"Break did not start focus");
        }
        check(timer.focus_sessions() == 4,"Focus sessions were lost");
        timer.set_mode(TimerMode::Stopwatch); timer.start(10); timer.lap(12345);
        check(timer.last_lap() == 12335 && timer.lap_count() == 1,"Stopwatch lap failed");
        timer.pause(20010); timer.start(50000);
        check(timer.milliseconds(55000) == 25000,"Stopwatch pause/resume lost elapsed time");
        check(timer.tick(90000000) == TimerEvent::None,"Stopwatch created a completion alert");
        std::puts("PASS: countdown deadlines, pause/resume, focus cycles, long break and stopwatch laps");
    }
    static void pet() {
        Preferences p; Pet pet; pet.initialize(0);
        for (unsigned minute = 1; minute <= 9; ++minute) pet.tick(p,minute * 60000,0);
        check(p.fullness == 80 && p.happiness == 80,"Pet decays on every short tick");
        pet.tick(p,600000,0); check(p.fullness == 79 && p.happiness == 80,"Pet hunger interval failed");
        pet.tick(p,900000,0); check(p.happiness == 79 && p.energy == 79,"Pet care intervals failed");
        Pet pats; Preferences q;
        check(pats.pat(q,0) && !pats.pat(q,100) && pats.pat(q,450),"Pet pat debounce failed");
        check(q.pats == 2 && q.happiness == 86 && pats.reacting(500),"Pet does not react to pats");
        q.energy = 50; pats.initialize(0); pats.rest(q); pats.tick(q,120000,0);
        check(q.energy == 51,"Sleeping pet did not recover energy");
        pats.feed(q); pats.play(q);
        check(q.fullness <= 100 && q.happiness <= 100 && !q.pet_sleeping,"Pet care escaped bounds");
        Preferences restored; restored.pet_epoch = 1700000000; Pet offline; offline.initialize(0);
        offline.tick(restored,1,1700021600);
        check(restored.fullness == 44 && restored.happiness == 56,"Offline pet recovery ignored elapsed time");
        check(valid_preferences(restored),"Valid pet record was rejected");
        restored.pet_name[0] = 1; check(!valid_preferences(restored),"Invalid pet name was accepted");
        restored = Preferences{}; restored.timer_mode = 9; check(!valid_preferences(restored),"Invalid app record was accepted");
        std::puts("PASS: pet pat reactions, care, slow decay, offline recovery and record validation");
    }
};
}
int main() {
    try {
        nexus::apps::AppsTest::game2048(); nexus::apps::AppsTest::snake();
        nexus::apps::AppsTest::calculator(); nexus::apps::AppsTest::timers(); nexus::apps::AppsTest::pet();
    } catch (const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what()); return 1;
    }
    return 0;
}

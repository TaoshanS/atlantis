// PCG32 pseudo-random generator. The whole state is two integers so it can be stored in
// save-slot snapshots and replayed deterministically (replaces Flash's Math.random).
#pragma once
#include <cstdint>

namespace sbso {

class Rng {
public:
    struct State {
        std::uint64_t state;
        std::uint64_t inc;
    };

    explicit Rng(std::uint64_t seed = 0x853c49e6748fea9bULL, std::uint64_t stream = 0xda3e39cb94b95bdbULL) {
        s_.state = 0;
        s_.inc = (stream << 1u) | 1u;
        next();
        s_.state += seed;
        next();
    }

    std::uint32_t next() {
        std::uint64_t old = s_.state;
        s_.state = old * 6364136223846793005ULL + s_.inc;
        std::uint32_t xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        std::uint32_t rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((~rot + 1u) & 31u));
    }

    // Uniform in [0, bound) without modulo bias.
    std::uint32_t below(std::uint32_t bound) {
        if (bound == 0) return 0;
        std::uint32_t threshold = (~bound + 1u) % bound;
        for (;;) {
            std::uint32_t r = next();
            if (r >= threshold) return r % bound;
        }
    }

    // Uniform in [0, 1), like Math.random().
    double unit() { return (static_cast<double>(next()) * 4294967296.0 + next()) / 18446744073709551616.0; }

    State state() const { return s_; }
    void set_state(State s) { s_ = s; }

private:
    State s_{};
};

}  // namespace sbso

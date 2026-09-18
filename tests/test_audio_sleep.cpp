#include "audio_sleep.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <utility>
#include <vector>

using namespace audio_sleep;
template <std::size_t N> void exercise(const Register (&sequence)[N]) {
    std::array<uint8_t, 256> registers{};
    std::vector<std::pair<uint8_t, uint8_t>> writes;
    int reads = 0, fail_read = -1, fail_write = -1;
    bool ignore_writes = false;
    auto read = [&](uint8_t reg, uint8_t &value) {
        if (reads++ == fail_read) return false;
        value = registers[reg]; return true;
    };
    auto write = [&](uint8_t reg, uint8_t value) {
        if (int(writes.size()) == fail_write) return false;
        writes.emplace_back(reg, value);
        if (!ignore_writes) registers[reg] = value;
        return true;
    };
    assert(apply(sequence, read, write).status == Status::applied);
    assert(writes.size() == N); // Intermediate transitions must not be dropped.
    for (std::size_t i = 0; i < N; ++i)
        assert(writes[i] == std::make_pair(sequence[i].address, sequence[i].value));
    writes.clear();
    assert(apply(sequence, read, write).status == Status::already_asleep);
    assert(writes.empty()); // Repeated deep-sleep wakeups do not reactivate clocks.

    // A failed initial read must never become a write based on fabricated zeros.
    registers.fill(0); reads = 0; fail_read = 1;
    assert(apply(sequence, read, write).status == Status::read_failed);
    assert(writes.empty());
    fail_read = -1; reads = 0; fail_write = 2;
    assert(apply(sequence, read, write).status == Status::write_failed);
    assert(writes.size() == 2); // Stop after a bus failure, no unbounded retry.
    fail_write = -1; writes.clear(); registers.fill(0); ignore_writes = true;
    assert(apply(sequence, read, write).status == Status::verify_failed);
    assert(writes.size() == N); // ACK without applying data is not success.
    ignore_writes = false; writes.clear(); reads = 0;
    // Count unique final registers to fail the first readback after writing.
    std::array<bool, 256> seen{}; int unique = 0;
    for (const auto &r : sequence) if (!seen[r.address]) { seen[r.address] = true; ++unique; }
    fail_read = unique;
    assert(apply(sequence, read, write).status == Status::read_failed);
    assert(writes.size() == N);
}
int main() {
    exercise(es8311);
    exercise(es7210);
    // Read-only/reserved bits outside each verification mask are ignored.
    constexpr Register masked[] = {{0x01, 0x00, 0x3f}};
    bool wrote = false;
    assert(apply(masked, [](uint8_t, uint8_t &v) { v = 0xc0; return true; },
                 [&](uint8_t, uint8_t) { wrote = true; return true; }).status == Status::already_asleep);
    assert(!wrote);
    // Real ES7210 readback clears reserved bits even after writing 0xff.
    std::array<uint8_t, 256> chip{};
    auto read_chip = [&](uint8_t r, uint8_t &v) { v = chip[r]; return true; };
    auto write_chip = [&](uint8_t r, uint8_t v) {
        if (r == 0x47 || r == 0x49) v &= 0x3f;
        if (r == 0x48 || r == 0x4a) v &= 0x1f;
        chip[r] = v; return true;
    };
    assert(apply(es7210, read_chip, write_chip).status == Status::applied);
    assert(apply(es7210, read_chip, write_chip).status == Status::already_asleep);
    chip[0x4b] &= ~0x40; // An enabled MICBIAS must still trigger a real power-down.
    assert(apply(es7210, read_chip, write_chip).status == Status::applied);
    puts("Audio standby: idempotence, ordered transitions and I2C failure tests passed");
}

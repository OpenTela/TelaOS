/**
 * Test: BinStream — shared BIN_CHAR receive transport.
 *
 * Covers the framing that BinReceive (app push) and OtaReceive both rely on:
 * in-order reassembly, write offsets, completion detection, and the error
 * paths (out-of-order, overflow, too-small). Pure logic, no ESP stubs.
 */
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include "ble/bin_stream.h"

#define TEST(name) printf("  %-52s ", name); total++;
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) printf("FAIL %s\n", msg)

// Frame a payload slice as [2B chunk_id LE][data] and feed it.
static void feed(BinStream& s, uint16_t id, const uint8_t* data, uint32_t len) {
    std::vector<uint8_t> pkt;
    pkt.push_back(id & 0xFF);
    pkt.push_back((id >> 8) & 0xFF);
    pkt.insert(pkt.end(), data, data + len);
    s.onChunk(pkt.data(), (uint32_t)pkt.size());
}

int main() {
    printf("=== BinStream Tests ===\n\n");
    int passed = 0, total = 0;

    // Source pattern to transfer.
    const uint32_t N = 1000;
    uint8_t src[N];
    for (uint32_t i = 0; i < N; i++) src[i] = (uint8_t)(i * 31 + 7);

    printf("happy path:\n");

    TEST("reassembles in-order chunks exactly");
    {
        uint8_t dst[N] = {0};
        BinStream s;
        s.begin(N, [&](const uint8_t* d, uint32_t l) { memcpy(dst + s.received(), d, l); });
        const uint32_t CH = 250;
        uint16_t id = 0;
        for (uint32_t off = 0; off < N; off += CH, id++) {
            uint32_t l = (N - off < CH) ? (N - off) : CH;
            feed(s, id, src + off, l);
        }
        if (s.isComplete() && s.received() == N && memcmp(dst, src, N) == 0) PASS();
        else { FAIL(""); printf("      received=%u complete=%d\n", s.received(), s.isComplete()); }
    }

    TEST("not complete before all bytes arrive");
    {
        uint8_t dst[N] = {0};
        BinStream s;
        s.begin(N, [&](const uint8_t* d, uint32_t l) { memcpy(dst + s.received(), d, l); });
        feed(s, 0, src, 250);
        if (s.isActive() && !s.isComplete() && s.received() == 250) PASS();
        else FAIL("");
    }

    TEST("single-chunk transfer completes");
    {
        uint8_t dst[10] = {0};
        BinStream s;
        s.begin(5, [&](const uint8_t* d, uint32_t l) { memcpy(dst + s.received(), d, l); });
        const uint8_t five[5] = {1,2,3,4,5};
        feed(s, 0, five, 5);
        if (s.isComplete() && memcmp(dst, five, 5) == 0) PASS();
        else FAIL("");
    }

    printf("\nerror paths:\n");

    TEST("out-of-order chunk triggers error + deactivates");
    {
        bool err = false;
        BinStream s;
        s.begin(N, [&](const uint8_t*, uint32_t) {},
                [&](BinStream::Error e) { err = (e == BinStream::Error::OutOfOrder); });
        feed(s, 0, src, 250);
        feed(s, 5, src + 250, 250);   // gap: expected 1, got 5
        if (err && !s.isActive()) PASS();
        else { FAIL(""); printf("      err=%d active=%d\n", err, s.isActive()); }
    }

    TEST("overflow (more than expected) triggers error");
    {
        bool err = false;
        BinStream s;
        s.begin(300, [&](const uint8_t*, uint32_t) {},
                [&](BinStream::Error e) { err = (e == BinStream::Error::Overflow); });
        feed(s, 0, src, 250);
        feed(s, 1, src + 250, 250);   // 250 + 250 > 300
        if (err && !s.isActive()) PASS();
        else FAIL("");
    }

    TEST("too-small chunk (header only) triggers error");
    {
        bool err = false;
        BinStream s;
        s.begin(N, [&](const uint8_t*, uint32_t) {},
                [&](BinStream::Error e) { err = (e == BinStream::Error::TooSmall); });
        uint8_t hdrOnly[2] = {0, 0};   // 2 bytes, not the end marker
        s.onChunk(hdrOnly, 2);
        if (err && !s.isActive()) PASS();
        else FAIL("");
    }

    TEST("end marker [0xFF,0xFF] is benign (ignored, no error)");
    {
        // OBSOLETE: compat shim for pre-PR#1 clients. Remove together with the
        // marker branch in bin_stream.cpp once the field has migrated.
        bool err = false;
        uint8_t dst[N] = {0};
        BinStream s;
        s.begin(500, [&](const uint8_t* d, uint32_t l) { memcpy(dst + s.received(), d, l); },
                [&](BinStream::Error) { err = true; });
        feed(s, 0, src, 250);
        feed(s, 1, src + 250, 250);            // now complete (500/500)
        uint8_t marker[2] = {0xFF, 0xFF};
        s.onChunk(marker, 2);                  // trailing marker after completion
        // marker must NOT raise an error, NOT advance count, transfer stays complete
        if (!err && s.isComplete() && s.received() == 500 && memcmp(dst, src, 500) == 0) PASS();
        else { FAIL(""); printf("      err=%d complete=%d received=%u\n", err, s.isComplete(), s.received()); }
    }

    TEST("chunks after error are ignored (no crash)");
    {
        BinStream s;
        s.begin(N, [&](const uint8_t*, uint32_t) {},
                [&](BinStream::Error) {});
        feed(s, 9, src, 250);          // immediate out-of-order
        feed(s, 10, src, 250);         // should be a no-op on inactive stream
        if (!s.isActive() && s.received() == 0) PASS();
        else FAIL("");
    }

    printf("\nlifecycle:\n");

    TEST("reset() disarms and zeroes counters");
    {
        BinStream s;
        s.begin(N, [&](const uint8_t*, uint32_t) {});
        feed(s, 0, src, 250);
        s.reset();
        if (!s.isActive() && s.received() == 0 && !s.isComplete()) PASS();
        else FAIL("");
    }

    TEST("re-begin after completion works");
    {
        uint8_t dst[10] = {0};
        BinStream s;
        const uint8_t a[3] = {1,2,3};
        s.begin(3, [&](const uint8_t* d, uint32_t l) { memcpy(dst + s.received(), d, l); });
        feed(s, 0, a, 3);
        bool first = s.isComplete();
        const uint8_t b[2] = {9,8};
        s.begin(2, [&](const uint8_t* d, uint32_t l) { memcpy(dst + s.received(), d, l); });
        feed(s, 0, b, 2);
        if (first && s.isComplete() && s.received() == 2) PASS();
        else FAIL("");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d BINSTREAM TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d BINSTREAM TESTS PASSED ===\n", passed, total);
    return 1;
}

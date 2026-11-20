/**
 * Test: a fetch reply after the app is gone finds no callback.
 *
 * net.fetch leaves its callback in BLEBridge until the reply comes; the callback holds the app's
 * Lua state. When the app closes (ScriptManager::shutdown closes that state), the callback must
 * go too: a late reply would call into freed memory. The test's own callbacks hold a token: its
 * use count shows whether BLEBridge still keeps them.
 */
#include <cstdio>
#include <memory>
#include "ble/ble_bridge.h"
#include "core/script_manager.h"
#include "utils/log_config.h"

#define TEST(name) printf("  %-56s ", name); total++;
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) printf("FAIL %s\n", msg)

static int fetchHolding(const std::shared_ptr<int>& token) {
    P::Array<P::String> fields;
    return BLEBridge::sendFetchRequest("GET", "https://example.com/weather", nullptr, false, "json", fields,
                                       [token](int, const P::String&) {});
}

int main() {
    printf("=== Fetch after the app is gone ===\n\n");
    int passed = 0, total = 0;
    Log::setAll(Log::Disabled);

    BLEBridge::init("test");
    NimBLEConnInfo info;
    NimBLEServer* server = NimBLEDevice::createServer();
    if (server->callbacks)
        server->callbacks->onConnect(server, info);  // a client: the phone

    TEST("a fetch on its way keeps its callback");
    auto token = std::make_shared<int>(0);
    const int id = fetchHolding(token);
    if (BLEBridge::isConnected() && id > 0 && token.use_count() == 2)
        PASS();
    else
        FAIL("");

    TEST("the app closes: its callbacks go");
    {
        ScriptManager scripts;
        scripts.shutdown();
        if (token.use_count() == 1)
            PASS();
        else
            FAIL("");
    }

    TEST("a fetch of the next app keeps its own");
    auto next = std::make_shared<int>(0);
    if (fetchHolding(next) > id && next.use_count() == 2)
        PASS();
    else
        FAIL("");

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d FETCH DROP TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d FETCH DROP TESTS PASSED ===\n", passed, total);
    return 1;
}

/**
 * Test: BLE pairing with a code (bluetooth.pairing).
 *
 * The NimBLE stub (ci/stubs/NimBLEDevice.h) keeps what BLEBridge asked of it: the security, the
 * properties of each characteristic, the server's callbacks. The test calls those as the NimBLE
 * host task would on the watch: a phone connects, asks for the code, the pairing ends.
 */
#include <cstdio>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "NimBLEDevice.h"
#include "ble/ble_bridge.h"
#include "console/console.h"
#include "utils/log_config.h"

#define TEST(name) printf("  %-56s ", name); total++;
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) printf("FAIL %s\n", msg)

static const char* RX = "12345678-1234-5678-1234-56789abcdef2";
static const char* TX = "12345678-1234-5678-1234-56789abcdef1";
static const char* BIN = "12345678-1234-5678-1234-56789abcdef3";

static bool has(const char* uuid, uint32_t flags) {
    return (g_nimble_mock.properties[uuid] & flags) == flags;
}

// The label showing `code` as on the watch: «123 456»
static LvglMock::Widget* codeLabel(uint32_t code) {
    std::string digits = std::to_string(code % 1000000 + 1000000).substr(1);
    digits.insert(3, " ");
    for (auto* w : LvglMock::g_all) {
        if (w->type == "Label" && w->text == digits)
            return w;
    }
    return nullptr;
}

static bool shown(const LvglMock::Widget* w) {
    if (!w)
        return false;
    for (; w; w = w->parent) {
        if (w->hidden)
            return false;
    }
    return true;
}

int main() {
    printf("=== BLE Pairing Tests ===\n\n");
    int passed = 0, total = 0;
    Log::setAll(Log::Disabled);
    LvglMock::create_screen(480, 480);
    NimBLEServer* server = NimBLEDevice::createServer();

    printf("pairing off (as it was):\n");
    BLEBridge::init("T");
    TEST("no security asked, the characteristics open");
    {
        const uint32_t sec = NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC;
        const bool noSecurity = g_nimble_mock.ioCap == -1 && !g_nimble_mock.bonding;
        const bool open = !(g_nimble_mock.properties[RX] & sec) && !(g_nimble_mock.properties[TX] & sec);
        if (noSecurity && open)
            PASS();
        else
            FAIL("");
    }
    TEST("a phone connects: no pairing asked");
    {
        NimBLEConnInfo c;
        g_nimble_mock.serverCallbacks->onConnect(server, c);
        const bool noPairing = g_nimble_mock.securityStarted == 0;
        if (noPairing && BLEBridge::isConnected())
            PASS();
        else
            FAIL("");
        g_nimble_mock.serverCallbacks->onDisconnect(server, c, 0);
    }
    BLEBridge::deinit();

    printf("\npairing on:\n");
    BLEBridge::setPairing(true);
    BLEBridge::init("T");
    TEST("bonding, MITM, secure connections; the watch displays");
    {
        const bool secure = g_nimble_mock.bonding && g_nimble_mock.mitm && g_nimble_mock.sc;
        const bool displays = g_nimble_mock.ioCap == BLE_HS_IO_DISPLAY_ONLY;
        if (secure && displays)
            PASS();
        else
            FAIL("");
    }
    TEST("RX, TX and BIN: only encrypted and authenticated");
    {
        const uint32_t w = NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN;
        const uint32_t r = NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN;
        const bool rx = has(RX, w | NIMBLE_PROPERTY::WRITE);
        const bool tx = has(TX, r | NIMBLE_PROPERTY::NOTIFY);
        if (rx && tx && has(BIN, w | r))
            PASS();
        else
            FAIL("");
    }
    TEST("a phone connects: the watch asks for security at once");
    NimBLEConnInfo phone;
    {
        g_nimble_mock.serverCallbacks->onConnect(server, phone);
        if (g_nimble_mock.securityStarted == 1 && !BLEBridge::isSecure())
            PASS();
        else
            FAIL("");
    }
    uint32_t code = 0;
    TEST("it asks for the code: six digits, on the screen");
    {
        code = g_nimble_mock.serverCallbacks->onPassKeyDisplay();
        BLEBridge::processBleQueue();
        auto* label = codeLabel(code);
        if (code < 1000000 && label && shown(label))
            PASS();
        else {
            FAIL("");
            printf("      code=%u\n", (unsigned)code);
        }
    }
    TEST("wrong code: the link is dropped, the code hidden");
    {
        NimBLEConnInfo failed = phone;  // the pairing ended, the link not encrypted
        g_nimble_mock.serverCallbacks->onAuthenticationComplete(failed);
        BLEBridge::processBleQueue();
        const bool dropped = g_nimble_mock.disconnects == 1 && !BLEBridge::isSecure();
        if (dropped && !shown(codeLabel(code)))
            PASS();
        else
            FAIL("");
        g_nimble_mock.serverCallbacks->onDisconnect(server, failed, 0);
    }
    TEST("the next pairing: a new code");
    {
        g_nimble_mock.serverCallbacks->onConnect(server, phone);
        const uint32_t next = g_nimble_mock.serverCallbacks->onPassKeyDisplay();
        BLEBridge::processBleQueue();
        // a CSPRNG may give the same six digits again, one time in a million
        if (next != code && shown(codeLabel(next)))
            PASS();
        else
            FAIL("");
        code = next;
    }
    TEST("right code: encrypted, bonded, the code hidden");
    {
        NimBLEConnInfo paired = phone;
        paired.encrypted = paired.authenticated = paired.bonded = true;
        g_nimble_mock.serverCallbacks->onAuthenticationComplete(paired);
        BLEBridge::processBleQueue();
        const bool kept = g_nimble_mock.disconnects == 1 && BLEBridge::isSecure();
        if (kept && !shown(codeLabel(code)))
            PASS();
        else
            FAIL("");
    }
    TEST("encrypted without the code (Just Works): dropped");
    {
        NimBLEConnInfo weak = phone;
        weak.encrypted = true;  // "Just Works": encrypted, but nobody checked the code
        g_nimble_mock.serverCallbacks->onAuthenticationComplete(weak);
        if (g_nimble_mock.disconnects == 2 && !BLEBridge::isSecure())
            PASS();
        else
            FAIL("");
        g_nimble_mock.serverCallbacks->onDisconnect(server, weak, 0);
    }

    printf("\nsys ble:\n");
    TEST("sys ble: what is on");
    {
        auto r = Console::exec("sys ble");
        if (r.success)
            PASS();
        else
            FAIL("");
    }
    TEST("sys ble forget: all the bonds");
    {
        g_nimble_mock.bonds = 2;
        auto r = Console::exec("sys ble forget");
        const bool forgotten = g_nimble_mock.bonds == 0 && BLEBridge::bonds() == 0;
        if (r.success && forgotten)
            PASS();
        else
            FAIL("");
    }
    TEST("sys ble forget with BLE off: an error, nothing forgotten");
    {
        BLEBridge::deinit();
        g_nimble_mock.bonds = 1;
        auto r = Console::exec("sys ble forget");
        const bool refused = !r.success && r.errorCode == "invalid";
        const bool kept = g_nimble_mock.bonds == 1 && BLEBridge::bonds() == -1;
        if (refused && kept)
            PASS();
        else
            FAIL("");
    }
    TEST("sys ble <nonsense>: invalid");
    {
        auto r = Console::exec("sys ble pair");
        if (!r.success && r.errorCode == "invalid")
            PASS();
        else
            FAIL("");
    }

    TEST("BLE off while the code is on the screen: the code goes");
    {
        BLEBridge::init("T");
        g_nimble_mock.serverCallbacks->onConnect(server, phone);
        const uint32_t c = g_nimble_mock.serverCallbacks->onPassKeyDisplay();
        BLEBridge::processBleQueue();
        const bool was = shown(codeLabel(c));
        BLEBridge::deinit();
        if (was && !shown(codeLabel(c)))
            PASS();
        else
            FAIL("");
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d BLE PAIRING TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d BLE PAIRING TESTS PASSED ===\n", passed, total);
    return 1;
}

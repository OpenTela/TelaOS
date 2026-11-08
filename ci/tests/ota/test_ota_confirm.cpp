/**
 * Test: OtaConfirm — a new firmware is kept once it has run 20 s.
 *
 * Against the esp_ota mock: the running partition's otadata state, the confirmation
 * (esp_ota_mark_app_valid_cancel_rollback), the partition rolled back from.
 */
#include <cstdio>
#include <cstring>
#include "ota/ota_confirm.h"
#include "esp_ota_ops.h"
#include "utils/log_config.h"

extern "C" bool verifyRollbackLater();

#define TEST(name) printf("  %-56s ", name); total++;
#define PASS() do { printf("PASS\n"); passed++; } while(0)
#define FAIL(msg) printf("FAIL %s\n", msg)

int main() {
    printf("=== OtaConfirm Tests ===\n\n");
    int passed = 0, total = 0;
    Log::setAll(Log::Disabled);

    TEST("Arduino leaves the confirmation to TelaOS");
    {
        if (verifyRollbackLater())
            PASS();
        else
            FAIL("");
    }

    TEST("a confirmed firmware: nothing to do");
    {
        OtaMock::reset();
        OtaConfirm::begin();
        OtaConfirm::tick(60000);
        const bool stateValid = !strcmp(OtaConfirm::state(), "valid");
        if (OtaMock::markedValid == 0 && stateValid)
            PASS();
        else
            FAIL("");
    }

    TEST("pending: not before 20 s");
    {
        OtaMock::reset();
        OtaMock::runningState = ESP_OTA_IMG_PENDING_VERIFY;
        OtaConfirm::begin();
        OtaConfirm::tick(5000);
        OtaConfirm::tick(19999);
        const bool statePending = !strcmp(OtaConfirm::state(), "pending");
        if (OtaMock::markedValid == 0 && statePending)
            PASS();
        else
            FAIL("");
    }

    TEST("pending: confirmed at 20 s, once");
    {
        OtaConfirm::tick(20000);
        OtaConfirm::tick(20005);
        OtaConfirm::tick(90000);
        const bool stateValid = !strcmp(OtaConfirm::state(), "valid");
        if (OtaMock::markedValid == 1 && stateValid)
            PASS();
        else
            FAIL("");
    }

    TEST("flashed over USB (boot_app0.bin: undefined): never confirmed");
    {
        OtaMock::reset();
        OtaMock::runningState = ESP_OTA_IMG_UNDEFINED;
        OtaConfirm::begin();
        OtaConfirm::tick(60000);
        const bool stateUndefined = !strcmp(OtaConfirm::state(), "undefined");
        if (OtaMock::markedValid == 0 && stateUndefined)
            PASS();
        else
            FAIL("");
    }

    TEST("no record in otadata: never confirmed");
    {
        OtaMock::reset();
        OtaMock::runningKnown = false;
        OtaConfirm::begin();
        OtaConfirm::tick(60000);
        const bool stateNone = !strcmp(OtaConfirm::state(), "none");
        if (OtaMock::markedValid == 0 && stateNone)
            PASS();
        else
            FAIL("");
    }

    TEST("the partition rolled back from, as of begin()");
    {
        OtaMock::reset();
        OtaConfirm::begin();
        const bool none = OtaConfirm::invalidPartition() == nullptr;
        OtaMock::invalid = &OtaMock::part;  // app1
        OtaConfirm::begin();
        const char* bad = OtaConfirm::invalidPartition();
        if (none && bad && !strcmp(bad, "app1"))
            PASS();
        else
            FAIL("");
        OtaMock::reset();
    }

    printf("\n");
    if (passed == total) {
        printf("=== ALL %d OTACONFIRM TESTS PASSED ===\n", total);
        return 0;
    }
    printf("=== %d/%d OTACONFIRM TESTS PASSED ===\n", passed, total);
    return 1;
}

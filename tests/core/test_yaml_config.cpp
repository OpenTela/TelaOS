/**
 * Test: YamlConfig
 * YAML-backed config — define, load from VFS, save to VFS, set with auto-save
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <LittleFS.h>
#include "core/yaml_config.h"

#define TEST(name) printf("  %-50s ", name); total++;
#define PASS() do { printf("✓\n"); passed++; } while(0)
#define FAIL(msg) printf("✗ %s\n", msg)

static std::string readVFS(const char* path) {
    auto it = VFS::store().files.find(path);
    if (it == VFS::store().files.end()) return "";
    return std::string(it->second.begin(), it->second.end());
}

int main() {
    printf("=== YamlConfig Tests ===\n\n");
    int passed = 0, total = 0;

    // === Define ===
    printf("Define:\n");

    TEST("define + get defaults") {
        VFS::reset();
        YamlConfig cfg("/test.yml", "test");
        cfg.define("display.brightness", VarType::Int, 255);
        cfg.define("power.auto_sleep", VarType::Bool, true);
        cfg.define("user.name", VarType::String, P::String("default"));
        if (cfg.getInt("display.brightness") == 255 &&
            cfg.getBool("power.auto_sleep") == true &&
            cfg.getString("user.name") == "default")
            PASS(); else FAIL("");
    }

    // === Load ===
    printf("\nLoad:\n");

    TEST("load overrides defaults from file") {
        VFS::reset();
        VFS::writeFile("/cfg.yml",
            "display:\n"
            "  brightness: 128\n"
            "power:\n"
            "  auto_sleep: false\n"
        );
        YamlConfig cfg("/cfg.yml", "test");
        cfg.define("display.brightness", VarType::Int, 255);
        cfg.define("power.auto_sleep", VarType::Bool, true);
        cfg.load();
        if (cfg.getInt("display.brightness") == 128 && cfg.getBool("power.auto_sleep") == false)
            PASS();
        else { FAIL(""); printf("      bright=%d sleep=%d\n",
            cfg.getInt("display.brightness"), cfg.getBool("power.auto_sleep")); }
    }

    TEST("load partial — undefined keys ignored") {
        VFS::reset();
        VFS::writeFile("/partial.yml",
            "display:\n"
            "  brightness: 100\n"
            "  unknown_key: 999\n"
        );
        YamlConfig cfg("/partial.yml", "test");
        cfg.define("display.brightness", VarType::Int, 255);
        cfg.load();
        // brightness loaded, unknown ignored
        if (cfg.getInt("display.brightness") == 100) PASS();
        else { FAIL(""); printf("      got=%d\n", cfg.getInt("display.brightness")); }
    }

    TEST("load missing file — returns false, defaults kept") {
        VFS::reset();
        YamlConfig cfg("/missing.yml", "test");
        cfg.define("x", VarType::Int, 42);
        bool ok = cfg.load();
        if (!ok && cfg.getInt("x") == 42) PASS(); else FAIL("");
    }

    TEST("load string values") {
        VFS::reset();
        VFS::writeFile("/str.yml",
            "user:\n"
            "  name: Alice\n"
        );
        YamlConfig cfg("/str.yml", "test");
        cfg.define("user.name", VarType::String, P::String(""));
        cfg.load();
        if (cfg.getString("user.name") == "Alice") PASS();
        else FAIL(cfg.getString("user.name").c_str());
    }

    TEST("load float values") {
        VFS::reset();
        VFS::writeFile("/fl.yml",
            "sensor:\n"
            "  threshold: 3.14\n"
        );
        YamlConfig cfg("/fl.yml", "test");
        cfg.define("sensor.threshold", VarType::Float, 0.0f);
        cfg.load();
        float v = cfg.getFloat("sensor.threshold");
        if (v > 3.13f && v < 3.15f) PASS();
        else { FAIL(""); printf("      got=%.2f\n", v); }
    }

    TEST("load with comments") {
        VFS::reset();
        VFS::writeFile("/comments.yml",
            "# This is a comment\n"
            "display:\n"
            "  brightness: 200  # inline comment\n"
        );
        YamlConfig cfg("/comments.yml", "test");
        cfg.define("display.brightness", VarType::Int, 0);
        cfg.load();
        if (cfg.getInt("display.brightness") == 200) PASS();
        else { FAIL(""); printf("      got=%d\n", cfg.getInt("display.brightness")); }
    }

    // === Save ===
    printf("\nSave:\n");

    TEST("save creates file in VFS") {
        VFS::reset();
        YamlConfig cfg("/out.yml", "test");
        cfg.define("display.brightness", VarType::Int, 128);
        cfg.save();
        if (VFS::exists("/out.yml")) PASS(); else FAIL("file not created");
    }

    TEST("save content is valid YAML") {
        VFS::reset();
        YamlConfig cfg("/out.yml", "test");
        cfg.define("display.brightness", VarType::Int, 128);
        cfg.define("power.auto_sleep", VarType::Bool, true);
        cfg.save();
        std::string content = readVFS("/out.yml");
        // Should contain section headers and values
        bool hasBrightness = content.find("brightness: 128") != std::string::npos;
        bool hasSleep = content.find("auto_sleep: true") != std::string::npos;
        if (hasBrightness && hasSleep) PASS();
        else { FAIL(""); printf("      content: '%s'\n", content.c_str()); }
    }

    TEST("save creates parent directory") {
        VFS::reset();
        YamlConfig cfg("/system/config.yml", "test");
        cfg.define("x", VarType::Int, 1);
        cfg.save();
        if (VFS::exists("/system")) PASS(); else FAIL("parent not created");
    }

    // === Roundtrip ===
    printf("\nRoundtrip:\n");

    TEST("save then load preserves values") {
        VFS::reset();
        {
            YamlConfig cfg("/rt.yml", "test");
            cfg.define("display.brightness", VarType::Int, 255);
            cfg.define("power.auto_sleep", VarType::Bool, false);
            cfg.define("user.name", VarType::String, P::String("Bob"));
            cfg.save();
        }
        {
            YamlConfig cfg2("/rt.yml", "test");
            cfg2.define("display.brightness", VarType::Int, 0);
            cfg2.define("power.auto_sleep", VarType::Bool, true);
            cfg2.define("user.name", VarType::String, P::String(""));
            cfg2.load();
            if (cfg2.getInt("display.brightness") == 255 &&
                cfg2.getBool("power.auto_sleep") == false &&
                cfg2.getString("user.name") == "Bob")
                PASS();
            else {
                FAIL("");
                printf("      bright=%d sleep=%d name='%s'\n",
                    cfg2.getInt("display.brightness"),
                    cfg2.getBool("power.auto_sleep"),
                    cfg2.getString("user.name").c_str());
            }
        }
    }

    // === Set with auto-save ===
    printf("\nAuto-save:\n");

    TEST("set() auto-saves to file") {
        VFS::reset();
        YamlConfig cfg("/auto.yml", "test");
        cfg.define("display.brightness", VarType::Int, 255);
        cfg.set("display.brightness", 100);
        // File should exist now
        std::string content = readVFS("/auto.yml");
        bool has100 = content.find("100") != std::string::npos;
        if (has100) PASS();
        else { FAIL(""); printf("      content: '%s'\n", content.c_str()); }
    }

    TEST("set string auto-saves") {
        VFS::reset();
        YamlConfig cfg("/auto2.yml", "test");
        cfg.define("user.name", VarType::String, P::String("old"));
        cfg.set("user.name", "new_name");
        // Reload from file
        YamlConfig cfg2("/auto2.yml", "test");
        cfg2.define("user.name", VarType::String, P::String(""));
        cfg2.load();
        if (cfg2.getString("user.name") == "new_name") PASS();
        else FAIL(cfg2.getString("user.name").c_str());
    }

    // === Summary ===
    printf("\n");
    if (passed == total) {
        printf("=== ALL %d YAML CONFIG TESTS PASSED ===\n", total);
        return 0;
    } else {
        printf("=== %d/%d YAML CONFIG TESTS PASSED ===\n", passed, total);
        return 1;
    }
}

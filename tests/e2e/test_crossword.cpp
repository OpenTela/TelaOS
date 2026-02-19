/**
 * Test: Crossword E2E — original HTML, full user emulation
 *
 * Grid cells have IDs → ui click c12
 * Keyboard/control buttons have no IDs → ui call kM, ui call nextWord
 * State reads → get("v00")
 *
 * The ONLY modification to the app: one print() line before navigate("/win")
 * for on-device Serial Monitor diagnostics.
 *
 *   Grid:
 *     0 1 2 3 4
 *   0 М А К . .    → МАК  (word 1, "Цветок")
 *   1 . . О . .    ↓ КОТ  (word 2, "Питомец")
 *   2 . . Т О К   → ТОК  (word 3, "Электричество")
 */
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "lvgl.h"
#include "lvgl_mock.h"
#include "ui/ui_engine.h"
#include "ui/ui_touch.h"
#include "engines/lua/lua_engine.h"
#include "core/state_store.h"
#include "console/console.h"

// ═══════════════════════════════════════════════════════
//  Test infra
// ═══════════════════════════════════════════════════════

static int g_passed = 0, g_total = 0;
#define TEST(name)     printf("  %-58s ", name); g_total++;
#define PASS()         do { printf("✓\n"); g_passed++; } while(0)
#define FAIL(msg)      printf("✗ %s\n", msg)
#define FAIL_V(f, ...) do { printf("✗ "); printf(f, __VA_ARGS__); printf("\n"); } while(0)
#define SECTION(name)  printf("\n%s:\n", name)

// Grid cells have IDs → click by ID
static void click(const char* id) {
    char buf[64];
    snprintf(buf, sizeof(buf), "ui click %s", id);
    Console::exec(buf);
}
// Keyboard/control buttons have no IDs → call Lua function
static void call(const char* func) {
    char buf[64];
    snprintf(buf, sizeof(buf), "ui call %s", func);
    Console::exec(buf);
}
static std::string get(const char* var) {
    return State::store().getString(var);
}
static std::string page() {
    auto p = UI::Engine::instance().currentPageId();
    return p ? p : "";
}

// Stdout capture
static int s_pipefd[2];
static int s_saved_stdout;
static void capture_start() {
    fflush(stdout);
    pipe(s_pipefd);
    s_saved_stdout = dup(STDOUT_FILENO);
    dup2(s_pipefd[1], STDOUT_FILENO);
}
static std::string capture_end() {
    fflush(stdout);
    dup2(s_saved_stdout, STDOUT_FILENO);
    close(s_saved_stdout);
    close(s_pipefd[1]);
    char buf[1024] = {};
    ssize_t n = read(s_pipefd[0], buf, sizeof(buf) - 1);
    close(s_pipefd[0]);
    return n > 0 ? std::string(buf, n) : "";
}

#define U_M "\xD0\x9C"
#define U_A "\xD0\x90"
#define U_K "\xD0\x9A"
#define U_O "\xD0\x9E"
#define U_T "\xD0\xA2"

// ═══════════════════════════════════════════════════════
//  Original app HTML (1 diagnostic print line added)
// ═══════════════════════════════════════════════════════

static const char* CROSSWORD_HTML = R"(
<app version="1.0" category="game" icon="system:puzzle-game">
  <ui default="/main">
    <page id="main" bgcolor="#000">
      <!-- Определение -->
      <label x="2%" y="1%" w="96%" h="36" bgcolor="#1c1c1c"/>
      <label x="4%" y="2%" w="10%" color="#ff9500" font="16">{dir}</label>
      <label x="14%" y="2%" w="84%" color="#FFF" font="16">{hint}</label>
      
      <!-- Сетка 5x5 -->
      <!-- Row 0: М А К . . -->
      <button id="c00" x="5%" y="17%" w="17%" h="28" bgcolor="#333" color="#FFF" radius="0" onclick="tap00">{v00}</button>
      <button id="c01" x="23%" y="17%" w="17%" h="28" bgcolor="#333" color="#FFF" radius="0" onclick="tap01">{v01}</button>
      <button id="c02" x="41%" y="17%" w="17%" h="28" bgcolor="#333" color="#FFF" radius="0" onclick="tap02">{v02}</button>
      <button id="c03" x="59%" y="17%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap03">{v03}</button>
      <button id="c04" x="77%" y="17%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap04">{v04}</button>
      <!-- Row 1: . . О . . -->
      <button id="c10" x="5%" y="29%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap10">{v10}</button>
      <button id="c11" x="23%" y="29%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap11">{v11}</button>
      <button id="c12" x="41%" y="29%" w="17%" h="28" bgcolor="#333" color="#FFF" radius="0" onclick="tap12">{v12}</button>
      <button id="c13" x="59%" y="29%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap13">{v13}</button>
      <button id="c14" x="77%" y="29%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap14">{v14}</button>
      <!-- Row 2: . . Т О К -->
      <button id="c20" x="5%" y="41%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap20">{v20}</button>
      <button id="c21" x="23%" y="41%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap21">{v21}</button>
      <button id="c22" x="41%" y="41%" w="17%" h="28" bgcolor="#333" color="#FFF" radius="0" onclick="tap22">{v22}</button>
      <button id="c23" x="59%" y="41%" w="17%" h="28" bgcolor="#333" color="#FFF" radius="0" onclick="tap23">{v23}</button>
      <button id="c24" x="77%" y="41%" w="17%" h="28" bgcolor="#333" color="#FFF" radius="0" onclick="tap24">{v24}</button>
      <!-- Row 3: . . . . . -->
      <button id="c30" x="5%" y="53%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap30">{v30}</button>
      <button id="c31" x="23%" y="53%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap31">{v31}</button>
      <button id="c32" x="41%" y="53%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap32">{v32}</button>
      <button id="c33" x="59%" y="53%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap33">{v33}</button>
      <button id="c34" x="77%" y="53%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap34">{v34}</button>
      <!-- Row 4: . . . . . -->
      <button id="c40" x="5%" y="65%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap40">{v40}</button>
      <button id="c41" x="23%" y="65%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap41">{v41}</button>
      <button id="c42" x="41%" y="65%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap42">{v42}</button>
      <button id="c43" x="59%" y="65%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap43">{v43}</button>
      <button id="c44" x="77%" y="65%" w="17%" h="28" bgcolor="#111" color="#FFF" radius="0" onclick="tap44">{v44}</button>
      
      <!-- Клавиатура -->
      <button x="2%" y="78%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kA">А</button>
      <button x="14%" y="78%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kE">Е</button>
      <button x="26%" y="78%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kI">И</button>
      <button x="38%" y="78%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kO">О</button>
      <button x="50%" y="78%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kU">У</button>
      <button x="62%" y="78%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kK">К</button>
      <button x="74%" y="78%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kT">Т</button>
      <button x="86%" y="78%" w="12%" h="26" bgcolor="#a0a0a0" color="#000" onclick="kDel">-</button>
      
      <button x="2%" y="88%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kL">Л</button>
      <button x="14%" y="88%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kN">Н</button>
      <button x="26%" y="88%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kR">Р</button>
      <button x="38%" y="88%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kS">С</button>
      <button x="50%" y="88%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kM">М</button>
      <button x="62%" y="88%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kV">В</button>
      <button x="74%" y="88%" w="11%" h="26" bgcolor="#505050" color="#FFF" onclick="kD">Д</button>
      <button x="86%" y="88%" w="12%" h="26" bgcolor="#ff9500" color="#FFF" onclick="nextWord">...</button>
    </page>
    
    <page id="win" bgcolor="#000">
      <label align="center" y="30%" color="#4caf50" font="48">!</label>
      <label align="center" y="50%" color="#FFF" font="32">Победа!</label>
      <label align="center" y="65%" color="#666">Все слова разгаданы</label>
      <button x="20%" y="80%" w="60%" h="50" bgcolor="#ff9500" color="#FFF" onclick="restart">Заново</button>
    </page>
  </ui>
  
  <state>
    <string name="dir" default=">"/>
    <string name="hint" default="Цветок"/>
    <string name="v00" default=""/><string name="v01" default=""/><string name="v02" default=""/><string name="v03" default=""/><string name="v04" default=""/>
    <string name="v10" default=""/><string name="v11" default=""/><string name="v12" default=""/><string name="v13" default=""/><string name="v14" default=""/>
    <string name="v20" default=""/><string name="v21" default=""/><string name="v22" default=""/><string name="v23" default=""/><string name="v24" default=""/>
    <string name="v30" default=""/><string name="v31" default=""/><string name="v32" default=""/><string name="v33" default=""/><string name="v34" default=""/>
    <string name="v40" default=""/><string name="v41" default=""/><string name="v42" default=""/><string name="v43" default=""/><string name="v44" default=""/>
  </state>
  
  <script language="lua">
    -- Простой формат: {направление, подсказка, координаты, буквы}
    local words = {
      {">", "Цветок", {{0,0},{0,1},{0,2}}, {"М","А","К"}},
      {"v", "Питомец", {{0,2},{1,2},{2,2}}, {"К","О","Т"}},
      {">", "Электричество", {{2,2},{2,3},{2,4}}, {"Т","О","К"}},
    }
    
    --[[
      Сетка:
        0 1 2 3 4
      0 М А К . .
      1 . . О . .
      2 . . Т О К
    ]]
    
    -- Цвета
    local BLACK = "#111"
    local CELL = "#333"
    local SELECTED = "#1a237e"
    local PARTIAL = "#1b4332"
    local CORRECT = "#2e7d32"
    
    -- Какие ячейки активны (для initColors)
    local activeCell = {}
    for wi = 1, #words do
      local coords = words[wi][3]
      for ci = 1, #coords do
        local r = coords[ci][1]
        local c = coords[ci][2]
        activeCell[r .. "," .. c] = true
      end
    end
    
    local solved = {}
    for i = 1, #words do solved[i] = false end
    
    local curWord = 1
    local curPos = 1
    
    local function vkey(r,c) return "v"..r..c end
    local function cid(r,c) return "c"..r..c end
    
    -- Сколько угаданных слов содержат эту ячейку?
    local function countSolvedAt(r, c)
      local count = 0
      for wi = 1, #words do
        if solved[wi] then
          local cells = words[wi][3]
          for ci = 1, #cells do
            if cells[ci][1] == r and cells[ci][2] == c then
              count = count + 1
              break
            end
          end
        end
      end
      return count
    end
    
    -- Сколько слов всего содержат эту ячейку?
    local function countWordsAt(r, c)
      local count = 0
      for wi = 1, #words do
        local cells = words[wi][3]
        for ci = 1, #cells do
          if cells[ci][1] == r and cells[ci][2] == c then
            count = count + 1
            break
          end
        end
      end
      return count
    end
    
    local function checkWord(wi)
      local cells = words[wi][3]
      local letters = words[wi][4]
      for ci = 1, #cells do
        local r = cells[ci][1]
        local c = cells[ci][2]
        if state[vkey(r,c)] ~= letters[ci] then
          return false
        end
      end
      return true
    end
    
    local function colorCell(r, c)
      local solvedCount = countSolvedAt(r, c)
      local totalCount = countWordsAt(r, c)
      
      if solvedCount == 0 then
        setAttr(cid(r,c), "bgcolor", CELL)
      elseif solvedCount < totalCount then
        setAttr(cid(r,c), "bgcolor", PARTIAL)  -- пересечение, частично
      else
        setAttr(cid(r,c), "bgcolor", CORRECT)  -- полностью
      end
    end
    
    local function checkAllWords()
      local allSolved = true
      for wi = 1, #words do
        if checkWord(wi) then
          solved[wi] = true
        else
          allSolved = false
        end
      end
      -- Перекрашиваем все ячейки всех слов
      for wi = 1, #words do
        local cells = words[wi][3]
        for ci = 1, #cells do
          local r = cells[ci][1]
          local c = cells[ci][2]
          colorCell(r, c)
        end
      end
      return allSolved
    end
    
    local function isCellSolved(r, c)
      return countSolvedAt(r, c) > 0
    end
    
    local function clearHL()
      for wi = 1, #words do
        if not solved[wi] then
          local cells = words[wi][3]
          for ci = 1, #cells do
            local r = cells[ci][1]
            local c = cells[ci][2]
            if not isCellSolved(r, c) then
              setAttr(cid(r,c), "bgcolor", CELL)
            end
          end
        end
      end
    end
    
    local function highlight()
      clearHL()
      local w = words[curWord]
      state.dir = w[1]
      state.hint = w[2]
      if not solved[curWord] then
        local cells = w[3]
        local r = cells[curPos][1]
        local c = cells[curPos][2]
        setAttr(cid(r,c), "bgcolor", SELECTED)
      end
    end
    
    local function doTap(r, c)
      for wi = 1, #words do
        if not solved[wi] then
          local cells = words[wi][3]
          for ci = 1, #cells do
            if cells[ci][1] == r and cells[ci][2] == c then
              curWord = wi
              curPos = ci
              highlight()
              return
            end
          end
        end
      end
    end
    
    local function addLetter(l)
      local w = words[curWord]
      local cells = w[3]
      if solved[curWord] then return end
      
      if curPos <= #cells then
        local r = cells[curPos][1]
        local c = cells[curPos][2]
        state[vkey(r,c)] = l
        if curPos < #cells then
          curPos = curPos + 1
        end
        if checkAllWords() then
          print("=== ALL SOLVED, navigating to /win ===")
          navigate("/win")
          return
        end
        highlight()
      end
    end
    
    function kDel()
      if solved[curWord] then return end
      
      local w = words[curWord]
      local cells = w[3]
      local r = cells[curPos][1]
      local c = cells[curPos][2]
      if state[vkey(r,c)] == "" and curPos > 1 then
        curPos = curPos - 1
        r = cells[curPos][1]
        c = cells[curPos][2]
      end
      state[vkey(r,c)] = ""
      highlight()
    end
    
    function nextWord()
      local start = curWord
      repeat
        curWord = curWord + 1
        if curWord > #words then
          curWord = 1
        end
        if not solved[curWord] then
          curPos = 1
          highlight()
          return
        end
      until curWord == start
    end
    
    function kA() addLetter("А") end
    function kE() addLetter("Е") end
    function kI() addLetter("И") end
    function kO() addLetter("О") end
    function kU() addLetter("У") end
    function kK() addLetter("К") end
    function kT() addLetter("Т") end
    function kL() addLetter("Л") end
    function kN() addLetter("Н") end
    function kR() addLetter("Р") end
    function kS() addLetter("С") end
    function kM() addLetter("М") end
    function kV() addLetter("В") end
    function kD() addLetter("Д") end
    
    function tap00() doTap(0,0) end function tap01() doTap(0,1) end function tap02() doTap(0,2) end function tap03() doTap(0,3) end function tap04() doTap(0,4) end
    function tap10() doTap(1,0) end function tap11() doTap(1,1) end function tap12() doTap(1,2) end function tap13() doTap(1,3) end function tap14() doTap(1,4) end
    function tap20() doTap(2,0) end function tap21() doTap(2,1) end function tap22() doTap(2,2) end function tap23() doTap(2,3) end function tap24() doTap(2,4) end
    function tap30() doTap(3,0) end function tap31() doTap(3,1) end function tap32() doTap(3,2) end function tap33() doTap(3,3) end function tap34() doTap(3,4) end
    function tap40() doTap(4,0) end function tap41() doTap(4,1) end function tap42() doTap(4,2) end function tap43() doTap(4,3) end function tap44() doTap(4,4) end
    
    function restart()
      for wi = 1, #words do
        solved[wi] = false
        local wcells = words[wi][3]
        for ci = 1, #wcells do
          local r = wcells[ci][1]
          local c = wcells[ci][2]
          state[vkey(r,c)] = ""
        end
      end
      curWord = 1
      curPos = 1
      navigate("/main")
      initColors()
      highlight()
    end
    
    -- Инициализация цветов сетки
    local function initColors()
      for r = 0, 4 do
        for c = 0, 4 do
          if activeCell[r .. "," .. c] then
            setAttr(cid(r,c), "bgcolor", CELL)
          else
            setAttr(cid(r,c), "bgcolor", BLACK)
          end
        end
      end
    end
    
    initColors()
    highlight()
  </script>
</app>

)";

// ═══════════════════════════════════════════════════════
//  Harness
// ═══════════════════════════════════════════════════════

static LuaEngine g_engine;

static void loadApp() {
    LvglMock::reset();
    LvglMock::create_screen(480, 480);
    State::store().clear();
    g_engine.shutdown();

    auto& ui = UI::Engine::instance();
    ui.init();
    TouchSim::init();
    ui.render(CROSSWORD_HTML);

    for (int i = 0; i < ui.stateCount(); i++) {
        const char* name = ui.stateVarName(i);
        const char* def = ui.stateVarDefault(i);
        if (name) State::store().set(name, def ? def : "");
    }

    g_engine.init();
    for (int i = 0; i < ui.stateCount(); i++) {
        const char* name = ui.stateVarName(i);
        const char* def = ui.stateVarDefault(i);
        if (name) g_engine.setState(name, def ? def : "");
    }

    ui.setOnClickHandler([](const char* func) {
        g_engine.call(func);
    });

    const char* code = ui.scriptCode();
    if (code && code[0]) g_engine.execute(code);
}

// ═══════════════════════════════════════════════════════
//  Tests
// ═══════════════════════════════════════════════════════

int main() {
    printf("=== Crossword E2E (Original HTML) ===\n");
    loadApp();

    // ─── 1. Initial state ───────────────────────────────

    SECTION("1. Initial state");

    TEST("Page = main") {
        if (page() == "main") PASS();
        else FAIL_V("page='%s'", page().c_str());
    }

    TEST("dir = >") {
        if (get("dir") == ">") PASS();
        else FAIL_V("dir='%s'", get("dir").c_str());
    }

    TEST("All active cells empty") {
        bool ok = get("v00") == "" && get("v01") == "" && get("v02") == ""
               && get("v12") == "" && get("v22") == "" && get("v23") == "" && get("v24") == "";
        if (ok) PASS(); else FAIL("not empty");
    }

    // ─── 2. МАК ─────────────────────────────────────────

    SECTION("2. МАК (Цветок)");

    TEST("call kM → v00 = М") {
        call("kM");
        if (get("v00") == U_M) PASS();
        else FAIL_V("v00='%s'", get("v00").c_str());
    }

    TEST("call kA → v01 = А") {
        call("kA");
        if (get("v01") == U_A) PASS();
        else FAIL_V("v01='%s'", get("v01").c_str());
    }

    TEST("call kK → v02 = К") {
        call("kK");
        if (get("v02") == U_K) PASS();
        else FAIL_V("v02='%s'", get("v02").c_str());
    }

    TEST("Still on main") {
        if (page() == "main") PASS();
        else FAIL_V("page='%s'", page().c_str());
    }

    // ─── 3. КОТ ─────────────────────────────────────────

    SECTION("3. КОТ (Питомец)");

    TEST("call nextWord → dir = v") {
        call("nextWord");
        if (get("dir") == "v") PASS();
        else FAIL_V("dir='%s'", get("dir").c_str());
    }

    TEST("call kK → v02 stays К (intersection)") {
        call("kK");
        if (get("v02") == U_K) PASS();
        else FAIL_V("v02='%s'", get("v02").c_str());
    }

    TEST("call kO → v12 = О") {
        call("kO");
        if (get("v12") == U_O) PASS();
        else FAIL_V("v12='%s'", get("v12").c_str());
    }

    TEST("call kT → v22 = Т") {
        call("kT");
        if (get("v22") == U_T) PASS();
        else FAIL_V("v22='%s'", get("v22").c_str());
    }

    TEST("Still on main") {
        if (page() == "main") PASS();
        else FAIL_V("page='%s'", page().c_str());
    }

    // ─── 4. ТОК → Victory ──────────────────────────────

    SECTION("4. ТОК (Электричество) → VICTORY");

    TEST("call nextWord → dir = >") {
        call("nextWord");
        if (get("dir") == ">") PASS();
        else FAIL_V("dir='%s'", get("dir").c_str());
    }

    TEST("call kT → v22 stays Т") {
        call("kT");
        if (get("v22") == U_T) PASS();
        else FAIL_V("v22='%s'", get("v22").c_str());
    }

    TEST("call kO → v23 = О") {
        call("kO");
        if (get("v23") == U_O) PASS();
        else FAIL_V("v23='%s'", get("v23").c_str());
    }

    std::string victoryOutput;
    TEST("call kK → v24 = К (ALL SOLVED)") {
        capture_start();
        call("kK");
        victoryOutput = capture_end();
        if (get("v24") == U_K) PASS();
        else FAIL_V("v24='%s'", get("v24").c_str());
    }

    TEST("print('=== ALL SOLVED ...') fired") {
        if (victoryOutput.find("ALL SOLVED") != std::string::npos) PASS();
        else FAIL_V("stdout='%s'", victoryOutput.c_str());
    }

    TEST("VICTORY! page = win") {
        if (page() == "win") PASS();
        else FAIL_V("page='%s'", page().c_str());
    }

    // ─── 5. Final grid ─────────────────────────────────

    SECTION("5. Final grid");

    TEST("Row 0: М А К . .") {
        bool ok = get("v00") == U_M && get("v01") == U_A && get("v02") == U_K
               && get("v03") == "" && get("v04") == "";
        if (ok) PASS(); else FAIL("mismatch");
    }

    TEST("Row 1: . . О . .") {
        bool ok = get("v10") == "" && get("v11") == "" && get("v12") == U_O
               && get("v13") == "" && get("v14") == "";
        if (ok) PASS(); else FAIL("mismatch");
    }

    TEST("Row 2: . . Т О К") {
        bool ok = get("v20") == "" && get("v21") == "" && get("v22") == U_T
               && get("v23") == U_O && get("v24") == U_K;
        if (ok) PASS(); else FAIL("mismatch");
    }

    // ─── 6. Restart ─────────────────────────────────────

    SECTION("6. Restart");

    TEST("call restart → page = main") {
        call("restart");
        if (page() == "main") PASS();
        else FAIL_V("page='%s'", page().c_str());
    }

    TEST("All cells cleared") {
        bool ok = get("v00") == "" && get("v01") == "" && get("v02") == ""
               && get("v12") == "" && get("v22") == "" && get("v23") == "" && get("v24") == "";
        if (ok) PASS(); else FAIL("not cleared");
    }

    TEST("dir = >") {
        if (get("dir") == ">") PASS();
        else FAIL_V("dir='%s'", get("dir").c_str());
    }

    // ─── 7. Delete ──────────────────────────────────────

    SECTION("7. Delete");

    TEST("М then kDel → v00 cleared") {
        call("kM");
        call("kDel");
        if (get("v00") == "") PASS();
        else FAIL_V("v00='%s'", get("v00").c_str());
    }

    TEST("М А kDel → v01 cleared, v00 stays") {
        call("kM");
        call("kA");
        call("kDel");
        if (get("v00") == U_M && get("v01") == "") PASS();
        else FAIL("mismatch");
    }

    TEST("Double kDel → v00 also cleared") {
        call("kDel");
        if (get("v00") == "") PASS();
        else FAIL_V("v00='%s'", get("v00").c_str());
    }

    // ─── 8. Wrong letter ────────────────────────────────

    SECTION("8. Wrong letter + fix");

    call("restart");

    TEST("Т (wrong) → v00 = Т") {
        call("kT");
        if (get("v00") == U_T) PASS(); else FAIL("");
    }

    TEST("kDel + М → v00 = М") {
        call("kDel");
        call("kM");
        if (get("v00") == U_M) PASS(); else FAIL("");
    }

    // ─── 9. Tap grid cells (ui click by ID) ─────────────

    SECTION("9. Tap grid cells (ui click)");

    call("restart");

    TEST("click c12 → word 2, dir = v") {
        click("c12");
        if (get("dir") == "v") PASS();
        else FAIL_V("dir='%s'", get("dir").c_str());
    }

    TEST("call kO → v12 = О") {
        call("kO");
        if (get("v12") == U_O) PASS();
        else FAIL_V("v12='%s'", get("v12").c_str());
    }

    TEST("click c00 → word 1, dir = >") {
        click("c00");
        if (get("dir") == ">") PASS();
        else FAIL_V("dir='%s'", get("dir").c_str());
    }

    // ─── 10. Full replay ────────────────────────────────

    SECTION("10. Full replay → second victory");

    call("restart");

    TEST("МАК") {
        call("kM"); call("kA"); call("kK");
        bool ok = get("v00") == U_M && get("v01") == U_A && get("v02") == U_K;
        if (ok) PASS(); else FAIL("");
    }

    TEST("nextWord → КОТ") {
        call("nextWord");
        call("kK"); call("kO"); call("kT");
        if (get("v22") == U_T) PASS(); else FAIL("");
    }

    TEST("nextWord → ТОК → WIN") {
        call("nextWord");
        call("kT"); call("kO"); call("kK");
        if (page() == "win") PASS();
        else FAIL_V("page='%s'", page().c_str());
    }

    // ═════════════════════════════════════════════════════

    printf("\n────────────────────────────────────────────────────\n");
    if (g_passed == g_total) {
        printf("  ✓ ALL %d CROSSWORD E2E TESTS PASSED\n", g_total);
    } else {
        printf("  ✗ %d/%d PASSED, %d FAILED\n", g_passed, g_total, g_total - g_passed);
    }
    printf("────────────────────────────────────────────────────\n");
    return g_passed == g_total ? 0 : 1;
}

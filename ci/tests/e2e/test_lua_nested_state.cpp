#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "engines/lua/lua_engine.h"
#include "core/state_store.h"
static int g_passed=0,g_total=0;
#define TEST(n) printf("  %-55s ",n);g_total++;
#define PASS() do{printf("✓\n");g_passed++;}while(0)
#define FAIL(m) printf("✗ %s\n",m)
#define FAIL_V(f,...) do{printf("✗ ");printf(f,__VA_ARGS__);printf("\n");}while(0)

static const char*APP=R"(<app os="2.0"><ui default="/m"><page id="m">
<label id="pn">{player.name}</label><label id="ph">{player.health}</label>
<label id="sc">{score}</label><label id="i0">{items[0]}</label><label id="i1">{items[1]}</label>
<label id="dl">{game.world.level}</label>
</page></ui><state>
player:
  name: "Hero"
  health: 100
  x: 10
  y: 20
score: 0
items:
  - "sword"
  - "shield"
  - "potion"
game:
  world:
    level: 3
</state><script language="lua">
function getPlayerName() return state.player.name end
function getPlayerX() return state.player.x end
function setPlayerX(v) state.player.x = v end
function getScore() return state.score end
function setScore(v) state.score = v end
function getItem1() return state.items[1] end
function getItem2() return state.items[2] end
function setItem1(v) state.items[1] = v end
function getItemCount() return #state.items end
function getDungeonLevel() return state.game.world.level end
function setHealth(v) state.player.health = v end
function complexOp()
  state.player.health = state.player.health - 10
  state.score = state.score + 5
  state.items[1] = "magic sword"
end
</script></app>)";

static LuaEngine*g_eng=nullptr;
static std::string g_cbKey,g_cbVal;
struct App{
    LuaEngine engine;
    bool load(){
        LvglMock::reset();LvglMock::create_screen(480,480);
        g_core.store().clear();engine.shutdown();
        g_cbKey.clear();g_cbVal.clear();
        g_core.initDynamicApp(nullptr);g_core.render(APP);
        for(int i=0;i<g_core.stateCount();i++){
            const char*n=g_core.stateVarName(i);
            const char*d=g_core.stateVarDefault(i);
            if(n)engine.setState(n,d?d:"");
        }
        engine.init();
        for(int i=0;i<g_core.stateCount();i++){
            const char*n=g_core.stateVarName(i);
            const char*d=g_core.stateVarDefault(i);
            if(n)engine.setState(n,d?d:"");
        }
        g_eng=&engine;
        g_core.setOnClickHandler([](const char*f){if(g_eng)g_eng->call(f);});
        engine.onStateChange([](const char*k,const char*v){g_cbKey=k;g_cbVal=v;});
        const char*code=g_core.scriptCode();
        if(code&&code[0])return engine.execute(code);
        return true;
    }
    std::string eval(const char*expr){
        lua_State*L=engine.getLuaState();
        std::string c=std::string("_result=tostring(")+expr+")";
        engine.execute(c.c_str());
        lua_getglobal(L,"_result");
        const char*r=lua_tostring(L,-1);
        std::string res=r?r:"";lua_pop(L,1);return res;
    }
    void exec(const char*c){engine.execute(c);}
};

int main(){
    printf("=== LUA NESTED STATE E2E ===\n\n");
    App app;if(!app.load()){printf("✗ load fail\n");return 1;}

    printf("Read nested:\n");
    TEST("player.name='Hero'"){auto r=app.eval("getPlayerName()");if(r=="Hero")PASS();else FAIL(r.c_str());}
    TEST("player.x=10"){auto r=app.eval("getPlayerX()");if(r=="10")PASS();else FAIL(r.c_str());}
    TEST("score=0"){auto r=app.eval("getScore()");if(r=="0")PASS();else FAIL(r.c_str());}
    TEST("game.world.level=3"){auto r=app.eval("getDungeonLevel()");if(r=="3")PASS();else FAIL(r.c_str());}

    printf("\nRead arrays (Lua 1-indexed):\n");
    TEST("items[1]='sword'"){auto r=app.eval("getItem1()");if(r=="sword")PASS();else FAIL(r.c_str());}
    TEST("items[2]='shield'"){auto r=app.eval("getItem2()");if(r=="shield")PASS();else FAIL(r.c_str());}
    TEST("#items=3"){auto r=app.eval("getItemCount()");if(r=="3")PASS();else FAIL(r.c_str());}

    printf("\nWrite nested:\n");
    TEST("player.x=50 store"){app.exec("setPlayerX(50)");if(g_core.store().getInt("player.x")==50)PASS();else FAIL("wrong");}
    TEST("player.x=50 reread"){auto r=app.eval("getPlayerX()");if(r=="50")PASS();else FAIL(r.c_str());}
    TEST("score=99"){app.exec("setScore(99)");if(g_core.store().getInt("score")==99)PASS();else FAIL("wrong");}
    TEST("health=75 callback"){g_cbKey.clear();app.exec("setHealth(75)");if(g_cbKey=="player.health"&&g_cbVal=="75")PASS();else FAIL_V("k='%s' v='%s'",g_cbKey.c_str(),g_cbVal.c_str());}

    printf("\nWrite arrays:\n");
    TEST("items[1]='magic sword'"){app.exec("setItem1('magic sword')");if(g_core.store().getArrayItem("items",0)=="magic sword")PASS();else FAIL(g_core.store().getArrayItem("items",0).c_str());}
    TEST("reread"){auto r=app.eval("getItem1()");if(r=="magic sword")PASS();else FAIL(r.c_str());}
    TEST("others unchanged"){if(g_core.store().getArrayItem("items",1)=="shield")PASS();else FAIL("wrong");}

    printf("\nComplex:\n");app.load();
    TEST("complexOp"){app.exec("complexOp()");
        bool ok=g_core.store().getInt("player.health")==90&&g_core.store().getInt("score")==5&&g_core.store().getArrayItem("items",0)=="magic sword";
        if(ok)PASS();else FAIL("wrong");}

    printf("\n");
    if(g_passed==g_total){printf("=== ALL %d LUA NESTED STATE TESTS PASSED ===\n",g_total);return 0;}
    else{printf("=== %d/%d LUA NESTED STATE TESTS PASSED ===\n",g_passed,g_total);return 1;}
}

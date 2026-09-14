#include <cstdio>
#include <cstring>
#include <string>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "ui/ui_html_internal.h"
#include "engines/lua/lua_engine.h"
#include "core/state_store.h"
using MW=LvglMock::Widget;
static int g_passed=0,g_total=0;
#define TEST(n) printf("  %-55s ",n);g_total++;
#define PASS() do{printf("✓\n");g_passed++;}while(0)
#define FAIL(m) printf("✗ %s\n",m)
#define FAIL_V(f,...) do{printf("✗ ");printf(f,__VA_ARGS__);printf("\n");}while(0)

static const char*APP=R"(<app os="2.0"><ui default="/m"><page id="m">
<label id="nl">{player.name}</label>
<label id="hl">HP: {player.health}</label>
<label id="sl">Score: {score}</label>
<label id="il">{items[0]}</label>
<label id="ml">{msg}</label>
</page></ui><state>
player:
  name: "Hero"
  health: 100
score: 0
items:
  - "sword"
  - "shield"
msg: "Welcome"
</state><script language="lua">
function takeDamage() state.player.health = state.player.health - 25 end
function addScore() state.score = state.score + 10 end
function upgradeWeapon() state.items[1] = "magic sword" end
function setMsg(t) state.msg = t end
function complexAction() state.player.health=50;state.score=99;state.msg="Boss defeated!" end
</script></app>)";

static LuaEngine*g_eng=nullptr;
struct App{
    LuaEngine engine;
    bool load(){
        LvglMock::reset();LvglMock::create_screen(480,480);
        g_core.store().clear();engine.shutdown();
        g_core.initDynamicApp(nullptr);g_core.render(APP);
        for(int i=0;i<g_core.stateCount();i++){
            const char*n=g_core.stateVarName(i);const char*d=g_core.stateVarDefault(i);
            if(n)engine.setState(n,d?d:"");
        }
        engine.init();
        for(int i=0;i<g_core.stateCount();i++){
            const char*n=g_core.stateVarName(i);const char*d=g_core.stateVarDefault(i);
            if(n)engine.setState(n,d?d:"");
        }
        g_eng=&engine;
        g_core.setOnClickHandler([](const char*f){if(g_eng)g_eng->call(f);});
        engine.onStateChange([](const char*k,const char*v){ui_update_bindings(k,v);});
        const char*code=g_core.scriptCode();
        return (code&&code[0])?engine.execute(code):true;
    }
    void exec(const char*c){engine.execute(c);}
    std::string lbl(const char*id){
        auto*p=LvglMock::g_screen->first("Container");
        if(!p)return "";auto*w=p->findById(id);return w?w->text:"";
    }
};

int main(){
    printf("=== BINDING UPDATE TESTS ===\n\n");
    App app;if(!app.load()){printf("✗ load\n");return 1;}

    printf("Initial:\n");
    TEST("nl='Hero'"){if(app.lbl("nl")=="Hero")PASS();else FAIL(app.lbl("nl").c_str());}
    TEST("hl='HP: 100'"){if(app.lbl("hl")=="HP: 100")PASS();else FAIL(app.lbl("hl").c_str());}
    TEST("sl='Score: 0'"){if(app.lbl("sl")=="Score: 0")PASS();else FAIL(app.lbl("sl").c_str());}
    TEST("il='sword'"){if(app.lbl("il")=="sword")PASS();else FAIL(app.lbl("il").c_str());}
    TEST("ml='Welcome'"){if(app.lbl("ml")=="Welcome")PASS();else FAIL(app.lbl("ml").c_str());}

    printf("\nNested write->UI:\n");
    TEST("takeDamage->HP:75"){app.exec("takeDamage()");if(app.lbl("hl")=="HP: 75")PASS();else FAIL(app.lbl("hl").c_str());}
    TEST("takeDamage->HP:50"){app.exec("takeDamage()");if(app.lbl("hl")=="HP: 50")PASS();else FAIL(app.lbl("hl").c_str());}
    TEST("addScore->Score:10"){app.exec("addScore()");if(app.lbl("sl")=="Score: 10")PASS();else FAIL(app.lbl("sl").c_str());}
    TEST("name unchanged"){if(app.lbl("nl")=="Hero")PASS();else FAIL(app.lbl("nl").c_str());}

    printf("\nFlat write->UI:\n");
    TEST("setMsg->updated"){app.exec("setMsg('Game Over')");if(app.lbl("ml")=="Game Over")PASS();else FAIL(app.lbl("ml").c_str());}

    printf("\nArray write->UI:\n");
    TEST("upgradeWeapon->magic sword"){app.exec("upgradeWeapon()");if(app.lbl("il")=="magic sword")PASS();else FAIL(app.lbl("il").c_str());}

    printf("\nMulti-write:\n");app.load();
    TEST("complexAction all update"){app.exec("complexAction()");
        auto h=app.lbl("hl"),s=app.lbl("sl"),m=app.lbl("ml");
        if(h=="HP: 50"&&s=="Score: 99"&&m=="Boss defeated!")PASS();
        else FAIL_V("h='%s' s='%s' m='%s'",h.c_str(),s.c_str(),m.c_str());}

    printf("\n");
    if(g_passed==g_total){printf("=== ALL %d BINDING UPDATE TESTS PASSED ===\n",g_total);return 0;}
    else{printf("=== %d/%d BINDING UPDATE TESTS PASSED ===\n",g_passed,g_total);return 1;}
}

#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"
using MW=LvglMock::Widget;
#define TEST(n) printf("  %-55s ",n);total++;
#define PASS() do{printf("✓\n");passed++;}while(0)
#define FAIL(m) do{printf("✗ %s\n",m);}while(0)
static void render(const char*h){LvglMock::reset();LvglMock::create_screen(480,480);g_core.store().clear();g_core.initDynamicApp(nullptr);g_core.render(h);}
static MW*pg(){return LvglMock::g_screen->first("Container");}

static const char*HN=R"(<app os="2.0"><ui default="/m"><page id="m">
<label id="pn">{player.name}</label><label id="ph">{player.health}</label>
<label id="bg">{theme.bg}</label><label id="sc">{score}</label>
</page></ui><state>
player:
  name: "Hero"
  health: 100
  x: 10
theme:
  bg: "#1a1a2e"
  accent: "#e74c3c"
score: 0
</state><script language="lua"></script></app>)";

static const char*HA=R"(<app os="2.0"><ui default="/m"><page id="m">
<label id="i0">{items[0]}</label><label id="i1">{items[1]}</label><label id="i2">{items[2]}</label>
</page></ui><state>
items:
  - "sword"
  - "shield"
  - "potion"
</state><script language="lua"></script></app>)";

static const char*HD=R"(<app os="2.0"><ui default="/m"><page id="m">
<label id="dc">{game.world.dungeon.color}</label><label id="dl">{game.world.dungeon.level}</label>
</page></ui><state>
game:
  world:
    dungeon:
      color: "#333"
      level: 5
</state><script language="lua"></script></app>)";

static const char*HM=R"(<app os="2.0"><ui default="/m"><page id="m">
<label id="fl">{title}</label><label id="nl">{player.name}</label><label id="al">{weapons[0]}</label>
</page></ui><state>
title: "RPG"
player:
  name: "Warrior"
  hp: 80
weapons:
  - "axe"
  - "bow"
</state><script language="lua"></script></app>)";

static const char*HU=R"(<app os="2.0"><ui default="/m"><page id="m">
<label id="c0">{colors[0]}</label><label id="c1">{colors[1]}</label>
</page></ui><state>
colors:
  - red
  - blue
</state><script language="lua"></script></app>)";

int main(){
    printf("=== NESTED STATE TESTS ===\n\n");int passed=0,total=0;
    printf("Dotted paths:\n");render(HN);auto*p=pg();auto&s=g_core.store();
    TEST("player.name='Hero'"){if(s.has("player.name")&&s.getString("player.name")=="Hero")PASS();else FAIL("wrong");}
    TEST("player.health=100"){if(s.getInt("player.health")==100)PASS();else FAIL("wrong");}
    TEST("player.x=10"){if(s.getInt("player.x")==10)PASS();else FAIL("wrong");}
    TEST("theme.bg='#1a1a2e'"){if(s.getString("theme.bg")=="#1a1a2e")PASS();else FAIL("wrong");}
    TEST("score=0 flat"){if(s.has("score")&&s.getInt("score")==0)PASS();else FAIL("wrong");}
    TEST("hasPrefix('player')"){if(s.hasPrefix("player"))PASS();else FAIL("false");}
    TEST("hasPrefix('theme')"){if(s.hasPrefix("theme"))PASS();else FAIL("false");}
    TEST("!hasPrefix('score')"){if(!s.hasPrefix("score"))PASS();else FAIL("true");}
    TEST("{player.name}->'Hero'"){auto*w=p->findById("pn");if(w&&w->text=="Hero")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("{player.health}->'100'"){auto*w=p->findById("ph");if(w&&w->text=="100")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("{theme.bg}"){auto*w=p->findById("bg");if(w&&w->text=="#1a1a2e")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("{score}->'0'"){auto*w=p->findById("sc");if(w&&w->text=="0")PASS();else FAIL(w?w->text.c_str():"no");}

    printf("\nArrays:\n");render(HA);p=pg();
    TEST("hasArray('items')"){if(g_core.store().hasArray("items"))PASS();else FAIL("false");}
    TEST("!has('items.0')"){if(!g_core.store().has("items.0"))PASS();else FAIL("items.0 exists");}
    TEST("getArraySize=3"){if(g_core.store().getArraySize("items")==3)PASS();else FAIL("wrong");}
    TEST("item[0]='sword'"){if(g_core.store().getArrayItem("items",0)=="sword")PASS();else FAIL("wrong");}
    TEST("item[1]='shield'"){if(g_core.store().getArrayItem("items",1)=="shield")PASS();else FAIL("wrong");}
    TEST("item[2]='potion'"){if(g_core.store().getArrayItem("items",2)=="potion")PASS();else FAIL("wrong");}
    TEST("OOB=''"){if(g_core.store().getArrayItem("items",99)=="")PASS();else FAIL("wrong");}
    TEST("{items[0]}->'sword'"){auto*w=p->findById("i0");if(w&&w->text=="sword")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("{items[1]}->'shield'"){auto*w=p->findById("i1");if(w&&w->text=="shield")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("{items[2]}->'potion'"){auto*w=p->findById("i2");if(w&&w->text=="potion")PASS();else FAIL(w?w->text.c_str():"no");}

    printf("\nUnquoted arrays:\n");render(HU);p=pg();
    TEST("colors[0]='red'"){auto*w=p->findById("c0");if(w&&w->text=="red")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("colors[1]='blue'"){auto*w=p->findById("c1");if(w&&w->text=="blue")PASS();else FAIL(w?w->text.c_str():"no");}

    printf("\nDeep nesting:\n");render(HD);p=pg();
    TEST("game.world.dungeon.color='#333'"){if(g_core.store().getString("game.world.dungeon.color")=="#333")PASS();else FAIL("wrong");}
    TEST("game.world.dungeon.level=5"){if(g_core.store().getInt("game.world.dungeon.level")==5)PASS();else FAIL("wrong");}
    TEST("{...color}->'#333'"){auto*w=p->findById("dc");if(w&&w->text=="#333")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("{...level}->'5'"){auto*w=p->findById("dl");if(w&&w->text=="5")PASS();else FAIL(w?w->text.c_str():"no");}

    printf("\nMixed flat+nested+array:\n");render(HM);p=pg();
    TEST("title='RPG'"){auto*w=p->findById("fl");if(w&&w->text=="RPG")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("{player.name}='Warrior'"){auto*w=p->findById("nl");if(w&&w->text=="Warrior")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("{weapons[0]}='axe'"){auto*w=p->findById("al");if(w&&w->text=="axe")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("weapons size=2"){if(g_core.store().getArraySize("weapons")==2)PASS();else FAIL("wrong");}
    TEST("setArrayItem"){g_core.store().setArrayItem("weapons",0,"magic axe",false);if(g_core.store().getArrayItem("weapons",0)=="magic axe")PASS();else FAIL("wrong");}
    TEST("idx 1 unchanged"){if(g_core.store().getArrayItem("weapons",1)=="bow")PASS();else FAIL("wrong");}

    printf("\n");
    if(passed==total){printf("=== ALL %d NESTED STATE TESTS PASSED ===\n",total);return 0;}
    else{printf("=== %d/%d NESTED STATE TESTS PASSED ===\n",passed,total);return 1;}
}

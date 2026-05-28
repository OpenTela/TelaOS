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

static const char*H_FOR_TPL=R"(<app os="2.0"><templates><NumBtn>
<button id="btn_{n}" onclick="press_{n}">{n}</button></NumBtn></templates>
<ui default="/m"><page id="m">@for(i in 1..4){<NumBtn n="{i}"/>}</page></ui>
<state>x: 0</state><script language="lua"></script></app>)";

static const char*H_TPL_FLEX=R"(<app os="2.0"><templates><Card>
<div id="{id}" w="90%" h="80" bgcolor="{bg}" radius="12" flex="row" gap="8" align="center">
<label color="#fff" font="24">{icon}</label><label color="#fff" grow="1">{title}</label>
</div></Card></templates>
<ui default="/m"><page id="m"><div flex="column" gap="8" w="100%" h="100%">
<Card id="c_w" bg="#2c3e50" icon="S" title="Weather"/>
<Card id="c_m" bg="#8e44ad" icon="M" title="Music"/>
</div></page></ui><state>x: 0</state><script language="lua"></script></app>)";

static const char*H_GRID=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="grid" w="100%" flex="row-wrap" gap="4">
@for(i in 0..2){<div id="cell_{i}" w="48%" h="60" bgcolor="#333" radius="8"><label id="lbl_{i}" color="#fff">{labels[{i}]}</label></div>}
</div><label id="tl">{config.title}</label>
</page></ui><state>
config:
  title: "Dashboard"
labels:
  - "CPU"
  - "RAM"
  - "Disk"
</state><script language="lua"></script></app>)";

static const char*H_RPG=R"(<app os="2.0"><templates>
<StatLine><label id="{id}" x="5%" y="{y}%" color="#aaa" font="16">{label}: {val}</label></StatLine>
<ActionBtn><button id="{id}" x="{x}%" y="{y}%" w="42%" h="40" bgcolor="{bg}" onclick="{click}">{label}</button></ActionBtn>
</templates><ui default="/m"><page id="m" bgcolor="#1a1a2e">
<label id="hero" align="center" y="5%" color="#fff" font="32">{player.name}</label>
<StatLine id="hp" label="HP" val="{player.health}" y="20"/>
<StatLine id="atk" label="ATK" val="{player.attack}" y="26"/>
<StatLine id="gold" label="Gold" val="{player.gold}" y="32"/>
<StatLine id="wpn" label="Weapon" val="{items[0]}" y="38"/>
<label id="msg" x="5%" y="55%" color="#888">{msg}</label>
<ActionBtn id="fight" x="5" y="75" bg="#e74c3c" click="fight" label="Fight"/>
<ActionBtn id="heal" x="52" y="75" bg="#27ae60" click="heal" label="Heal"/>
</page></ui><state>
player:
  name: "Hero"
  health: 100
  attack: 15
  gold: 0
items:
  - "Rusty Sword"
  - "Wooden Shield"
msg: "A wild monster appears!"
</state><script language="lua"></script></app>)";

int main(){
    printf("=== V2 COMBINED TESTS ===\n\n");int passed=0,total=0;

    printf("@for + template v2:\n");render(H_FOR_TPL);auto*p=pg();
    TEST("btn_1"){if(p->findById("btn_1"))PASS();else FAIL("no");}
    TEST("btn_4"){if(p->findById("btn_4"))PASS();else FAIL("no");}
    TEST("4 buttons"){if(p->count("Button",true)==4)PASS();else FAIL("wrong");}

    printf("\nTemplate v2 + div/flex:\n");render(H_TPL_FLEX);p=pg();
    TEST("c_w exists"){if(p->findById("c_w"))PASS();else FAIL("no");}
    TEST("c_m exists"){if(p->findById("c_m"))PASS();else FAIL("no");}
    TEST("c_w flex=row"){auto*d=p->findById("c_w");if(d&&d->flexFlow==0)PASS();else FAIL("wrong");}
    TEST("c_w radius=12"){auto*d=p->findById("c_w");if(d&&d->radius==12)PASS();else FAIL("wrong");}
    TEST("Weather in c_w"){auto*d=p->findById("c_w");if(d&&d->findByText("Weather"))PASS();else FAIL("no");}

    printf("\n@for + flex + nested state:\n");render(H_GRID);p=pg();
    TEST("grid row-wrap"){auto*d=p->findById("grid");if(d&&d->flexFlow==2)PASS();else FAIL("wrong");}
    TEST("cell_0"){if(p->findById("cell_0"))PASS();else FAIL("no");}
    TEST("cell_2"){if(p->findById("cell_2"))PASS();else FAIL("no");}
    TEST("lbl_0='CPU'"){auto*w=p->findById("lbl_0");if(w&&w->text=="CPU")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("lbl_1='RAM'"){auto*w=p->findById("lbl_1");if(w&&w->text=="RAM")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("lbl_2='Disk'"){auto*w=p->findById("lbl_2");if(w&&w->text=="Disk")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("config.title='Dashboard'"){auto*w=p->findById("tl");if(w&&w->text=="Dashboard")PASS();else FAIL(w?w->text.c_str():"no");}

    printf("\nFull RPG:\n");render(H_RPG);p=pg();
    TEST("hero='Hero'"){auto*w=p->findById("hero");if(w&&w->text=="Hero")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("hp='HP: 100'"){auto*w=p->findById("hp");if(w&&w->text=="HP: 100")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("atk='ATK: 15'"){auto*w=p->findById("atk");if(w&&w->text=="ATK: 15")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("gold='Gold: 0'"){auto*w=p->findById("gold");if(w&&w->text=="Gold: 0")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("wpn='Weapon: Rusty Sword'"){auto*w=p->findById("wpn");if(w&&w->text=="Weapon: Rusty Sword")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("msg"){auto*w=p->findById("msg");if(w&&w->text=="A wild monster appears!")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("fight btn"){if(p->findById("fight"))PASS();else FAIL("no");}
    TEST("heal btn"){if(p->findById("heal"))PASS();else FAIL("no");}

    printf("\n");
    if(passed==total){printf("=== ALL %d V2 COMBINED TESTS PASSED ===\n",total);return 0;}
    else{printf("=== %d/%d V2 COMBINED TESTS PASSED ===\n",passed,total);return 1;}
}

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

static const char*HV2=R"(<app os="2.0"><templates><StatLine><label id="{id}" color="#aaa">{label}: {val}</label></StatLine></templates>
<ui default="/m"><page id="m"><StatLine id="hp" label="HP" val="100"/><StatLine id="atk" label="ATK" val="15"/></page></ui>
<state>x: 0</state><script language="lua"></script></app>)";

static const char*HMIX=R"(<app os="2.0"><templates>
<Card><div id="{id}" w="90%" h="80" bgcolor="{bg}" radius="8"><label color="#fff">{title}</label></div></Card>
</templates><ui default="/m"><page id="m">
<Card id="c1" bg="#333" title="Weather"/><Card id="c2" bg="#444" title="Music"/>
</page></ui><state>x: 0</state><script language="lua"></script></app>)";

int main(){
    printf("=== TEMPLATE V2 TESTS ===\n\n");int passed=0,total=0;
    printf("PascalCase:\n");render(HV2);auto*p=pg();
    TEST("hp exists"){if(p->findById("hp"))PASS();else FAIL("no");}
    TEST("hp='HP: 100'"){auto*w=p->findById("hp");if(w&&w->text=="HP: 100")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("atk exists"){if(p->findById("atk"))PASS();else FAIL("no");}
    TEST("atk='ATK: 15'"){auto*w=p->findById("atk");if(w&&w->text=="ATK: 15")PASS();else FAIL(w?w->text.c_str():"no");}

    printf("\nWith div:\n");render(HMIX);p=pg();
    TEST("c1 exists"){if(p->findById("c1"))PASS();else FAIL("no");}
    TEST("c2 exists"){if(p->findById("c2"))PASS();else FAIL("no");}
    TEST("Weather in c1"){auto*d=p->findById("c1");if(d&&d->findByText("Weather"))PASS();else FAIL("no");}
    TEST("Music in c2"){auto*d=p->findById("c2");if(d&&d->findByText("Music"))PASS();else FAIL("no");}

    printf("\n");
    if(passed==total){printf("=== ALL %d TEMPLATE V2 TESTS PASSED ===\n",total);return 0;}
    else{printf("=== %d/%d TEMPLATE V2 TESTS PASSED ===\n",passed,total);return 1;}
}

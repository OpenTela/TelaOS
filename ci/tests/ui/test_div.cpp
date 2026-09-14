#include <cstdio>
#include <cstring>
#include "lvgl.h"
#include "lvgl_mock.h"
#include "core/core.h"
#include "core/state_store.h"
using MW = LvglMock::Widget;
#define TEST(n) printf("  %-55s ",n); total++;
#define PASS() do{printf("✓\n");passed++;}while(0)
#define FAIL(m) do{printf("✗ %s\n",m);}while(0)
static void render(const char*h){LvglMock::reset();LvglMock::create_screen(480,480);g_core.store().clear();g_core.initDynamicApp(nullptr);g_core.render(h);}
static MW*pg(){return LvglMock::g_screen->first("Container");}

static const char*H1=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="card" x="5%" y="10%" w="90%" h="120" bgcolor="#1a1a2e" radius="12">
  <label id="t" color="#fff">Weather</label><button id="b" onclick="go">Act</button>
</div></page></ui><state>x: 0</state><script language="lua"></script></app>)";

static const char*H2=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="out" x="0" y="0" w="100%" h="100%" bgcolor="#111">
  <div id="in" x="10%" y="10%" w="80%" h="30%" bgcolor="#333" radius="8">
    <label id="lbl" color="#fff">Card</label>
  </div>
</div></page></ui><state>x: 0</state><script language="lua"></script></app>)";

static const char*H3=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="level1" w="100%" h="100%" bgcolor="#111">
  <div id="level2" w="80%" h="80%" bgcolor="#222">
    <div id="level3" w="60%" h="60%" bgcolor="#333">
      <label id="deep" color="#fff">Deep</label>
    </div></div></div>
</page></ui><state>x: 0</state><script language="lua"></script></app>)";

static const char*H4=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="padded" w="100%" h="60" padding="12"><label id="p1">Pad</label></div>
<div id="scroll_div" w="100%" h="200" overflow="scroll"><label>S</label></div>
<div id="hidden_div" w="100%" h="200"><label>N</label></div>
</page></ui><state>x: 0</state><script language="lua"></script></app>)";

int main(){
    printf("=== DIV TESTS ===\n\n");int passed=0,total=0;
    printf("Basic:\n"); render(H1); auto*p=pg();
    TEST("card exists"){if(p->findById("card"))PASS();else FAIL("no");}
    TEST("label inside div"){auto*d=p->findById("card");if(d&&d->findById("t"))PASS();else FAIL("no");}
    TEST("button inside div"){auto*d=p->findById("card");if(d&&d->findById("b"))PASS();else FAIL("no");}
    TEST("x=5%=24"){auto*d=p->findById("card");if(d&&d->x==24)PASS();else FAIL("wrong");}
    TEST("y=10%=48"){auto*d=p->findById("card");if(d&&d->y==48)PASS();else FAIL("wrong");}
    TEST("bgcolor=#1a1a2e"){auto*d=p->findById("card");if(d&&d->hasBgcolor&&d->bgcolor==0x1a1a2e)PASS();else FAIL("wrong");}
    TEST("radius=12"){auto*d=p->findById("card");if(d&&d->radius==12)PASS();else FAIL("wrong");}

    printf("\nNested:\n"); render(H2); p=pg();
    TEST("outer exists"){if(p->findById("out"))PASS();else FAIL("no");}
    TEST("inner inside outer"){auto*o=p->findById("out");if(o&&o->findById("in"))PASS();else FAIL("no");}
    TEST("label in inner"){auto*i=p->findById("in");if(i&&i->findById("lbl"))PASS();else FAIL("no");}
    TEST("label NOT direct child of outer"){auto*o=p->findById("out");bool d=false;if(o)for(auto*c:o->children)if(c->id=="lbl"){d=true;break;}if(!d)PASS();else FAIL("broken");}

    printf("\nDeep (3):\n"); render(H3); p=pg();
    TEST("level1"){if(p->findById("level1"))PASS();else FAIL("no");}
    TEST("level2 in level1"){auto*l1=p->findById("level1");if(l1&&l1->findById("level2"))PASS();else FAIL("no");}
    TEST("level3 in level2"){auto*l2=p->findById("level2");if(l2&&l2->findById("level3"))PASS();else FAIL("no");}
    TEST("deep label in level3"){auto*l3=p->findById("level3");if(l3&&l3->findById("deep"))PASS();else FAIL("no");}
    TEST("deep text='Deep'"){auto*w=p->findById("deep");if(w&&w->text=="Deep")PASS();else FAIL("wrong");}

    printf("\nPadding/overflow:\n"); render(H4); p=pg();
    TEST("padding=12"){auto*d=p->findById("padded");if(d&&d->padAll==12)PASS();else FAIL("wrong");}
    TEST("scroll_div scrollable"){auto*d=p->findById("scroll_div");if(d&&d->scrollable)PASS();else FAIL("no");}
    TEST("hidden_div not scrollable"){auto*d=p->findById("hidden_div");if(d&&!d->scrollable)PASS();else FAIL("yes");}

    printf("\n");
    if(passed==total){printf("=== ALL %d DIV TESTS PASSED ===\n",total);return 0;}
    else{printf("=== %d/%d DIV TESTS PASSED ===\n",passed,total);return 1;}
}

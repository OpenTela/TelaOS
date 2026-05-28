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

static const char*H_ROW=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="row" w="100%" h="50" flex="row"><button id="a" w="80" h="40">A</button><button id="b" w="80" h="40">B</button><button id="c" w="80" h="40">C</button></div>
</page></ui><state>x: 0</state><script language="lua"></script></app>)";

static const char*H_COL=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="col" w="100%" h="200" flex="column" gap="4"><label id="i1" h="30">1</label><label id="i2" h="30">2</label><label id="i3" h="30">3</label></div>
</page></ui><state>x: 0</state><script language="lua"></script></app>)";

static const char*H_FLOWS=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="rw" flex="row-wrap" w="100%" h="100"><label>x</label></div>
<div id="cw" flex="column-wrap" w="100%" h="100"><label>x</label></div>
<div id="rr" flex="row-reverse" w="100%" h="100"><label>x</label></div>
<div id="cr" flex="column-reverse" w="100%" h="100"><label>x</label></div>
</page></ui><state>x: 0</state><script language="lua"></script></app>)";

static const char*H_JUST=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="jc" flex="row" justify="center" w="100%" h="50"><button w="40" h="30">x</button></div>
<div id="je" flex="row" justify="end" w="100%" h="50"><button w="40" h="30">x</button></div>
<div id="jb" flex="row" justify="between" w="100%" h="50"><button w="40" h="30">x</button></div>
<div id="ja" flex="row" justify="around" w="100%" h="50"><button w="40" h="30">x</button></div>
<div id="jv" flex="row" justify="evenly" w="100%" h="50"><button w="40" h="30">x</button></div>
</page></ui><state>x: 0</state><script language="lua"></script></app>)";

static const char*H_ALIGN=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="ac" flex="row" align="center" w="100%" h="50"><button w="40" h="30">x</button></div>
<div id="ae" flex="row" align="end" w="100%" h="50"><button w="40" h="30">x</button></div>
<div id="as" flex="row" align="stretch" w="100%" h="50"><button w="40" h="30">x</button></div>
</page></ui><state>x: 0</state><script language="lua"></script></app>)";

static const char*H_GROW=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="sl" w="100%" h="100%" flex="row">
  <div id="side" w="60" h="100%" bgcolor="#111"/>
  <div id="content" grow="1" h="100%" bgcolor="#000"><label id="mt" color="#fff">C</label></div>
</div></page></ui><state>x: 0</state><script language="lua"></script></app>)";

static const char*H_GROWW=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="bar" w="100%" h="50" flex="row" gap="4">
  <button id="left" w="60" h="40">L</button>
  <label id="spacer" grow="1" h="40">S</label>
  <button id="right" w="60" h="40">R</button>
</div></page></ui><state>x: 0</state><script language="lua"></script></app>)";

static const char*H_FOR=R"(<app os="2.0"><ui default="/m"><page id="m">
<div id="grid" w="90%" flex="row-wrap" gap="8">
  @for(i in 0..3) {<div id="item_{i}" w="45%" h="60" bgcolor="#333" radius="8"><label id="lbl_{i}" color="#fff">Item {i}</label></div>}
</div></page></ui><state>x: 0</state><script language="lua"></script></app>)";

int main(){
    printf("=== FLEX TESTS ===\n\n");int passed=0,total=0;

    printf("flex=row:\n");render(H_ROW);auto*p=pg();
    TEST("row flow=ROW(0)"){auto*d=p->findById("row");if(d&&d->flexFlow==LV_FLEX_FLOW_ROW)PASS();else FAIL("wrong");}
    TEST("3 buttons"){auto*d=p->findById("row");if(d&&d->count("Button",true)==3)PASS();else FAIL("wrong");}

    printf("\nflex=column:\n");render(H_COL);p=pg();
    TEST("col flow=COLUMN(1)"){auto*d=p->findById("col");if(d&&d->flexFlow==LV_FLEX_FLOW_COLUMN)PASS();else FAIL("wrong");}
    TEST("gap=4 padRow"){auto*d=p->findById("col");if(d&&d->padRow==4)PASS();else FAIL("wrong");}
    TEST("gap=4 padCol"){auto*d=p->findById("col");if(d&&d->padColumn==4)PASS();else FAIL("wrong");}

    printf("\nAll flows:\n");render(H_FLOWS);p=pg();
    TEST("row-wrap=2"){auto*d=p->findById("rw");if(d&&d->flexFlow==LV_FLEX_FLOW_ROW_WRAP)PASS();else FAIL("wrong");}
    TEST("column-wrap=3"){auto*d=p->findById("cw");if(d&&d->flexFlow==LV_FLEX_FLOW_COLUMN_WRAP)PASS();else FAIL("wrong");}
    TEST("row-reverse=4"){auto*d=p->findById("rr");if(d&&d->flexFlow==LV_FLEX_FLOW_ROW_REVERSE)PASS();else FAIL("wrong");}
    TEST("column-reverse=5"){auto*d=p->findById("cr");if(d&&d->flexFlow==LV_FLEX_FLOW_COLUMN_REVERSE)PASS();else FAIL("wrong");}

    printf("\nJustify:\n");render(H_JUST);p=pg();
    TEST("center"){auto*d=p->findById("jc");if(d&&d->flexMainAlign==LV_FLEX_ALIGN_CENTER)PASS();else FAIL("wrong");}
    TEST("end"){auto*d=p->findById("je");if(d&&d->flexMainAlign==LV_FLEX_ALIGN_END)PASS();else FAIL("wrong");}
    TEST("between"){auto*d=p->findById("jb");if(d&&d->flexMainAlign==LV_FLEX_ALIGN_SPACE_BETWEEN)PASS();else FAIL("wrong");}
    TEST("around"){auto*d=p->findById("ja");if(d&&d->flexMainAlign==LV_FLEX_ALIGN_SPACE_AROUND)PASS();else FAIL("wrong");}
    TEST("evenly"){auto*d=p->findById("jv");if(d&&d->flexMainAlign==LV_FLEX_ALIGN_SPACE_EVENLY)PASS();else FAIL("wrong");}

    printf("\nAlign:\n");render(H_ALIGN);p=pg();
    TEST("center cross"){auto*d=p->findById("ac");if(d&&d->flexCrossAlign==LV_FLEX_ALIGN_CENTER)PASS();else FAIL("wrong");}
    TEST("end cross"){auto*d=p->findById("ae");if(d&&d->flexCrossAlign==LV_FLEX_ALIGN_END)PASS();else FAIL("wrong");}
    TEST("stretch cross"){auto*d=p->findById("as");if(d&&d->flexCrossAlign==LV_FLEX_ALIGN_STRETCH)PASS();else FAIL("wrong");}

    printf("\nGrow div:\n");render(H_GROW);p=pg();
    TEST("side grow=0"){auto*d=p->findById("side");if(d&&d->flexGrow==0)PASS();else FAIL("wrong");}
    TEST("content grow=1"){auto*d=p->findById("content");if(d&&d->flexGrow==1)PASS();else FAIL("wrong");}
    TEST("mt inside content"){auto*d=p->findById("content");if(d&&d->findById("mt"))PASS();else FAIL("no");}

    printf("\nGrow widget:\n");render(H_GROWW);p=pg();
    TEST("spacer grow=1"){auto*d=p->findById("spacer");if(d&&d->flexGrow==1)PASS();else FAIL("wrong");}

    printf("\n@for+flex:\n");render(H_FOR);p=pg();
    TEST("grid row-wrap"){auto*d=p->findById("grid");if(d&&d->flexFlow==LV_FLEX_FLOW_ROW_WRAP)PASS();else FAIL("wrong");}
    TEST("4 items"){auto*d=p->findById("grid");if(d&&d->count("Container",false)==4)PASS();else FAIL("wrong");}
    TEST("item_0"){if(p->findById("item_0"))PASS();else FAIL("no");}
    TEST("item_3"){if(p->findById("item_3"))PASS();else FAIL("no");}
    TEST("lbl_2='Item 2'"){auto*w=p->findById("lbl_2");if(w&&w->text=="Item 2")PASS();else FAIL("wrong");}

    printf("\n");
    if(passed==total){printf("=== ALL %d FLEX TESTS PASSED ===\n",total);return 0;}
    else{printf("=== %d/%d FLEX TESTS PASSED ===\n",passed,total);return 1;}
}

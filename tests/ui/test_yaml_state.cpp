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

static const char*HF=R"(<app os="2.0"><ui default="/m"><page id="m">
<label id="n">{userName}</label><label id="b">{brightness}</label><label id="e">{enabled}</label><label id="t">{temperature}</label><label id="s">{status}</label>
</page></ui><state>
userName: "Alice"
brightness: 50
enabled: false
temperature: 22.5
status: Ready
</state><script language="lua"></script></app>)";

static const char*HE=R"(<app os="2.0"><ui default="/m"><page id="m"><label id="v">{value}</label></page></ui><state>
# Comment
value: 42
empty1: ""
empty2: ''
negative: -5
zero: 0
pi: 3.14
</state><script language="lua"></script></app>)";

int main(){
    printf("=== YAML STATE TESTS ===\n\n");int passed=0,total=0;
    printf("Flat YAML:\n");render(HF);auto*p=pg();auto&st=g_core.store();
    TEST("userName='Alice'"){if(st.getString("userName")=="Alice")PASS();else FAIL(st.getString("userName").c_str());}
    TEST("brightness=50 int"){if(st.getInt("brightness")==50&&st.getType("brightness")==VarType::Int)PASS();else FAIL("wrong");}
    TEST("enabled=false bool"){if(st.has("enabled")&&!st.getBool("enabled")&&st.getType("enabled")==VarType::Bool)PASS();else FAIL("wrong");}
    TEST("temperature=22.5 float"){float t=st.getFloat("temperature");if(t>22.4f&&t<22.6f&&st.getType("temperature")==VarType::Float)PASS();else FAIL("wrong");}
    TEST("status='Ready' unquoted"){if(st.getString("status")=="Ready")PASS();else FAIL(st.getString("status").c_str());}
    TEST("{userName}->'Alice'"){auto*w=p->findById("n");if(w&&w->text=="Alice")PASS();else FAIL(w?w->text.c_str():"no");}
    TEST("{brightness}->'50'"){auto*w=p->findById("b");if(w&&w->text=="50")PASS();else FAIL(w?w->text.c_str():"no");}

    printf("\nEdge cases:\n");render(HE);st=g_core.store();
    TEST("comment skipped, value=42"){if(st.getInt("value")==42)PASS();else FAIL("wrong");}
    TEST("empty1=''"){if(st.has("empty1")&&st.getString("empty1")=="")PASS();else FAIL("wrong");}
    TEST("empty2=''"){if(st.has("empty2")&&st.getString("empty2")=="")PASS();else FAIL("wrong");}
    TEST("negative=-5"){if(st.getInt("negative")==-5)PASS();else FAIL("wrong");}
    TEST("zero=0"){if(st.has("zero")&&st.getInt("zero")==0)PASS();else FAIL("wrong");}
    TEST("pi=3.14"){float v=st.getFloat("pi");if(v>3.13f&&v<3.15f)PASS();else FAIL("wrong");}

    printf("\n");
    if(passed==total){printf("=== ALL %d YAML STATE TESTS PASSED ===\n",total);return 0;}
    else{printf("=== %d/%d YAML STATE TESTS PASSED ===\n",passed,total);return 1;}
}

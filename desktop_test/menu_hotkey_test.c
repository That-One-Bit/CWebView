#include <stdio.h>
#include "menu_hotkey.h"
static int bad;
static void t(int v,const char *s){printf("%s %s\n",v?"PASS":"FAIL",s);if(!v)bad++;}
int main(void){
 unsigned l=cwv_wiiu_hotkey_bit(6),r=cwv_wiiu_hotkey_bit(7);
 unsigned start=cwv_wiiu_hotkey_bit(10),select=cwv_wiiu_hotkey_bit(11);
 t(l==CWV_WIIU_HK_L&&r==CWV_WIIU_HK_R&&select==CWV_WIIU_HK_SELECT,"SDL button indices");
 t(!cwv_wiiu_hotkey_edge(0,l,l|r),"shoulders alone do not open");
 t(cwv_wiiu_hotkey_edge(0,l|r,l|r|select),"SELECT chord default");
 t(!cwv_wiiu_hotkey_edge(0,l|r,l|r|start),"START chord disabled by default");
 t(cwv_wiiu_hotkey_edge(1,l|r,l|r|start),"legacy START chord opt-in");
 t(cwv_wiiu_hotkey_edge(2,l|r,l|r|start),"either chord opt-in");
 t(!cwv_wiiu_hotkey_edge(0,l|r|select,l|r|select),"no held-key retrigger");
 t(cwv_wiiu_hotkey_edge(0,select|l,select|l|r),"any final chord key triggers");
 return !!bad;
}

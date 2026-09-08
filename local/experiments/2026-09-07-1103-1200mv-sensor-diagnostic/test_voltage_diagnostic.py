from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]
s=(root/'components/asic/mc3.c').read_text()
body=s[s.index('static void mc3_log_voltage_diag_snapshot'):s.index('uint8_t MC3_read_pvt_voltages')]
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#define MC3_VOLTAGE_DONE 1
#define MC3_VOLTAGE_STATUS 2
#define MC3_VOLTAGE_COMMAND 3
#define MC3_VOLTAGE_VDD_CHANNEL_A 13
#define MC3_VOLTAGE_VDD_CHANNEL_B 14
#define MC3_VOLTAGE_STACK_CHANNEL_FIRST 4
#define MC3_VOLTAGE_STACK_CHANNEL_COUNT 12
#define MC3_VOLTAGE_RESOLUTION_BITS 14
#define MC3_VOLTAGE_VDD_SCALE 2.0f
#define MC3_CHIP_NUM_ALL 255
#define TAG "test"
#define pdMS_TO_TICKS(x) (x)
static int mc3_voltage_sensor_lock,mc3_chip_count=4,busy,locks,unlocks,mode;
static unsigned reads,triggers,step_us=1000;
static int64_t now;
static bool done[4];
static char output[40000];
static void logmsg(const char *tag,const char *fmt,...){(void)tag;size_t n=strlen(output);va_list a;va_start(a,fmt);vsnprintf(output+n,sizeof(output)-n,fmt,a);va_end(a);strcat(output,"\n");}
#define ESP_LOGI logmsg
#define ESP_LOGW logmsg
static int pthread_mutex_trylock(int *p){(void)p;if(busy)return 1;locks++;return 0;}
static int pthread_mutex_unlock(int *p){(void)p;unlocks++;return 0;}
static int64_t esp_timer_get_time(void){return now;}
static void vTaskDelay(int ms){now+=(int64_t)ms*1000;}
static uint32_t mc3_voltage_dout_register(uint8_t ch){return 100+ch;}
static float mc3_voltage_from_dout(uint32_t raw){return raw/10000.0f;}
static bool mc3_read_register(uint8_t chip,uint32_t reg,uint32_t *out){assert(chip<4);reads++;now+=step_us;if(mode==3 && chip>=2)return false;if(reg==MC3_VOLTAGE_DONE)*out=done[chip];else if(reg==MC3_VOLTAGE_STATUS)*out=(mode==1 && chip>=2)?3:0;else{assert(reg==104 || reg==113 || reg==114);*out=triggers?6000:4000;done[chip]=false;}return true;}
static bool mc3_write_register(uint8_t chip,uint32_t reg,uint32_t value,uint8_t all){assert(chip==0 && reg==MC3_VOLTAGE_COMMAND && value==0x88000505 && all==255);triggers++;now+=1000;for(int c=0;c<4;c++)done[c]=!(mode==1 && c>=2);return true;}
'''+body+r'''
static void reset(void){output[0]=0;now=0;reads=triggers=locks=unlocks=0;mode=busy=0;step_us=1000;mc3_chip_count=4;memset(done,0,sizeof(done));}
static void expect_fresh(int chip,int fresh){char key[50];snprintf(key,sizeof(key),"phase=after A%d ",chip);char *p=strstr(output,key);assert(p);char *end=strchr(p,'\n');char expected[20];snprintf(expected,sizeof(expected),"fresh=%d ",fresh);char *v=strstr(p,expected);assert(v && v<end);}
int main(void){
 reset();MC3_log_voltage_diagnostic();assert(triggers==12 && locks==1 && unlocks==1);for(int c=1;c<=4;c++)expect_fresh(c,1);
 reset();mode=1;MC3_log_voltage_diagnostic();assert(triggers==12 && now<2500000);expect_fresh(1,1);expect_fresh(2,1);expect_fresh(3,0);expect_fresh(4,0);
 reset();mode=3;MC3_log_voltage_diagnostic();expect_fresh(3,0);expect_fresh(4,0);assert(unlocks==1);
 reset();step_us=25000;MC3_log_voltage_diagnostic();assert(now<=2600000 && triggers<12 && unlocks==1);
 reset();busy=1;MC3_log_voltage_diagnostic();assert(reads==0 && triggers==0 && unlocks==0);
 reset();mc3_chip_count=3;MC3_log_voltage_diagnostic();assert(reads==0 && triggers==0);
 puts("PASS: voltage diagnostic freshness per ASIC, incomplete/failed conversions, bounded slow reads, busy/chain rejection, voltage-only triggers.");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

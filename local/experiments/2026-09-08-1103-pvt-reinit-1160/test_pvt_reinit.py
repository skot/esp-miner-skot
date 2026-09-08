from pathlib import Path
import subprocess,tempfile,re
root=Path(__file__).resolve().parents[3];s=(root/'components/asic/mc3.c').read_text()
body=s[s.index('static void mc3_setup_thermal_sensor(void)'):s.index('static bool mc3_trigger_voltage_conversion(void)')]
regs=['MC3_THERMAL_SDIF_ENABLE','MC3_THERMAL_CLOCK','MC3_THERMAL_COMMAND','MC3_THERMAL_STATUS','MC3_VOLTAGE_SDIF_ENABLE','MC3_VOLTAGE_CLOCK','MC3_VOLTAGE_COMMAND','MC3_VOLTAGE_STATUS','MC3_VOLTAGE_CLOCK_DEFAULT','MC3_VOLTAGE_STACK_CHANNEL_MASK','MC3_PLL0_EN']
defs='\n'.join(re.search(r'^#define '+r+r' .+$',s,re.M)[0] for r in regs)
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#define TAG "test"
#define MC3_CHIP_NUM_ALL 255
#define ESP_LOGI(tag, ...) ignored_log(__VA_ARGS__)
#define ESP_LOGW(tag, ...) ignored_log(__VA_ARGS__)
static void ignored_log(const char *fmt,...){(void)fmt;}
static int mc3_voltage_sensor_lock,mc3_thermal_sensor_lock,mc3_chip_count=4,busy;
static unsigned writes,reads,waits,locks,unlocks,read_chips;
static int64_t now;
'''+defs+r'''
static int pthread_mutex_trylock(int *p){if((busy==1 && p==&mc3_voltage_sensor_lock)||(busy==2 && p==&mc3_thermal_sensor_lock))return 1;locks++;return 0;}
static int pthread_mutex_unlock(int *p){(void)p;unlocks++;return 0;}
static bool mc3_write_register(uint8_t chip,uint32_t reg,uint32_t value,uint8_t all){
 static const uint32_t expected_reg[]={MC3_VOLTAGE_SDIF_ENABLE,MC3_VOLTAGE_CLOCK,MC3_VOLTAGE_COMMAND,MC3_VOLTAGE_COMMAND,MC3_VOLTAGE_COMMAND,MC3_THERMAL_SDIF_ENABLE,MC3_THERMAL_CLOCK,MC3_THERMAL_COMMAND,MC3_THERMAL_COMMAND};
 static const uint32_t expected_value[]={0,0x01010000,0x89000000,0x8D000040,0x8C10FFF0,0,0x01010000,0x89000000,0x8D000200};
 assert(chip==0 && all==255 && writes<9 && reg==expected_reg[writes] && value==expected_value[writes]);writes++;now+=1000;return true;
}
static bool mc3_wait_register(uint8_t chip,uint32_t reg,uint32_t expected,unsigned timeout){assert(chip==0 && expected==0 && timeout==500 && (reg==MC3_VOLTAGE_STATUS || reg==MC3_THERMAL_STATUS));waits++;now+=500000;return false;}
static bool mc3_read_register(uint8_t chip,uint32_t reg,uint32_t *out){assert(chip<4 && (reg==MC3_VOLTAGE_STATUS || reg==MC3_THERMAL_STATUS || reg==MC3_VOLTAGE_CLOCK || reg==MC3_THERMAL_CLOCK || reg==MC3_PLL0_EN));reads++;read_chips|=1<<chip;now+=1000;*out=reg==MC3_VOLTAGE_CLOCK || reg==MC3_THERMAL_CLOCK?0x01010000:0;return true;}
'''+body+r'''
static void reset(void){writes=reads=waits=locks=unlocks=read_chips=0;now=0;busy=0;mc3_chip_count=4;}
int main(void){
 reset();assert(MC3_reinitialize_pvt_sensors());assert(writes==9 && reads==20 && waits==3 && read_chips==15 && locks==2 && unlocks==2 && now<1600000);
 reset();busy=1;assert(!MC3_reinitialize_pvt_sensors());assert(!writes && !reads && !unlocks);
 reset();busy=2;assert(!MC3_reinitialize_pvt_sensors());assert(!writes && !reads && locks==1 && unlocks==1);
 reset();mc3_chip_count=3;assert(!MC3_reinitialize_pvt_sensors());assert(!writes && !reads);
 puts("PASS: exact PVT-only setup commands, no PLL/work writes, all-chip readback, bounded setup waits, busy locks and chain rejection.");
}
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

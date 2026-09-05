from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3];s=(root/'main/tasks/power_management_task.c').read_text()
a=s.index('static void update_asic_telemetry(');b=s.index('\nstatic void update_domain_voltage_telemetry',a)
code=r'''
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#define MAX_ASIC_TEMPS 8
#define PROTO 1
#define MC3 3
#define ESP_LOGE(...) ((void)0)
typedef struct {float asic_temps[8],asic_voltages[8],chip_temp_avg,chip_temp2_avg,core_voltage;uint8_t asic_temp_count,asic_voltage_count;} PowerManagementModule;
typedef struct {PowerManagementModule POWER_MANAGEMENT_MODULE;bool ASIC_initalized;struct {struct {int id,asic_count;struct {int id;} asic;} family;} DEVICE_CONFIG;struct {bool hardware_fault;char hardware_fault_msg[64];} SYSTEM_MODULE;} GlobalState;
static int count=4;static float temp=55;
static uint8_t MC3_read_temperatures(float *t,int max){(void)max;for(int i=0;i<count;i++)t[i]=i==2?temp:55;return count;}
static uint8_t MC3_read_vdd_voltages(float *v,int max){(void)max;for(int i=0;i<4;i++)v[i]=1160;return 4;}
static float Thermal_get_chip_temp(GlobalState *g){(void)g;return 40;}
static float Thermal_get_chip_temp2(GlobalState *g){(void)g;return 40;}
'''+s[a:b]+r'''
int main(void){
 for(int mode=0;mode<6;mode++){
  GlobalState g={.ASIC_initalized=true,.DEVICE_CONFIG={.family={.id=PROTO,.asic_count=4,.asic={MC3}}}};
  count=mode==4?3:mode==5?0:4;temp=mode==1?-1:mode==2?NAN:mode==3?INFINITY:55;
  update_asic_telemetry(&g);
  assert(g.SYSTEM_MODULE.hardware_fault==(mode!=0));
  if(mode==0)assert(g.POWER_MANAGEMENT_MODULE.asic_temp_count==4 && g.POWER_MANAGEMENT_MODULE.chip_temp_avg==55);
 }
 puts("PASS: pool telemetry requires all fresh Proto temperatures; invalid/NaN/infinite/missing readings latch a fault instead of reusing cached values.");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(code);subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)

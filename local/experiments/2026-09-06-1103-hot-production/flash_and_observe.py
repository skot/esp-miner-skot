from pathlib import Path
import hashlib,json,shutil,subprocess,time,urllib.request
p=Path(__file__).resolve().parent;root=p.parents[2];prep=json.loads((p/'preparation.json').read_text())
assert hashlib.sha256((root/'components/asic/mc3.c').read_bytes()).hexdigest()==prep['mc3_before_sha256']
manifest={}
for dest,source in {'esp-miner.bin':'build/esp-miner.bin','esp-miner.elf':'build/esp-miner.elf','asic-init-source.c':'main/power/asic_init.c','mc3-source.c':'components/asic/mc3.c'}.items():
 shutil.copyfile(root/source,p/dest);manifest[dest]=hashlib.sha256((p/dest).read_bytes()).hexdigest()
(p/'firmware-identity.json').write_text(json.dumps({'sha256':manifest,'app_only':True,'scope_used':False,'first_work_source_unchanged':True},indent=2)+'\n')
while time.time()-prep['cooling_started_unix']<120:
 remaining=120-(time.time()-prep['cooling_started_unix']);print('Cooling with fan 100%, seconds remaining',round(remaining),flush=True);time.sleep(min(20,remaining))
a=json.load(urllib.request.urlopen('http://192.168.30.9/api/system/info',timeout=10))
assert a['macAddr'].upper()=='28:84:85:BA:96:08' and a['regulatorVoltage']<100 and a['fanspeed']>=99
req=urllib.request.Request('http://192.168.30.9/api/system',data=json.dumps({'autofanspeed':prep['original_autofanspeed'],'fanspeed':prep['original_manual_fan']}).encode(),method='PATCH',headers={'Content-Type':'application/json'})
assert urllib.request.urlopen(req,timeout=10).status==200
with (p/'flash.log').open('w') as log:
 subprocess.run(['/Users/skot/.espressif/python_env/idf5.5_py3.9_env/bin/python','-m','esptool','--chip','esp32s3','--port','/dev/cu.usbmodem11434201','--baud','460800','--before','default_reset','--after','hard_reset','write_flash','0x10000',str(p/'esp-miner.bin')],stdout=log,stderr=subprocess.STDOUT,check=True)
print('Flash verified; capturing startup and five-minute pool hold',flush=True)
subprocess.run(['/tmp/bitaxe-bench-env/bin/python','-u',str(p/'observe.py')],check=True)

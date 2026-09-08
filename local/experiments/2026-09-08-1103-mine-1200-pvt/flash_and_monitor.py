import json, pathlib, time, urllib.request, hashlib
p=pathlib.Path(__file__).resolve().parent
base='http://192.168.30.9'
keys=['version','axeOSVersion','boardVersion','macAddr','actualFrequency','hashRate','asicTemps','asicVoltages','domainVoltages','regulatorVoltage','miningPaused','uptimeSeconds','sharesAccepted','sharesRejected','power','fanspeed','coreVoltage','frequency','hardwareFault']
def get(path):
    with urllib.request.urlopen(base+path,timeout=15) as r:return json.load(r)
def info():
    data=get('/api/system/info');return {k:data[k] for k in keys if k in data}
def save(name,data):(p/name).write_text(json.dumps(data,indent=2)+'\n')
prep=json.loads((p/'preparation.json').read_text())
assert hashlib.sha256(pathlib.Path('components/asic/mc3.c').read_bytes()).hexdigest()==prep['mc3_sha256']
while time.time()-prep['cooling_started_unix']<120:
    remaining=120-(time.time()-prep['cooling_started_unix'])
    print('Fan 100% cooling; seconds remaining:',round(remaining),flush=True)
    time.sleep(min(20,remaining))
before=info();assert before['boardVersion']=='1103' and before['macAddr']=='28:84:85:BA:96:08'
assert before['coreVoltage']==1160 and before['frequency']==600 and before['regulatorVoltage']<100 and before['fanspeed']==100
save('before.json',before)
request=urllib.request.Request(base+'/api/system',data=json.dumps({**prep['original_fan'],'coreVoltage':1200}).encode(),method='PATCH',headers={'Content-Type':'application/json'})
with urllib.request.urlopen(request,timeout=15) as response:assert response.status==200
configured=info();assert configured['coreVoltage']==1200 and configured['frequency']==600 and configured['regulatorVoltage']<100
save('configured.json',configured)
manifest={'warmup_coldest_c':40,'first_work_minimum_c':40,'high_frequency_step_ms':2000,'warmup_voltage_mv':1160,'ramp_voltage_mv':1200,'mining_voltage_mv':1200,'pvt_reinit_before_work':True,'paired_pvt_readings_before_work':True,'voltage_drop':False,'core_voltage_mv':1200,'mc3_sha256':prep['mc3_sha256'],'version':pathlib.Path('main/http_server/axe-os/dist/axe-os/version.txt').read_text().strip(),'files':{}}
for name,endpoint in [('esp-miner.bin','OTA')]:
    data=pathlib.Path('build',name).read_bytes();(p/name).write_bytes(data)
    manifest['files'][name]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    save('manifest.json',manifest)
    print('Uploading',name,len(data),'bytes',flush=True)
    req=urllib.request.Request(base+'/api/system/'+endpoint,data=data,method='POST',headers={'Content-Type':'application/octet-stream'})
    with urllib.request.urlopen(req,timeout=180) as r:
        response=r.read().decode();print(response.strip(),flush=True)
        (p/(endpoint+'-response.log')).write_text(response)
    if endpoint=='OTAWWW':time.sleep(2)
start=time.monotonic(); pool_start=None; reboot_seen=False; samples=[]; last_print=-30; n=0
with (p/'samples.jsonl').open('w') as f:
    while time.monotonic()-start<900:
        elapsed=time.monotonic()-start
        try:
            state=info(); diag=get('/api/system/diagnostics')
            reboot_seen=reboot_seen or state['uptimeSeconds']<before['uptimeSeconds']
            row={'elapsed':elapsed,'info':state,'diagnostics':diag};samples.append(row);f.write(json.dumps(row)+'\n');f.flush()
            if reboot_seen and diag.get('mining')==1 and diag.get('startupResult')=='passed':
                if pool_start is None:pool_start=elapsed
            if elapsed-last_print>=25:
                print(json.dumps({'elapsed':round(elapsed),'uptime':state['uptimeSeconds'],'version':state['version'],'frequency':state['actualFrequency'],'ghs':state['hashRate'],'temps':state['asicTemps'],'status':diag.get('status'),'startupResult':diag.get('startupResult'),'accepted':state['sharesAccepted'],'rejected':state['sharesRejected']}),flush=True);last_print=elapsed
            if n%2==0:
                try:
                    with urllib.request.urlopen(base+'/api/system/logs',timeout=10) as r:log=r.read()
                    (p/'device-latest.log').write_bytes(log)
                    with (p/'device-all.log').open('ab') as lf:lf.write(log+b'\n')
                except Exception:pass
            n+=1
            if reboot_seen and diag.get('startupResult')=='failed':
                save('failure.json',row)
                with urllib.request.urlopen(base+'/api/system/logs',timeout=15) as r:log=r.read()
                (p/'device-latest.log').write_bytes(log)
                with (p/'device-all.log').open('ab') as lf:lf.write(log+b'\n')
                raise RuntimeError('Startup failed; preserving state for diagnosis')
            if pool_start is not None and elapsed-pool_start>=300:
                save('final.json',row)
                pool=[s['info']['hashRate'] for s in samples if s['elapsed']>=pool_start]
                save('result.json',{'reboot_seen':reboot_seen,'pool_seconds':elapsed-pool_start,'api_rate_samples':len(pool),'mean_ghs':sum(pool)/len(pool),'min_ghs':min(pool),'max_ghs':max(pool),'final':state})
                print('PASS: five-minute mining observation complete',flush=True);break
        except RuntimeError:raise
        except Exception as exc:print(type(exc).__name__,str(exc)[:180],flush=True)
        time.sleep(5)
    else:raise RuntimeError('Startup/mining observation timed out')

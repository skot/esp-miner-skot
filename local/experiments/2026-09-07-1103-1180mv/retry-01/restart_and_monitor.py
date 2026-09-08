import json, pathlib, time, urllib.request, hashlib
p=pathlib.Path(__file__).resolve().parent
base='http://192.168.30.9'
keys=['version','axeOSVersion','boardVersion','macAddr','actualFrequency','hashRate','asicTemps','asicVoltages','domainVoltages','regulatorVoltage','miningPaused','uptimeSeconds','sharesAccepted','sharesRejected','power','fanspeed','coreVoltage','frequency','hardwareFault']
def get(path):
    with urllib.request.urlopen(base+path,timeout=15) as r:return json.load(r)
def info():
    data=get('/api/system/info');return {k:data[k] for k in keys if k in data}
def save(name,data):(p/name).write_text(json.dumps(data,indent=2)+'\n')
before=info();assert before['boardVersion']=='1103' and before['macAddr']=='28:84:85:BA:96:08';save('before.json',before)
assert before['coreVoltage']==1180 and before['frequency']==600
request=urllib.request.Request(base+'/api/system/restart',data=b'',method='POST')
with urllib.request.urlopen(request,timeout=15) as response:
    print('Identical 1180 mV restart:',response.status,flush=True)
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
                save('failure.json',row);raise RuntimeError('Startup failed; preserving state for diagnosis')
            if pool_start is not None and elapsed-pool_start>=300:
                save('final.json',row)
                pool=[s['info']['hashRate'] for s in samples if s['elapsed']>=pool_start]
                save('result.json',{'reboot_seen':reboot_seen,'pool_seconds':elapsed-pool_start,'api_rate_samples':len(pool),'mean_ghs':sum(pool)/len(pool),'min_ghs':min(pool),'max_ghs':max(pool),'final':state})
                print('PASS: five-minute mining observation complete',flush=True);break
        except RuntimeError:raise
        except Exception as exc:print(type(exc).__name__,str(exc)[:180],flush=True)
        time.sleep(5)
    else:raise RuntimeError('Startup/mining observation timed out')

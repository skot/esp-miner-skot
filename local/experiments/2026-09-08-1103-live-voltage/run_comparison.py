"""App OTA followed by a leased, voltage-only 1200 -> 1160 -> 1200 comparison."""
import hashlib, json, pathlib, re, statistics, time, urllib.request
P = pathlib.Path(__file__).resolve().parent
BASE = 'http://192.168.30.9'
KEYS = ['version','boardVersion','macAddr','actualFrequency','hashRate','asicTemps','asicVoltages','domainVoltages','regulatorVoltage','miningPaused','uptimeSeconds','sharesAccepted','sharesRejected','power','fanspeed','coreVoltage','frequency','hardwareFault']
def request(path, data=None, method=None, timeout=15):
    req = urllib.request.Request(BASE + path, data=data, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as r: return r.read()
def get(path): return json.loads(request(path))
def info():
    s = get('/api/system/info'); return {k:s[k] for k in KEYS if k in s}
def save(name, obj): (P/name).write_text(json.dumps(obj, indent=2)+'\n')
def voltage(mv): request('/api/system/diagnostics/voltage?millivolts='+str(mv), b'', 'POST')
def logs():
    data=request('/api/system/logs')
    with (P/'device-all.log').open('ab') as f: f.write(data+b'\n')
    (P/'device-latest.log').write_bytes(data)
    return re.sub(r'\x1b\[[0-9;]*m', '', data.decode(errors='replace'))
def counters(d):
    if d.get('source')!='mining' or len(d['chips'])!=4: return None
    if any(c['valid']!=1 or c['windowSeconds']<=0 or c['ageSeconds']>15 or len(c['corePassCounts'])!=156 or c['frequency']!=600 for c in d['chips']): return None
    domains=[]; zeros=[]; fails=0
    for c in d['chips']:
        factor=2**d['leadingZeros']/c['windowSeconds']/1e9
        domains += [sum(c['corePassCounts'][39*i:39*(i+1)])*factor for i in range(4)]
        zeros += [(c['chipId']+1,i) for i,n in enumerate(c['corePassCounts']) if n==0]
        fails += sum(c['coreFailCounts'])
    return {'ghs':sum(domains),'domains':domains,'zero_cores':zeros,'fail_counts':fails,'epoch':d['epoch']}

def main():
    before=info(); save('before.json',before)
    assert before['macAddr']=='28:84:85:BA:96:08' and before['boardVersion']=='1103'
    assert before['coreVoltage']==1200 and before['frequency']==600
    data=pathlib.Path('build/esp-miner.bin').read_bytes(); (P/'esp-miner.bin').write_bytes(data)
    save('manifest.json', {'app_sha256':hashlib.sha256(data).hexdigest(),'base_commit':'cef666d55805674210828250f49df88420f05519','startup_unchanged':True,'saved_voltage_mv':1200,'frequency_mhz':600,'steps_mv':[1200,1190,1180,1170,1160,1200],'lease_seconds':90,'mc3_sha256':hashlib.sha256(pathlib.Path('components/asic/mc3.c').read_bytes()).hexdigest()})
    print('Uploading app-only OTA', len(data),flush=True)
    print(request('/api/system/OTA',data,'POST',180).decode().strip(),flush=True)
    start=time.monotonic(); last_print=-30; reboot=False
    with (P/'startup-samples.jsonl').open('w') as f:
        while time.monotonic()-start<600:
            elapsed=time.monotonic()-start
            try:
                s=info();d=get('/api/system/diagnostics')
            except Exception as e:
                print('Startup connection:',type(e).__name__,flush=True);time.sleep(5);continue
            reboot |= s['uptimeSeconds']<before['uptimeSeconds']
            f.write(json.dumps({'elapsed':elapsed,'info':s,'diagnostics':d})+'\n');f.flush()
            if elapsed-last_print>=25:
                print(json.dumps({'phase':'startup','elapsed':round(elapsed),'status':d['status'],'freq':s['actualFrequency'],'temps':s['asicTemps']}),flush=True);last_print=elapsed;logs()
            if reboot and d['startupResult']=='failed': raise RuntimeError('Startup failed; voltage test not started')
            if reboot and d['mining']==1 and d['startupResult']=='passed' and counters(d): break
            time.sleep(5)
        else: raise RuntimeError('Startup timeout')
    save('startup-ready.json',{'info':s,'diagnostics':d})
    assert d.get('liveVoltageMv')==1200
    log_floor=s['uptimeSeconds']*1000
    seen_logs=set(logs().splitlines()); all_results=[]; baseline_zero=set(); baseline_rate=None
    def phase(mv, duration, name, guard=True):
        nonlocal baseline_rate, baseline_zero
        voltage(mv);begin=time.monotonic();last_renew=begin;last_print=-30;seen=set();windows=[];samples=[];new_zero_runs={};low_runs=0;changed=False
        print('BEGIN',name,mv,duration,flush=True)
        with (P/'samples.jsonl').open('a') as f:
            while time.monotonic()-begin < duration:
                elapsed=time.monotonic()-begin
                if time.monotonic()-last_renew>=25: voltage(mv);last_renew=time.monotonic()
                s=info(); d=get('/api/system/diagnostics'); c=counters(d)
                row={'phase':name,'target_mv':mv,'elapsed':elapsed,'info':s,'diagnostics':d};f.write(json.dumps(row)+'\n');f.flush();samples.append(row)
                if s['actualFrequency']!=600 or s['coreVoltage']!=1200 or d['mining']!=1 or d['startupResult']!='passed': raise RuntimeError('Operating state changed')
                if any(t<=0 or t>=70 for t in s['asicTemps']) or len(s['asicTemps'])!=4: raise RuntimeError('Temperature invalid/hot')
                if d['liveVoltageMv']==mv: changed=True
                if elapsed>15 and (not changed or d['liveVoltageMv']!=mv): raise RuntimeError('Voltage override not applied')
                if elapsed>15 and abs(s['regulatorVoltage']-2*mv)>30: raise RuntimeError('Regulator not at requested voltage')
                if elapsed>15 and c and c['epoch'] not in seen:
                    seen.add(c['epoch']);c.update(power_w=s['power'],elapsed=elapsed,temps=s['asicTemps'],asic_voltages=s['asicVoltages']);windows.append(c)
                    zs={tuple(z) for z in c['zero_cores']}
                    if name=='baseline':baseline_zero |= zs
                    new=zs-baseline_zero
                    new_zero_runs={z:new_zero_runs.get(z,0)+1 for z in new}
                    low = min(c['domains']) < 600*.312*.90 or (baseline_rate and c['ghs']<baseline_rate*.97)
                    low_runs=low_runs+1 if low else 0
                    if guard and (low_runs>=3 or max(new_zero_runs.values(),default=0)>=3):
                        save('degradation.json',{'phase':name,'target_mv':mv,'counter':c,'new_zero_runs':[(z,n) for z,n in new_zero_runs.items()]})
                        raise RuntimeError('Persistent domain/core degradation at '+str(mv))
                if elapsed-last_print>=25:
                    print(json.dumps({'phase':name,'elapsed':round(elapsed),'mv':d['liveVoltageMv'],'rail_mv':s['regulatorVoltage'],'ghs':round(c['ghs'],2) if c else None,'zero_cores':c['zero_cores'] if c else None,'W':round(s['power'],2),'temps':[round(t,1) for t in s['asicTemps']],'shares':[s['sharesAccepted'],s['sharesRejected']]}),flush=True);last_print=elapsed
                    for line in logs().splitlines():
                        m=re.search(r'\((\d+)\)',line)
                        if not m or int(m[1])<log_floor or line in seen_logs: continue
                        seen_logs.add(line)
                        if 'THERMAL invalid after bounded retries' in line: raise RuntimeError('PVT conversion failure while mining')
                time.sleep(5)
        if len(windows)<8: raise RuntimeError('Insufficient fresh counter windows')
        summary={'name':name,'millivolts':mv,'duration_seconds':time.monotonic()-begin,'complete_epochs':len(windows),'mean_ghs':statistics.mean(w['ghs'] for w in windows),'min_ghs':min(w['ghs'] for w in windows),'mean_power_w':statistics.mean(w['power_w'] for w in windows),'mean_j_per_th':statistics.mean(w['power_w']/(w['ghs']/1000) for w in windows),'min_domain_percent':min(min(w['domains'])/(600*.312)*100 for w in windows),'zero_core_union':sorted(set(tuple(z) for w in windows for z in w['zero_cores'])),'max_temp_c':max(t for w in windows for t in w['temps']),'accepted_delta':samples[-1]['info']['sharesAccepted']-samples[0]['info']['sharesAccepted'],'rejected_delta':samples[-1]['info']['sharesRejected']-samples[0]['info']['sharesRejected'],'temperature_distinct_values':[len(set(w['temps'][i] for w in windows)) for i in range(4)],'voltage_distinct_values':[len(set(w['asic_voltages'][i] for w in windows)) for i in range(4)]}
        all_results.append(summary);save('results.json',all_results);save(name+'-epochs.json',windows)
        print('RESULT',json.dumps(summary),flush=True)
        if name=='baseline':baseline_rate=summary['mean_ghs']
    error=None
    try:
        phase(1200,120,'baseline')
        for mv in [1190,1180,1170,1160]: phase(mv,300 if mv==1160 else 120,'voltage-'+str(mv))
    except Exception as e:
        error=str(e);save('test-error.json',{'error':error});print('STOP:',error,flush=True)
    finally:
        voltage(1200)
        phase(1200,120,'restore-1200',guard=False)
        save('final.json',{'info':info(),'diagnostics':get('/api/system/diagnostics'),'test_error':error})
        logs()
        print('COMPLETE; restored 1200 mV without PLL/work changes',flush=True)
if __name__=='__main__':main()

"""Stock 1103 warm power-cycle and deferred-work supervisor with prefilled long capture."""
import json,re,socket,threading,time,queue,os
from pathlib import Path
import numpy as np
import serial,vxi11
from scope_limits import assess, MEAN_WINDOW_S
p=Path(__file__).resolve().parent
serial_port=os.environ['BITAXE_BENCH_PORT']  # Required: select the connected stock 1103 explicitly.
ended=threading.Event();quit_reader=threading.Event();armed=threading.Event();launched=threading.Event()
ready=queue.Queue();lines=[];port=None;reader=None
s=vxi11.Instrument('192.168.1.250');s.timeout=10

def block(data):
    assert data[:1]==b'#'
    n=int(chr(data[1]));length=int(data[2:2+n]);payload=data[2+n:2+n+length]
    assert len(payload)==length
    return payload

def check_inputs():
    settings={q:s.ask(q) for q in [':CHAN1:IMP?',':CHAN2:IMP?',':CHAN1:COUP?',':CHAN2:COUP?',':CHAN1:PROB?',':CHAN2:PROB?']}
    assert settings[':CHAN1:IMP?']=='FIFT', 'CH1 must remain 50 ohms'
    assert settings[':CHAN2:IMP?']=='OMEG', 'CH2 setup changed'
    assert settings[':CHAN1:COUP?']==settings[':CHAN2:COUP?']=='DC'
    assert settings[':CHAN1:PROB?']==settings[':CHAN2:PROB?']=='1'
    return settings

def arm_scope():
    check_inputs()
    s.write(':SINGle')
    # At this timebase the first WAIT can precede usable pretrigger data.
    # The old-image control verified 16s prefill at 5s/div with 700k points.
    time.sleep(16)
    limit=time.monotonic()+10
    while s.ask(':TRIG:STAT?')!='WAIT':
        if time.monotonic()>limit:raise RuntimeError('Scope did not arm')
        time.sleep(.05)

def read_capture(name, target=None):
    out=p/name;out.mkdir(exist_ok=True)
    inputs_before=check_inputs()
    time.sleep(.5)
    assert s.ask(':TRIG:STAT?')=='STOP'
    arrays={};preambles={};early=False;t0=time.monotonic()
    for c in ['CHAN1','CHAN2']:
        for cmd in [f':WAV:SOUR {c}',':WAV:MODE RAW',':WAV:FORM BYTE',':WAV:POIN 700000']:s.write(cmd)
        preambles[c]=s.ask(':WAV:PRE?')
        assert int(preambles[c].split(',')[2])==700000
        arrays[c]=np.empty(700000,dtype=np.uint8)
    pre={c:np.array(v.split(','),float) for c,v in preambles.items()}
    assert np.array_equal(pre['CHAN1'][4:7],pre['CHAN2'][4:7])
    tails = {'lower': np.array([]), 'upper': np.array([])}
    carry = max(0, round(MEAN_WINDOW_S / pre['CHAN1'][4]) - 1)
    for start in range(1,700001,250000):
        stop=min(700000,start+249999)
        for c in ['CHAN2','CHAN1']:
            for cmd in [f':WAV:SOUR {c}',f':WAV:STAR {start}',f':WAV:STOP {stop}']:s.write(cmd)
            payload=block(s.ask_raw(b':WAV:DATA?'));assert len(payload)==stop-start+1
            arrays[c][start-1:stop]=np.frombuffer(payload,np.uint8)
        chunk={c:a[start-1:stop] for c,a in arrays.items()}
        v={c:(a.astype(float)-pre[c][8]-pre[c][9])*pre[c][7] for c,a in chunk.items()}
        lower = np.r_[tails['lower'], v['CHAN2']]
        upper = np.r_[tails['upper'], v['CHAN1']-v['CHAN2']]
        limits = assess(lower, upper, pre['CHAN1'][4])
        tails = {'lower': lower[-carry:] if carry else np.array([]), 'upper': upper[-carry:] if carry else np.array([])}
        if not early and (any(np.any((a==0)|(a==255)) for a in chunk.values()) ):
            early=True;port.write(b'X');port.flush()
            print(name,'capture clipped; X sent after',round(time.monotonic()-t0,3),'s',flush=True)
    np.savez_compressed(out/'raw.npz',**arrays)
    (out/'preambles.json').write_text(json.dumps(preambles,indent=2)+'\n')
    v={c:(a.astype(float)-pre[c][8]-pre[c][9])*pre[c][7] for c,a in arrays.items()}
    valid={c:(a>0)&(a<255) for c,a in arrays.items()};both=valid['CHAN1']&valid['CHAN2']
    end=(699999-pre['CHAN1'][6])*pre['CHAN1'][4]+pre['CHAN1'][5]
    audit=dict(lower_max_v=float(v['CHAN2'][valid['CHAN2']].max()),upper_max_v=float((v['CHAN1']-v['CHAN2'])[both].max()),total_max_v=float(v['CHAN1'][valid['CHAN1']].max()),clipped_samples=int((~both).sum()),record_end_s=float(end),download_s=time.monotonic()-t0)
    covered=True
    if target is not None:
        log=''.join(lines)
        a=re.search(r'\((\d+)\).*DEFER TRIAL\b',log)
        b=re.search(r'\((\d+)\).*DEFER LAUNCHED\b',log)
        dt=(int(b[1])-int(a[1]))/1000 if a and b else None
        audit['launch_duration_s']=dt
        covered=dt is not None and dt+.05<end
    inputs_after=check_inputs()
    (out/'input-settings.json').write_text(json.dumps({'before':inputs_before,'after':inputs_after},indent=2)+'\n')
    audit.update(assess(v['CHAN2'], v['CHAN1']-v['CHAN2'], pre['CHAN1'][4]))
    audit['voltage_limits_enforced']=False
    audit['accepted']=bool(not early and not audit['clipped_samples'] and covered and not ended.is_set())
    (out/'audit.json').write_text(json.dumps(audit,indent=2)+'\n')
    print(name,audit,flush=True)
    return audit['accepted']

try:
    s.open();s.client.sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1);s.clear();s.max_recv_size=262144
    print(s.ask('*IDN?'),flush=True)
    # One long acquisition covers reset, the entire no-work ramp and first work.
    # Readback/audit must confirm actual coverage; no assumption about clock gating.
    for cmd in [':CHAN1:IMP FIFT',':STOP',':TIM:MODE MAIN',':TIM:SCAL 5',':TIM:OFFS 25',':ACQ:MDEP 700000',':ACQ:TYPE NORM',':CHAN1:SCAL 0.5',':CHAN1:OFFS -1.16',':CHAN2:SCAL 0.5',':CHAN2:OFFS -1.16',':TRIG:MODE EDGE',':TRIG:EDGE:SOUR CHAN2',':TRIG:EDGE:SLOP POS',':TRIG:EDGE:LEV 2.0']:
        s.write(cmd)
    settings=check_inputs()
    for q in [':ACQ:MDEP?',':ACQ:SRAT?',':TIM:SCAL?',':TIM:OFFS?',':CHAN1:BWLimit?',':CHAN2:BWLimit?']:settings[q]=s.ask(q)
    assert settings[':ACQ:MDEP?']=='700000'
    assert settings[':CHAN1:BWLimit?']==settings[':CHAN2:BWLimit?']=='20M'
    (p/'scope-settings.json').write_text(json.dumps(settings,indent=2)+'\n')
    port=serial.Serial(serial_port,115200,timeout=.2)
    def read_serial():
        with (p/'serial.log').open('w',buffering=1) as log:
            while not quit_reader.is_set():
                line=re.sub(r'\x1b\[[0-9;]*m','',port.readline().decode(errors='replace'))
                log.write(line);lines.append(line)
                if 'WARM ' in line or 'DEFER ' in line or 'THERMAL retry' in line or 'THERMAL recovered' in line or 'THERMAL invalid' in line or 'SAFETY STOP' in line:print(line,end='',flush=True)
                if 'DEFER READY' in line:ready.put(600)
                if 'DEFER ARMED' in line:armed.set()
                if 'DEFER LAUNCHED' in line:launched.set()
                if 'WARM END' in line:ended.set()
    reader=threading.Thread(target=read_serial,daemon=True);reader.start()
    limit=time.monotonic()+900
    while not ended.is_set():
        if time.monotonic()>limit:raise RuntimeError('Warmup/ready timeout')
        try:ready.get(timeout=.2);break
        except queue.Empty:continue
    if not ended.is_set():
        arm_scope();armed.clear();launched.clear();port.write(b'G');port.flush()
        if not armed.wait(3):raise RuntimeError('Missing G acknowledgement')
        s.write(':TFORce');port.write(b'T');port.flush()
        limit=time.monotonic()+40
        while not launched.wait(.1):
            if ended.is_set():break
            if time.monotonic()>limit:raise RuntimeError('Reset/ramp/first-work timeout')
        # Preserve the triggered waveform even if the MCU stops during reset/ramp.
        limit=time.monotonic()+75
        while s.ask(':TRIG:STAT?')!='STOP':
            if time.monotonic()>limit:raise RuntimeError('Incomplete long capture')
            time.sleep(.1)
        accepted=read_capture('deferred-start',600)
        if not ended.is_set():
            port.write(b'R' if accepted else b'X');port.flush()
            limit=time.monotonic()+140
            while not ended.wait(.2):
                if time.monotonic()>limit:raise RuntimeError('Final observation timeout')
    time.sleep(.5)
finally:
    if port:
        if not ended.is_set():port.write(b'X');port.flush()
        quit_reader.set()
        if reader:reader.join(2)
        port.close()
    try:
        s.write(':CHAN1:IMP FIFT');s.write(':STOP')
        (p/'final-input-settings.json').write_text(json.dumps(check_inputs(),indent=2)+'\n')
    finally:s.close()

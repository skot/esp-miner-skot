"""One guarded broadcast test. USB G launches; R follows a passing raw-scope audit."""
import json,re,socket,threading,time
from pathlib import Path
import numpy as np
import serial,vxi11
from PIL import Image
p=Path(__file__).resolve().parent
ready=threading.Event();launched=threading.Event();ended=threading.Event();quit_reader=threading.Event()
lines=[]
def block(data):
    assert data[:1]==b'#'
    n=int(chr(data[1]));length=int(data[2:2+n]);payload=data[2+n:2+n+length]
    assert len(payload)==length
    return payload
s=vxi11.Instrument('192.168.1.250');s.timeout=15
port=None;reader=None
try:
    s.open();s.client.sock.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1);s.clear()
    print(s.ask('*IDN?'),flush=True)
    for cmd in [':RUN',':TIM:SCAL 0.02',':TIM:OFFS 0.1',':ACQ:MDEP 7000000',':ACQ:TYPE NORM',
                ':CHAN1:SCAL 0.2',':CHAN1:OFFS -2.28',':CHAN2:SCAL 0.1',':CHAN2:OFFS -1.14',
                ':CALC:MODE SUB',':CALC:SUB:SA CHAN1',':CALC:SUB:SB CHAN2',':CALC:SUB:VSC 0.1',':CALC:SUB:VOFF -1.14',
                ':TRIG:MODE EDGE',':TRIG:EDGE:SOUR CHAN2',':TRIG:EDGE:SLOP POS',':TRIG:EDGE:LEV 1.20']:
        s.write(cmd)
    settings={q:s.ask(q) for q in [':ACQ:MDEP?',':ACQ:SRAT?',':TIM:SCAL?',':TIM:OFFS?',':CHAN1:BWLimit?',':CHAN2:BWLimit?',':CHAN1:PROB?',':CHAN2:PROB?']}
    assert int(settings[':ACQ:MDEP?'])==7000000
    assert settings[':CHAN1:BWLimit?']==settings[':CHAN2:BWLimit?']=='20M'
    assert settings[':CHAN1:PROB?']==settings[':CHAN2:PROB?']=='1'
    (p/'transition-settings.json').write_text(json.dumps(settings,indent=2)+'\n')
    print(settings,flush=True)
    port=serial.Serial('/dev/cu.usbmodem11434401',115200,timeout=.2)
    def read_serial():
        with (p/'transition-serial.log').open('w',buffering=1) as log:
            while not quit_reader.is_set():
                line=re.sub(r'\x1b\[[0-9;]*m','',port.readline().decode(errors='replace'))
                log.write(line);lines.append(line)
                if any(x in line for x in ['BCAST BEGIN','BCAST READY','BCAST TRIAL','BCAST LAUNCHED','BCAST ACTION','BCAST RECOVERED','BCAST SCOPE','BCAST END','baseline ramp rejected','SAFETY STOP']):print(line,end='',flush=True)
                if 'BCAST READY' in line:ready.set()
                if 'BCAST LAUNCHED' in line:launched.set()
                if 'BCAST END' in line or 'BCAST setup failed' in line:ended.set()
    reader=threading.Thread(target=read_serial,daemon=True);reader.start()
    deadline=time.monotonic()+180
    while not ready.wait(.2):
        if ended.is_set() or time.monotonic()>deadline:raise RuntimeError('Baseline failed or READY missing; no test launched')
    s.write(':SINGle')
    deadline=time.monotonic()+3
    while True:
        state=s.ask(':TRIG:STAT?')
        if state=='WAIT':break
        if state=='STOP' or time.monotonic()>deadline:raise RuntimeError(f'Unexpected scope arm state {state}')
        time.sleep(.1)
    # Trigger before USB G; firmware polls G at <=20 ms intervals. This avoids
    # relying on host reception time of a buffered serial TRIAL message.
    s.write(':TFORce');port.write(b'G');port.flush()
    print('Scope forced; USB G sent',flush=True)
    deadline=time.monotonic()+10
    while not launched.wait(.1):
        if ended.is_set() or time.monotonic()>deadline:break
    time.sleep(.5)
    assert s.ask(':TRIG:STAT?')=='STOP'
    # Large read requests were checked byte-for-byte against a prior capture.
    # SCPI writes remain short, below the instrument-advertised receive size.
    s.max_recv_size=262144
    arrays={};preambles={}
    t0=time.monotonic()
    for channel in ['CHAN1','CHAN2']:
        for cmd in [f':WAV:SOUR {channel}',':WAV:MODE RAW',':WAV:FORM BYTE',':WAV:POIN 7000000']:s.write(cmd)
        preamble=s.ask(':WAV:PRE?');preambles[channel]=preamble
        points=int(preamble.split(',')[2]);assert points==7000000
        arr=np.empty(points,dtype=np.uint8)
        for start in range(1,points+1,250000):
            stop=min(points,start+249999)
            s.write(f':WAV:STAR {start}');s.write(f':WAV:STOP {stop}')
            payload=block(s.ask_raw(b':WAV:DATA?'));assert len(payload)==stop-start+1
            arr[start-1:stop]=np.frombuffer(payload,dtype=np.uint8)
            if stop%2000000==0:print(channel,stop,'points',round(time.monotonic()-t0,1),'seconds',flush=True)
        arrays[channel]=arr;np.save(p/f'transition-{channel}-raw.npy',arr)
        (p/'transition-preambles.json').write_text(json.dumps(preambles,indent=2)+'\n')
    np.savez_compressed(p/'transition-raw.npz',**arrays)
    pre={c:np.array(v.split(','),dtype=float) for c,v in preambles.items()}
    assert np.array_equal(pre['CHAN1'][4:7],pre['CHAN2'][4:7])
    v={c:(arrays[c].astype(float)-pre[c][8]-pre[c][9])*pre[c][7] for c in arrays}
    valid={c:(arrays[c]>0)&(arrays[c]<255) for c in arrays};both=valid['CHAN1']&valid['CHAN2']
    upper=v['CHAN1']-v['CHAN2']
    end_s=(len(upper)-1-pre['CHAN1'][6])*pre['CHAN1'][4]+pre['CHAN1'][5]
    trial=re.search(r'\((\d+)\).*BCAST TRIAL',''.join(lines));launch=re.search(r'\((\d+)\).*BCAST LAUNCHED',''.join(lines))
    launch_s=(int(launch[1])-int(trial[1]))/1000 if trial and launch else None
    audit=dict(lower_max_v=float(v['CHAN2'][valid['CHAN2']].max()),upper_max_v=float(upper[both].max()),
        total_max_v=float(v['CHAN1'][valid['CHAN1']].max()),clipped_samples=int((~both).sum()),
        record_end_s=float(end_s),launch_duration_s=launch_s)
    audit['accepted']=bool(audit['lower_max_v']<1.2 and audit['upper_max_v']<1.2 and audit['clipped_samples']==0 and launch_s is not None and launch_s+.05<end_s and not ended.is_set())
    (p/'scope-audit.json').write_text(json.dumps(audit,indent=2)+'\n');print('Scope audit',audit,flush=True)
    port.write(b'R' if audit['accepted'] else b'X');port.flush()
    print('Sent scope verdict', 'R' if audit['accepted'] else 'X',flush=True)
    bmp=block(s.ask_raw(b':DISP:DATA?'));(p/'transition-screen.bmp').write_bytes(bmp)
    Image.open(p/'transition-screen.bmp').save(p/'transition-screen.png')
    if not ended.wait(55):
        port.write(b'X');port.flush();raise RuntimeError('Probe did not finish after verdict')
    time.sleep(1)
finally:
    if port:
        if not ended.is_set():port.write(b'X');port.flush()
        quit_reader.set()
        if reader:reader.join(2)
        port.close()
    s.close()

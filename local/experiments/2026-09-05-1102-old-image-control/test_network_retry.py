import ast,socket,http.client,urllib.error
from pathlib import Path
s=ast.parse((Path(__file__).parent/'run_control.py').read_text())
fn=next(n for n in s.body if isinstance(n,ast.FunctionDef) and n.name=='soft_sample')
for exc in [socket.timeout('boot timeout'),ConnectionResetError('reset'),http.client.RemoteDisconnected('reboot'),urllib.error.URLError('not ready')]:
 def sample():raise exc
 ns=dict(sample=sample,urllib=urllib,http=http);exec(compile(ast.Module(body=[fn],type_ignores=[]),'retry','exec'),ns);assert ns['soft_sample']() is None
for exc in [AssertionError('wrong board'),RuntimeError('overtemperature')]:
 def sample():raise exc
 ns=dict(sample=sample,urllib=urllib,http=http);exec(compile(ast.Module(body=[fn],type_ignores=[]),'retry','exec'),ns)
 try:ns['soft_sample']();raise AssertionError('fault swallowed')
 except type(exc):pass
print('PASS: boot network disconnects/timeouts are tolerated; identity and temperature faults propagate.')

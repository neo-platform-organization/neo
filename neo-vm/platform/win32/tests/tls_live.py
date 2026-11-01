import subprocess,socket,time,threading,select,json,sys,ssl
from pathlib import Path
pins=json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
exe=str(Path('build/x86_64/Windows/neo.exe').resolve())
processes=[]; outputs=[]; errors=[]
stop=threading.Event(); corrupt=threading.Event(); corrupted=threading.Event()
server_probe=socket.socket(); server_probe.bind(('127.0.0.1',0)); server_port=server_probe.getsockname()[1];server_probe.close()
proxy_listener=socket.socket();proxy_listener.bind(('127.0.0.1',0));proxy_listener.listen();proxy_listener.settimeout(8)
proxy_port=proxy_listener.getsockname()[1]
def send(p,commands):
 p.stdin.write('\n'.join(commands)+'\n');p.stdin.flush()
def start(role,limit=0,running=True,ack=False,hb=False):
 path=Path('build/x86_64/Windows/tls-session-'+role+'.log');output=path.open('w',encoding='utf-8');outputs.append(output)
 p=subprocess.Popen([exe,'live','neo/fixtures/session-'+role+'.neo'],stdin=subprocess.PIPE,stdout=output,stderr=subprocess.STDOUT,text=True);processes.append(p)
 local=pins['server'] if role=='receive' else pins['client'];peer=pins['client'] if role=='receive' else pins['server']
 mode='listen' if role=='receive' else 'connect';port=server_port if role=='receive' else proxy_port
 send(p,[f'net-grant-tls /session {mode} 127.0.0.1 {port} {local} {peer}','set /session/require-authentication true',f'set /session/connection-limit {limit}',f'set /service/acknowledgements {str(ack).lower()}',f'set /service/heartbeats {str(hb).lower()}','set /session/heartbeat-interval 4','set /session/idle-limit 512','link /scheduler candidate /session 99','call /scheduler register','unlink /scheduler candidate','call /session start','drive /scheduler']+(['resume'] if running else []))
 return p,path
def waitfor(p,path,command,marker):
 offset=len(path.read_text(encoding='utf-8'));deadline=time.monotonic()+12
 while time.monotonic()<deadline:
  send(p,[command]);time.sleep(.02)
  content=path.read_text(encoding='utf-8')
  if marker in content[offset:]:return content
  if p.poll() is not None:break
  if errors:raise AssertionError(errors)
 raise AssertionError(path.read_text(encoding='utf-8'))
def relay():
 try:
  front=proxy_listener.accept()[0];back=socket.create_connection(('127.0.0.1',server_port),timeout=5)
  with front,back:
   front.setblocking(False);back.setblocking(False)
   pending={front:bytearray(),back:bytearray()};records=bytearray()
   while not stop.is_set():
    readable,writable,_=select.select([front,back],[s for s in [front,back] if pending[s]],[],.02)
    for s in readable:
     data=s.recv(4096)
     if not data:return
     target=back if s is front else front
     if s is front:
      records.extend(data)
      while len(records)>=5:
       length=5+int.from_bytes(records[3:5],'big')
       assert length<=65540,length
       if len(records)<length:break
       record=bytearray(records[:length]);del records[:length]
       if corrupt.is_set() and not corrupted.is_set() and record[0]==23:
        record[-1]^=1;corrupted.set()
       pending[target].extend(record)
     else:pending[target].extend(data)
    for s in writable:
     count=s.send(pending[s][:37]);del pending[s][:count]
 except Exception as exc:
  if not stop.is_set():errors.append(repr(exc))
thread=None
try:
 receiver,rlog=start('receive');waitfor(receiver,rlog,'show /session/phase','phase = "connecting"')
 thread=threading.Thread(target=relay,daemon=True);thread.start()
 sender,slog=start('send')
 waitfor(receiver,rlog,'show /service/inbox','candidate = object')
 waitfor(receiver,rlog,'show /session/peer-identity','peer-identity = "'+pins['client']+'"')
 waitfor(sender,slog,'show /session/peer-identity','peer-identity = "'+pins['server']+'"')
 send(receiver,['pause','set /service/accept-name "first"','call /service accept','resume'])
 waitfor(receiver,rlog,'show /service/revision','revision = 1')
 corrupt.set();send(sender,['pause','set /scope/value 99','call /session resync','resume'])
 waitfor(receiver,rlog,'show /session/phase','phase = "failed"')
 assert corrupted.is_set(),'proxy did not corrupt an application record'
 send(receiver,['pause','show /service/inbox','show /service/revision'])
 waitfor(receiver,rlog,'show /service/accepted/first/state/value','value = 42')
 waitfor(receiver,rlog,'show /service/revision','revision = 1')
 offset=len(rlog.read_text(encoding='utf-8'))
 waitfor(receiver,rlog,'show /service/inbox','inbox = object')
 assert 'candidate = object' not in rlog.read_text(encoding='utf-8')[offset:]
 for p in processes:send(p,['pause','call /session stop','quit'])
 for p in processes:p.wait(timeout=5)
 for path in [rlog,slog]:
  content=path.read_text(encoding='utf-8');assert 'neo:' not in content,content
 # A protocol-valid TLS client without a client certificate must not publish a peer.
 processes.clear()
 with socket.socket() as fresh:
  fresh.bind(('127.0.0.1',0));server_port=fresh.getsockname()[1]
 receiver,rlog=start('receive')
 waitfor(receiver,rlog,'show /session/phase','phase = "connecting"')
 context=ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT);context.check_hostname=False;context.verify_mode=ssl.CERT_NONE
 context.minimum_version=ssl.TLSVersion.TLSv1_2;context.maximum_version=ssl.TLSVersion.TLSv1_2
 try:
  with socket.create_connection(('127.0.0.1',server_port),timeout=3) as plain:
   with context.wrap_socket(plain,server_hostname='localhost') as attacker:
    attacker.sendall(b'not an authorized neo peer')
 except (ssl.SSLError,ConnectionError):pass
 waitfor(receiver,rlog,'show /session/phase','phase = "failed"')
 send(receiver,['pause','show /service/inbox','call /session stop','quit']);receiver.wait(timeout=5)
 assert 'candidate = object' not in rlog.read_text(encoding='utf-8'),rlog.read_text(encoding='utf-8')
 # A silent TCP peer cannot retain a TLS attempt beyond the image's poll budget.
 processes.clear()
 with socket.socket() as fresh:
  fresh.bind(('127.0.0.1',0));server_port=fresh.getsockname()[1]
 receiver,rlog=start('receive',limit=3,running=False)
 send(receiver,['call /session tick'])
 waitfor(receiver,rlog,'show /session/phase','phase = "connecting"')
 with socket.create_connection(('127.0.0.1',server_port),timeout=3) as silent:
  send(receiver,['call /session tick','call /session tick'])
  waitfor(receiver,rlog,'show /session/phase','phase = "connecting"')
  send(receiver,['call /session tick'])
  waitfor(receiver,rlog,'show /session/phase','phase = "failed"')
  assert silent.recv(1)==b'', 'expired TLS socket was not closed'
 offset=len(rlog.read_text(encoding='utf-8'))
 waitfor(receiver,rlog,'show /session','connection-remaining = 0')
 assert 'peer = stream' not in rlog.read_text(encoding='utf-8')[offset:]
 send(receiver,['call /session retry']+['call /session tick']*4)
 waitfor(receiver,rlog,'show /session/phase','phase = "connecting"')
 waitfor(receiver,rlog,'show /session/connection-remaining','connection-remaining = 3')
 send(receiver,['call /session stop','quit']);receiver.wait(timeout=5)
 # Finish a pending publication through the image, without implicitly accepting it.
 processes.clear()
 with socket.socket() as fresh:
  fresh.bind(('127.0.0.1',0));server_port=fresh.getsockname()[1]
 proxy_port=server_port
 receiver,rlog=start('receive')
 waitfor(receiver,rlog,'show /session/phase','phase = "connecting"')
 sender,slog=start('send')
 waitfor(receiver,rlog,'show /service/inbox','candidate = object')
 send(receiver,['pause','set /service/accept-name "first"','call /service accept','resume'])
 waitfor(receiver,rlog,'show /service/revision','revision = 1')
 send(sender,['pause','set /scope/value 99','call /session resync','call /session tick'])
 waitfor(sender,slog,'show /service/pending','pending = true')
 send(sender,['call /session finish']+['call /session tick']*20)
 waitfor(sender,slog,'show /session/phase','phase = "stopped"')
 waitfor(receiver,rlog,'show /service/inbox/candidate/state/value','value = 99')
 waitfor(receiver,rlog,'show /service/revision','revision = 1')
 # Explicitly chosen finish budget can abort a close; the candidate survives.
 send(receiver,['pause','set /session/finish-limit 1','call /session finish','call /session tick'])
 waitfor(receiver,rlog,'show /session/phase','phase = "failed"')
 waitfor(receiver,rlog,'show /service/inbox/candidate/state/value','value = 99')
 for p in processes:send(p,['call /session stop','quit'])
 for p in processes:p.wait(timeout=5)
 # A pending candidate defers boundary EOF; only an explicit decision releases it.
 processes.clear()
 with socket.socket() as fresh:
  fresh.bind(('127.0.0.1',0));server_port=fresh.getsockname()[1]
 proxy_port=server_port
 receiver,rlog=start('receive')
 waitfor(receiver,rlog,'show /session/phase','phase = "connecting"')
 sender,slog=start('send')
 waitfor(receiver,rlog,'show /service/inbox/candidate/state/value','value = 42')
 send(sender,['pause','call /session finish']+['call /session tick']*20)
 waitfor(sender,slog,'show /session/phase','phase = "stopped"')
 waitfor(receiver,rlog,'show /session/phase','phase = "active"')
 waitfor(receiver,rlog,'show /session/remote-closed','remote-closed = false')
 waitfor(receiver,rlog,'show /service/revision','revision = 0')
 send(receiver,['pause','set /session/automatic-retry true','set /service/accept-name "first"','call /service accept','resume'])
 waitfor(receiver,rlog,'show /session/phase','phase = "stopped"')
 waitfor(receiver,rlog,'show /session/remote-closed','remote-closed = true')
 waitfor(receiver,rlog,'show /service/revision','revision = 1')
 waitfor(receiver,rlog,'show /service/accepted/first/state/value','value = 42')
 for p in processes:send(p,['pause','call /session stop','quit'])
 for p in processes:p.wait(timeout=5)
 # Opt-in image replies distinguish receipt, acceptance and rejection.
 processes.clear()
 with socket.socket() as fresh:
  fresh.bind(('127.0.0.1',0));server_port=fresh.getsockname()[1]
 proxy_port=server_port
 receiver,rlog=start('receive',ack=True)
 waitfor(receiver,rlog,'show /session/phase','phase = "connecting"')
 sender,slog=start('send',ack=True)
 waitfor(sender,slog,'show /service/received-revision','received-revision = 1')
 waitfor(sender,slog,'show /service/accepted-revision','accepted-revision = 0')
 waitfor(sender,slog,'show /service/awaiting-revision','awaiting-revision = 1')
 waitfor(receiver,rlog,'show /service/revision','revision = 0')
 send(receiver,['pause','set /service/accept-name "first"','call /service accept','resume'])
 waitfor(sender,slog,'show /service/accepted-revision','accepted-revision = 1')
 send(sender,['pause','set /scope/value 43','call /session resync','resume'])
 waitfor(sender,slog,'show /service/received-revision','received-revision = 2')
 send(receiver,['pause','call /service reject','resume'])
 waitfor(sender,slog,'show /service/rejected-revision','rejected-revision = 2')
 waitfor(sender,slog,'show /service/accepted-revision','accepted-revision = 1')
 send(sender,['pause','call /session resync','resume'])
 waitfor(sender,slog,'show /service/received-revision','received-revision = 3')
 # Candidate3 survives a new connection, but its later decision is not a reply for4.
 for p in [sender,receiver]:send(p,['pause','call /session stop'])
 waitfor(receiver,rlog,'show /session/phase','phase = "stopped"')
 send(receiver,['call /session start','resume']);send(sender,['call /session start','resume'])
 waitfor(sender,slog,'show /session/phase','phase = "active"')
 waitfor(sender,slog,'show /service/awaiting-revision','awaiting-revision = 4')
 send(receiver,['pause','set /service/accept-name "old"','call /service accept','resume'])
 waitfor(sender,slog,'show /service/received-revision','received-revision = 4')
 waitfor(sender,slog,'show /service/accepted-revision','accepted-revision = 1')
 send(receiver,['pause','set /service/accept-name "fourth"','call /service accept','resume'])
 waitfor(sender,slog,'show /service/accepted-revision','accepted-revision = 4')
 # Correlation failures preserve the last valid observations and retain inert reply.
 send(sender,['pause','call /session resync','resume'])
 waitfor(sender,slog,'show /service/received-revision','received-revision = 5')
 send(receiver,['pause','set /service/candidate-revision 999','call /service reject','resume'])
 waitfor(sender,slog,'show /session/phase','phase = "failed"')
 waitfor(sender,slog,'show /service/awaiting-revision','awaiting-revision = 5')
 waitfor(sender,slog,'show /service/accepted-revision','accepted-revision = 4')
 waitfor(sender,slog,'show /service/rejected-revision','rejected-revision = 2')
 waitfor(sender,slog,'show /service/reply/revision','revision = 999')
 for p in processes:send(p,['pause','call /session stop','quit'])
 for p in processes:p.wait(timeout=5)
 # Manual balanced turns distinguish elapsed time from the image work budget.
 for silent_role in ['receive','send','bad-alive']:
  processes.clear()
  with socket.socket() as fresh:
   fresh.bind(('127.0.0.1',0));server_port=fresh.getsockname()[1]
  proxy_port=server_port
  receiver,rlog=start('receive',running=False,ack=True,hb=True)
  send(receiver,['call /session tick'])
  waitfor(receiver,rlog,'show /session/phase','phase = "connecting"')
  sender,slog=start('send',running=False,ack=True,hb=True)
  for turn in range(180):
   send(sender,['call /session tick']);send(receiver,['call /session tick']);time.sleep(.002)
  waitfor(sender,slog,'show /session/phase','phase = "active"')
  waitfor(receiver,rlog,'show /session/phase','phase = "active"')
  waitfor(sender,slog,'show /service/received-revision','received-revision = 1')
  waitfor(sender,slog,'show /service/accepted-revision','accepted-revision = 0')
  waitfor(receiver,rlog,'show /service/inbox/candidate/state/value','value = 42')
  content=waitfor(receiver,rlog,'show /service/last-probe','last-probe = ')
  import re
  assert int(re.findall(r'last-probe = (\d+)',content)[-1])>=3,content
  if silent_role=='receive':
   # Keep candidate1, reconnect and defer new snapshot2 while probes continue.
   for p in processes:send(p,['call /session stop'])
   waitfor(receiver,rlog,'show /session/phase','phase = "stopped"')
   send(receiver,['call /session start','call /session tick'])
   waitfor(receiver,rlog,'show /session/phase','phase = "connecting"')
   send(sender,['call /session start'])
   for turn in range(180):
    send(sender,['call /session tick']);send(receiver,['call /session tick']);time.sleep(.002)
   waitfor(receiver,rlog,'show /session/phase','phase = "active"')
   waitfor(sender,slog,'show /session/phase','phase = "active"')
   waitfor(receiver,rlog,'show /service/inbox/candidate/revision','revision = 1')
   waitfor(receiver,rlog,'show /service/deferred/revision','revision = 2')
   waitfor(sender,slog,'show /service/awaiting-revision','awaiting-revision = 2')
   send(receiver,['call /service reject'])
   for turn in range(100):
    send(sender,['call /session tick']);send(receiver,['call /session tick']);time.sleep(.002)
   waitfor(sender,slog,'show /service/received-revision','received-revision = 2')
   waitfor(receiver,rlog,'show /service/inbox/candidate/revision','revision = 2')
  if silent_role=='bad-alive':
   # Drain earlier probes, then corrupt the peer's echo without accepting state.
   send(sender,['set /session/heartbeat-remaining 10000'])
   for turn in range(30):
    send(sender,['call /session tick']);send(receiver,['call /session tick']);time.sleep(.002)
   waitfor(sender,slog,'show /service/awaiting-probe','awaiting-probe = 0')
   send(sender,['call /service probe']+['call /session tick']*3)
   waitfor(receiver,rlog,'call /session tick\nshow /service/alive-pending','alive-pending = true')
   send(receiver,['set /service/last-probe 999']+['call /session tick']*2)
   waitfor(sender,slog,'call /session tick\nshow /session/phase','phase = "failed"')
   waitfor(sender,slog,'show /session/liveness-expired','liveness-expired = false')
   waitfor(receiver,rlog,'show /service/inbox/candidate/state/value','value = 42')
   waitfor(sender,slog,'show /service/accepted-revision','accepted-revision = 0')
   for p in processes:send(p,['call /session stop','quit'])
   for p in processes:p.wait(timeout=5)
   continue
  # Pausing the remote image while leaving TCP open must exhaust the local budget.
  send(sender,['set /session/heartbeat-remaining 10000'])
  for turn in range(30):
   send(sender,['call /session tick']);send(receiver,['call /session tick']);time.sleep(.002)
  local,llog=(sender,slog) if silent_role=='receive' else (receiver,rlog)
  send(local,['set /session/idle-limit 3','set /session/idle-remaining 3','call /session tick','call /session tick'])
  waitfor(local,llog,'show /session/phase','phase = "active"')
  waitfor(local,llog,'show /session/idle-remaining','idle-remaining = 1')
  send(local,['call /session tick'])
  waitfor(local,llog,'show /session/phase','phase = "failed"')
  waitfor(local,llog,'show /session/liveness-expired','liveness-expired = true')
  waitfor(receiver,rlog,'show /service/inbox/candidate/state/value','value = 42')
  waitfor(receiver,rlog,'show /service/revision','revision = 0')
  waitfor(sender,slog,'show /service/accepted-revision','accepted-revision = 0')
  for p in processes:send(p,['call /session stop','quit'])
  for p in processes:p.wait(timeout=5)
 print('PASS live mutual TLS: fragmented records, verified identities, tampering rejected without state change, missing client certificate denied, silent handshake expires and retry rearms, image finish drains pending data without acceptance, finish budget abort preserves candidate, explicit candidate decision releases clean EOF without automatic retry; opt-in receipt/accept/reject replies, reconnect correlation and malformed reply denial; correlated heartbeats preserve pending candidates and idle budgets detect paused peers in either direction; deferred reconnect snapshots and mismatched heartbeat echoes checked')
finally:
 stop.set();proxy_listener.close()
 for p in processes:
  if p.poll() is None:p.kill();p.wait()
 if thread is not None:thread.join(timeout=2)
 for output in outputs:output.close()

import {spawn} from 'node:child_process';
import {connect} from 'node:net';
import {copyFileSync, mkdtempSync, readFileSync, rmSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join, resolve, dirname} from 'node:path';
import assert from 'node:assert/strict';
const out=resolve(process.argv[2]), mode=process.argv[3] ?? 'surface';
const tempRoot=resolve(tmpdir()), temporary=mkdtempSync(join(tempRoot,'neo-boot-'));
const vars=join(temporary,'vars.fd'), socket=join(temporary,'qmp.sock');
copyFileSync('/usr/share/OVMF/OVMF_VARS_4M.fd',vars);
const child=spawn('qemu-system-x86_64',['-machine','q35','-m','512','-accel','tcg',
    '-drive','if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd',
    '-drive',`if=pflash,format=raw,unit=1,file=${vars}`,'-cdrom',join(out,'neo.iso'),
    '-serial','stdio','-display','none','-monitor','none','-qmp',`unix:${socket},server=on,wait=off`,'-no-reboot'],
    {stdio:['pipe','pipe','pipe']});
let serial='', errors='', client;
child.stdout.on('data',data=>{serial+=data;}); child.stderr.on('data',data=>{errors+=data;});
let timer;
const deadline=new Promise((_,reject)=>{timer=setTimeout(()=>reject(new Error(`Boot timeout\n${serial}\n${errors}`)),45000);});
async function waitFor(test) {
    return Promise.race([deadline,new Promise((resolveWait,reject)=>{
        const interval=setInterval(()=>{if(test()) {clearInterval(interval);resolveWait();}
            else if(child.exitCode!==null) {clearInterval(interval);reject(new Error(`QEMU exited ${child.exitCode}\n${serial}\n${errors}`));}},10);
        deadline.catch(()=>clearInterval(interval));
    })]);
}
async function qmp() {
    await waitFor(()=>{try {return readFileSync('/proc/net/unix','utf8').includes(socket);}catch{return false;}});
    client=connect(socket);let buffer='', nextId=0;const pending=new Map();
    client.on('error',error=>{for(const entry of pending.values()) {entry.reject(error);}});
    client.on('data',data=>{buffer+=data;let boundary;while((boundary=buffer.indexOf('\n'))>=0) {
        const line=buffer.slice(0,boundary);buffer=buffer.slice(boundary+1);if(!line.trim())continue;
        const message=JSON.parse(line);if(message.id && pending.has(message.id)) {
            const entry=pending.get(message.id);pending.delete(message.id);
            if(message.error)entry.reject(new Error(JSON.stringify(message.error)));else entry.resolve(message.return);
        }
    }});
    const command=(execute,args={})=>Promise.race([deadline,new Promise((resolveCommand,reject)=>{
        const id=++nextId;pending.set(id,{resolve:resolveCommand,reject});client.write(JSON.stringify({execute,arguments:args,id})+'\n');})]);
    await command('qmp_capabilities');return command;
}
try {
    const command=await qmp();
    await waitFor(()=>serial.includes('NEO x86_64 ready'));
    assert.ok(serial.includes('firmware exited, trusted code only'),serial);
    if(mode==='cli') {
        child.stdin.write('revision\nsend instrument sample 21\nsend instrument reading\nquit\n');
        await waitFor(()=>serial.includes('NEO image stopped 0'));
        assert.match(serial,/revision 1/);assert.equal((serial.match(/result 42/g)??[]).length,2,serial);
        console.log('PASS x86_64: boot services exited; standalone CLI and JIT execute the canonical image');
    } else {
        await waitFor(()=>serial.includes('NEO frame 53 53 53'));
        await command('input-send-event',{events:[{type:'btn',data:{down:true,button:'left'}}]});
        await waitFor(()=>serial.includes('NEO frame 145 145 145'));
        await command('input-send-event',{events:[{type:'btn',data:{down:false,button:'left'}}]});
        const screenshot=join(temporary,'screen.ppm');await command('screendump',{filename:screenshot});
        const bytes=readFileSync(screenshot), header=bytes.toString('ascii',0,100).match(/^P6\n(\d+) (\d+)\n255\n/);
        assert.ok(header,'Expected QEMU PPM screenshot');
        const offset=header[0].length;assert.deepEqual([...bytes.subarray(offset,offset+3)],[145,145,145]);
        const ticks=[...serial.matchAll(/ticks (\d+)/g)].map(match=>Number(match[1]));assert.ok(ticks.some(t=>t>0),serial);
        console.log('PASS x86_64: firmware exit, timer interrupts, PS/2 mouse, JIT, and image-owned framebuffer match hosted pixels');
    }
    await command('quit');
} finally {
    clearTimeout(timer);if(client)client.destroy();child.kill('SIGKILL');
    if(child.exitCode===null)await new Promise(resolveExit=>child.once('exit',resolveExit));
    assert.equal(dirname(temporary),tempRoot);assert.ok(temporary.startsWith(join(tempRoot,'neo-boot-')));
    rmSync(temporary,{recursive:true,force:true});
}

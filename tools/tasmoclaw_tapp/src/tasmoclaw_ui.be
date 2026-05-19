import webserver

class TasmoClawUI
  def chat_page()
    webserver.content_start('TasmoClaw')
    webserver.content_send_style()
    webserver.content_send('<style>.tc-box{background:#111;color:#ddd;padding:8px;border-radius:6px;min-height:240px;white-space:pre-wrap;overflow:auto}.tc-badge{padding:2px 6px;border-radius:10px;background:#333;color:#eee;font-size:11px;margin-right:4px}</style>')
    webserver.content_send('<h2>TasmoClaw</h2><div><span class="tc-badge" id="m"></span><span class="tc-badge" id="e"></span><span class="tc-badge" id="h"></span><span class="tc-badge" id="w"></span></div>')
    webserver.content_send('<p><a href="/tasmoclaw/config"><button>Config</button></a> <button id="clear">Clear</button></p><div id="chat" class="tc-box"></div><p><textarea id="msg" style="width:100%;height:90px"></textarea></p><p><button id="send">Send</button></p><div id="pending" style="display:none"></div>')
    webserver.content_send('<script>const j=(u,o)=>fetch(u,o).then(r=>r.json());const chat=document.getElementById("chat");function add(r,t){chat.textContent+="["+r+"] "+t+"\\n";chat.scrollTop=chat.scrollHeight;}function load(){j("/tasmoclaw/api/history").then(x=>{chat.textContent="";(x.history||[]).forEach(m=>add(m.role,m.content));});j("/tasmoclaw/api/status").then(s=>{if(s.ok){m.textContent=s.model;e.textContent=s.api_url;h.textContent="heap:"+s.heap;w.textContent="wifi:"+s.wifi;}});j("/tasmoclaw/api/pending").then(showPending);}function showPending(p){const d=document.getElementById("pending");if(!p.pending){d.style.display="none";return;}d.style.display="block";d.innerHTML="<h4>Pending approval</h4><pre>"+JSON.stringify(p.pending,null,2)+"</pre><button id=a>Approve</button> <button id=r>Reject</button>";document.getElementById("a").onclick=()=>j("/tasmoclaw/api/approve",{method:"POST"}).then(x=>{add("tool",JSON.stringify(x));load();});document.getElementById("r").onclick=()=>j("/tasmoclaw/api/reject",{method:"POST"}).then(_=>load());}document.getElementById("send").onclick=()=>{let v=msg.value.trim();if(!v)return;add("user",v);send.disabled=true;j("/tasmoclaw/api/chat",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify({message:v})}).then(x=>{if(x.content)add("assistant",x.content);if(x.error)add("error",x.error);showPending(x);}).finally(()=>send.disabled=false);};document.getElementById("clear").onclick=()=>j("/tasmoclaw/api/clear",{method:"POST"}).then(_=>{chat.textContent="";});load();</script>')
    webserver.content_stop()
  end
end

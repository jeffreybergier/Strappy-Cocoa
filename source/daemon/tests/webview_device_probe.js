(function(){try{
var all=messageRows().concat(toolSourceRows()),html='',refs=[],i;
for(i=0;i<all.length;i++){html+=all[i]._strappyCommittedHTML;refs.push(all[i]);}
if(!all.length)return 'no fixture rows';
syncMessages(html);
for(i=0;i<refs.length;i++)if(byId(rowId(refs[i]))!==refs[i])return 'initial replay replaced '+rowId(refs[i]);
var first=refs[0];first.setAttribute('data-probe-expanded','yes');
syncMessages(html);
if(byId(rowId(first))!==first||first.getAttribute('data-probe-expanded')!=='yes')return 'replay lost UI state';
var newHTML='<div class="row user" id="probe-new" data-message-key="probe-new"><div class="bubble">New prompt</div></div>';
syncMessages(html+newHTML);
if(!byId('probe-new'))return 'new row missing';
var newRow=byId('probe-new');
syncMessages(html+newHTML);
if(byId('probe-new')!==newRow)return 'new row replay replaced';
var changed=newHTML.replace('New prompt','Changed prompt');
syncMessages(html+changed);
if(byId('probe-new')===newRow||byId('probe-new').innerHTML.indexOf('Changed prompt')<0)return 'existing row not updated';
var late='<div class="row user" id="probe-late" data-message-key="probe-late"><div class="bubble">Inserted first</div></div>';
syncMessages(html+late+changed);
if(byId('probe-late').nextSibling!==byId('probe-new'))return 'late insertion ordering';
syncMessages(html);
if(byId('probe-new')||byId('probe-late'))return 'deleted rows retained';
for(i=0;i<refs.length;i++)if(byId(rowId(refs[i]))!==refs[i])return 'updates replaced history';
return 'PASS';
}catch(e){return String(e);}})()

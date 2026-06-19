#include "strappy_webview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct strappy_webview_buffer {
  char *data;
  size_t length;
  size_t capacity;
} strappy_webview_buffer;

static void strappy_webview_buffer_init(strappy_webview_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }

  buffer->data = NULL;
  buffer->length = 0U;
  buffer->capacity = 0U;
}

static void strappy_webview_buffer_destroy(strappy_webview_buffer *buffer)
{
  if (buffer == NULL) {
    return;
  }

  free(buffer->data);
  strappy_webview_buffer_init(buffer);
}

static int strappy_webview_buffer_reserve(strappy_webview_buffer *buffer,
                                          size_t extra)
{
  char *next_data;
  size_t needed;
  size_t next_capacity;

  if (buffer == NULL) {
    return 0;
  }
  if (buffer->length > (((size_t)-1) - extra - 1U)) {
    return 0;
  }

  needed = buffer->length + extra + 1U;
  if (needed <= buffer->capacity) {
    return 1;
  }

  next_capacity = (buffer->capacity > 0U) ? buffer->capacity : 256U;
  while (next_capacity < needed) {
    if (next_capacity > (((size_t)-1) / 2U)) {
      next_capacity = needed;
      break;
    }
    next_capacity *= 2U;
  }

  next_data = (char *)realloc(buffer->data, next_capacity);
  if (next_data == NULL) {
    return 0;
  }

  buffer->data = next_data;
  buffer->capacity = next_capacity;
  return 1;
}

static int strappy_webview_buffer_append(strappy_webview_buffer *buffer,
                                         const char *data,
                                         size_t length)
{
  if ((buffer == NULL) || ((data == NULL) && (length > 0U))) {
    return 0;
  }
  if (!strappy_webview_buffer_reserve(buffer, length)) {
    return 0;
  }

  if (length > 0U) {
    memcpy(buffer->data + buffer->length, data, length);
  }
  buffer->length += length;
  buffer->data[buffer->length] = '\0';
  return 1;
}

static int strappy_webview_buffer_append_cstring(strappy_webview_buffer *buffer,
                                                 const char *data)
{
  if (data == NULL) {
    return 1;
  }

  return strappy_webview_buffer_append(buffer, data, strlen(data));
}

static int strappy_webview_buffer_append_char(strappy_webview_buffer *buffer,
                                              char character)
{
  return strappy_webview_buffer_append(buffer, &character, 1U);
}

static char *strappy_webview_buffer_finish(strappy_webview_buffer *buffer)
{
  char *result;

  if (buffer == NULL) {
    return NULL;
  }

  if (buffer->data == NULL) {
    if (!strappy_webview_buffer_append_cstring(buffer, "")) {
      return NULL;
    }
  }

  result = buffer->data;
  buffer->data = NULL;
  buffer->length = 0U;
  buffer->capacity = 0U;
  return result;
}

static int strappy_webview_append_html_escaped(strappy_webview_buffer *buffer,
                                               const char *text)
{
  const unsigned char *cursor;

  if (text == NULL) {
    return 1;
  }

  cursor = (const unsigned char *)text;
  while (*cursor != '\0') {
    if (*cursor == '&') {
      if (!strappy_webview_buffer_append_cstring(buffer, "&amp;")) {
        return 0;
      }
    } else if (*cursor == '<') {
      if (!strappy_webview_buffer_append_cstring(buffer, "&lt;")) {
        return 0;
      }
    } else if (*cursor == '>') {
      if (!strappy_webview_buffer_append_cstring(buffer, "&gt;")) {
        return 0;
      }
    } else if (*cursor == '"') {
      if (!strappy_webview_buffer_append_cstring(buffer, "&quot;")) {
        return 0;
      }
    } else if (!strappy_webview_buffer_append_char(buffer, (char)*cursor)) {
      return 0;
    }
    cursor++;
  }

  return 1;
}

static int strappy_webview_append_js_string(strappy_webview_buffer *buffer,
                                            const char *text)
{
  const unsigned char *cursor;
  char escaped[8];

  if (!strappy_webview_buffer_append_char(buffer, '\'')) {
    return 0;
  }

  if (text != NULL) {
    cursor = (const unsigned char *)text;
    while (*cursor != '\0') {
      if (*cursor == '\\') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\\\")) {
          return 0;
        }
      } else if (*cursor == '\'') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\'")) {
          return 0;
        }
      } else if (*cursor == '\n') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\n")) {
          return 0;
        }
      } else if (*cursor == '\r') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\r")) {
          return 0;
        }
      } else if (*cursor == '\t') {
        if (!strappy_webview_buffer_append_cstring(buffer, "\\t")) {
          return 0;
        }
      } else if (*cursor < 32U) {
        snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned int)*cursor);
        if (!strappy_webview_buffer_append_cstring(buffer, escaped)) {
          return 0;
        }
      } else if (!strappy_webview_buffer_append_char(buffer, (char)*cursor)) {
        return 0;
      }
      cursor++;
    }
  }

  return strappy_webview_buffer_append_char(buffer, '\'');
}

static int strappy_webview_append_chunks(strappy_webview_buffer *buffer,
                                         const char * const *chunks)
{
  size_t index;

  if ((buffer == NULL) || (chunks == NULL)) {
    return 0;
  }

  for (index = 0U; chunks[index] != NULL; index++) {
    if (!strappy_webview_buffer_append_cstring(buffer, chunks[index])) {
      return 0;
    }
  }

  return 1;
}

static const char *strappy_webview_string_or_empty(const char *value)
{
  return (value != NULL) ? value : "";
}

static const char *strappy_webview_agent_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->agent != NULL) && (labels->agent[0] != '\0')) {
    return labels->agent;
  }
  return "Agent";
}

static const char *strappy_webview_you_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->you != NULL) && (labels->you[0] != '\0')) {
    return labels->you;
  }
  return "You";
}

static const char *strappy_webview_thinking_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) &&
      (labels->thinking != NULL) &&
      (labels->thinking[0] != '\0')) {
    return labels->thinking;
  }
  return "Thinking";
}

static const char *strappy_webview_request_metadata_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) &&
      (labels->request_metadata != NULL) &&
      (labels->request_metadata[0] != '\0')) {
    return labels->request_metadata;
  }
  return "Request Metadata";
}

static const char *strappy_webview_retry_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) && (labels->retry != NULL) && (labels->retry[0] != '\0')) {
    return labels->retry;
  }
  return "Retry";
}

static const char *strappy_webview_tool_call_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) &&
      (labels->tool_call != NULL) &&
      (labels->tool_call[0] != '\0')) {
    return labels->tool_call;
  }
  return "Tool Call";
}

static const char *strappy_webview_tool_result_label(
  const strappy_webview_labels *labels)
{
  if ((labels != NULL) &&
      (labels->tool_result != NULL) &&
      (labels->tool_result[0] != '\0')) {
    return labels->tool_result;
  }
  return "Tool Result";
}

static int strappy_webview_is_assistant_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "assistant") == 0);
}

static int strappy_webview_is_tool_call_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "tool_call") == 0);
}

static int strappy_webview_is_tool_result_role(const char *role)
{
  return (role != NULL) && (strcmp(role, "tool") == 0);
}

static const char *strappy_webview_role_label(
  const char *role,
  const strappy_webview_labels *labels)
{
  if (strappy_webview_is_assistant_role(role)) {
    return strappy_webview_agent_label(labels);
  }
  if (strappy_webview_is_tool_call_role(role)) {
    return strappy_webview_tool_call_label(labels);
  }
  if (strappy_webview_is_tool_result_role(role)) {
    return strappy_webview_tool_result_label(labels);
  }
  return strappy_webview_you_label(labels);
}

static int strappy_webview_append_element_id(strappy_webview_buffer *buffer,
                                             const strappy_webview_message *message)
{
  char identifier[64];

  if ((message != NULL) &&
      (message->element_id != NULL) &&
      (message->element_id[0] != '\0')) {
    return strappy_webview_append_html_escaped(buffer, message->element_id);
  }

  if ((message != NULL) && (message->message_id > 0LL)) {
    snprintf(identifier, sizeof(identifier), "saved-%lld", message->message_id);
  } else {
    snprintf(identifier, sizeof(identifier), "saved-0");
  }

  return strappy_webview_append_html_escaped(buffer, identifier);
}

static int strappy_webview_append_reasoning_html(
  strappy_webview_buffer *buffer,
  const char *reasoning,
  int render_when_empty,
  const strappy_webview_labels *labels)
{
  if (((reasoning == NULL) || (reasoning[0] == '\0')) && !render_when_empty) {
    return 1;
  }

  if (!strappy_webview_buffer_append_cstring(buffer, "<div class=\"reasoning\"")) {
    return 0;
  }
  if ((reasoning == NULL) || (reasoning[0] == '\0')) {
    if (!strappy_webview_buffer_append_cstring(buffer, " style=\"display:none\"")) {
      return 0;
    }
  }
  if (!strappy_webview_buffer_append_cstring(buffer,
                                            "><div class=\"reasoning-label\">") ||
      !strappy_webview_append_html_escaped(buffer,
                                           strappy_webview_thinking_label(labels)) ||
      !strappy_webview_buffer_append_cstring(buffer,
                                            "</div><div class=\"reasoning-body\">") ||
      !strappy_webview_append_html_escaped(buffer, reasoning) ||
      !strappy_webview_buffer_append_cstring(buffer, "</div></div>")) {
    return 0;
  }

  return 1;
}

static int strappy_webview_append_metadata_html(
  strappy_webview_buffer *buffer,
  const char *role,
  const char *metadata_json,
  const strappy_webview_labels *labels)
{
  if (!strappy_webview_is_assistant_role(role) ||
      (metadata_json == NULL) ||
      (metadata_json[0] == '\0')) {
    return 1;
  }

  return strappy_webview_buffer_append_cstring(
           buffer,
           "<div class=\"request-metadata\" data-metadata=\"") &&
         strappy_webview_append_html_escaped(buffer, metadata_json) &&
         strappy_webview_buffer_append_cstring(
           buffer,
           "\"><div class=\"request-metadata-title\">") &&
         strappy_webview_append_html_escaped(
           buffer,
           strappy_webview_request_metadata_label(labels)) &&
         strappy_webview_buffer_append_cstring(
           buffer,
           "</div><div class=\"request-metadata-body\"></div></div>");
}

static int strappy_webview_append_styles(strappy_webview_buffer *buffer)
{
  static const char * const chunks[] = {
    "<style>",
    "html,body{margin:0;padding:0;background:#f4f4f4;color:#222;",
    "font:13px Helvetica,Arial,sans-serif;}",
    ".page{padding:18px 12px;}",
    ".empty{margin:90px auto 0;max-width:520px;color:#777;",
    "text-align:center;line-height:1.45;}",
    "#messages{max-width:860px;margin:0 auto;}",
    ".load-more{display:block;margin:0 auto 14px;padding:7px 10px;",
    "text-align:center;color:#2468a8;text-decoration:none;}",
    ".row{margin:0 0 16px;clear:both;}",
    ".row:last-child{margin-bottom:0;}",
    ".role{font-size:11px;font-weight:bold;color:#666;",
    "text-transform:uppercase;margin:0 0 5px 2px;}",
    ".bubble{display:block;max-width:72%;border:1px solid #d8d8d8;",
    "background:#fff;padding:12px 14px;line-height:1.45;",
    "white-space:normal;word-wrap:break-word;}",
    ".assistant .bubble{background:#fcfcfc;}",
    ".tool_call .role,.tool .role{color:#4d6478;}",
    ".tool_call .bubble,.tool .bubble{max-width:100%;box-sizing:border-box;",
    "font:12px Menlo,Consolas,Monaco,monospace;white-space:pre-wrap;}",
    ".tool_call .bubble{background:#fffdf2;border-color:#ded6a8;}",
    ".tool .bubble{background:#f7fbff;border-color:#cbd7e2;}",
    ".bubble p,.reasoning-body p{margin:0 0 9px;}",
    ".bubble h1,.bubble h2,.bubble h3,.bubble h4,.bubble h5,.bubble h6,",
    ".reasoning-body h1,.reasoning-body h2,.reasoning-body h3,",
    ".reasoning-body h4,.reasoning-body h5,.reasoning-body h6{",
    "font-size:14px;line-height:1.3;margin:0 0 8px;font-weight:bold;}",
    ".bubble ul,.bubble ol,.reasoning-body ul,.reasoning-body ol{",
    "margin:0 0 9px 20px;padding:0;}",
    ".bubble li,.reasoning-body li{margin:0 0 3px;}",
    ".bubble blockquote,.reasoning-body blockquote{",
    "margin:0 0 9px;padding:0 0 0 10px;",
    "border-left:3px solid #d8d8d8;color:#555;}",
    ".bubble pre,.reasoning-body pre{margin:0 0 9px;padding:8px;",
    "border:1px solid #ddd;",
    "background:#f2f2f2;overflow:auto;white-space:pre-wrap;}",
    ".bubble code,.reasoning-body code{font:12px Monaco,Consolas,monospace;",
    "background:#f2f2f2;",
    "padding:1px 3px;}",
    ".bubble pre code,.reasoning-body pre code{background:transparent;padding:0;}",
    ".bubble .table-wrap,.reasoning-body .table-wrap{",
    "margin:0 0 9px;overflow:auto;}",
    ".bubble table,.reasoning-body table{border-collapse:collapse;",
    "min-width:100%;font-size:12px;line-height:1.35;}",
    ".bubble th,.bubble td,.reasoning-body th,.reasoning-body td{",
    "border:1px solid #d8d8d8;padding:5px 7px;text-align:left;}",
    ".bubble th,.reasoning-body th{background:#f2f2f2;font-weight:bold;}",
    ".bubble a,.reasoning-body a{color:#2468a8;text-decoration:none;}",
    ".bubble p:last-child,.bubble ul:last-child,.bubble ol:last-child,",
    ".bubble pre:last-child,.bubble blockquote:last-child,",
    ".bubble .table-wrap:last-child,",
    ".reasoning-body p:last-child,.reasoning-body ul:last-child,",
    ".reasoning-body ol:last-child,.reasoning-body pre:last-child,",
    ".reasoning-body blockquote:last-child,",
    ".reasoning-body .table-wrap:last-child{margin-bottom:0;}",
    ".reasoning{max-width:72%;border:1px solid #ddd;background:#fffdf2;",
    "color:#4f4a36;padding:10px 12px;margin:0 0 7px;line-height:1.4;",
    "white-space:pre-wrap;word-wrap:break-word;}",
    ".reasoning-label{font-size:11px;font-weight:bold;text-transform:uppercase;",
    "color:#7a7046;margin:0 0 5px;}",
    ".reasoning-body{white-space:normal;}",
    ".request-metadata{max-width:72%;box-sizing:border-box;",
    "border:1px solid #cbd7e2;background:#f7fbff;color:#2e3f4f;",
    "padding:9px 11px;margin:7px 0 0;line-height:1.35;",
    "white-space:pre-wrap;word-wrap:break-word;}",
    ".request-metadata-title{font-size:11px;font-weight:bold;",
    "text-transform:uppercase;color:#4d6478;margin:0 0 5px;}",
    ".request-metadata-body{font:11px Menlo,Consolas,Monaco,monospace;",
    "white-space:pre-wrap;}",
    ".user .role,.user .meta{text-align:right;}",
    ".user .bubble{margin-left:auto;background:#eef5ff;border-color:#c8d8ef;}",
    ".meta{font-size:11px;color:#777;margin-top:6px;}",
    ".state-pending .bubble{opacity:.72;}",
    ".state-pending .status{color:#777;}",
    ".state-error .bubble{border-color:#d99;background:#fff7f7;}",
    ".state-error .status{color:#a22;}",
    ".status a{color:#2468a8;text-decoration:none;}",
    "</style>",
    NULL
  };

  return strappy_webview_append_chunks(buffer, chunks);
}

static int strappy_webview_append_scripts(strappy_webview_buffer *buffer)
{
  static const char * const chunks[] = {
    "<script>",
    "function byId(i){return document.getElementById(i);}",
    "function hasClass(e,n){return e&&(' '+e.className+' ').indexOf(' '+n+' ')>=0;}",
    "function firstByClass(root,name){var n=root.getElementsByTagName('*');",
    "for(var i=0;i<n.length;i++){if(hasClass(n[i],name))return n[i];}return null;}",
    "function setNodeText(n,t){if(!n)return;if(typeof n.textContent!='undefined')",
    "n.textContent=t;else n.innerText=t;}",
    "function escHTML(t){return String(t).replace(/&/g,'&amp;')",
    ".replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;');}",
    "function mdSafeHref(h){var v=(h||'').replace(/^\\s+|\\s+$/g,'').toLowerCase();",
    "return v.indexOf('http://')==0||v.indexOf('https://')==0||",
    "v.indexOf('mailto:')==0;}",
    "function mdStash(a,s){var k='\\x1f'+a.length+'\\x1f';a[a.length]=s;return k;}",
    "function mdRestore(s,a){var p;do{p=s;s=s.replace(/\\x1f(\\d+)\\x1f/g,",
    "function(m,i){return a[parseInt(i,10)]||'';});}while(s!=p&&s.indexOf('\\x1f')>=0);return s;}",
    "function mdInline(s){var a=[];s=s.replace(/`([^`\\n]+)`/g,",
    "function(m,c){return mdStash(a,'<code>'+c+'</code>');});",
    "s=s.replace(/\\[([^\\]\\n]+)\\]\\(([^)\\s]+)\\)/g,function(m,l,h){",
    "if(!mdSafeHref(h))return l;return mdStash(a,'<a href=\"'+h+'\">'+mdInline(l)+'</a>');});",
    "s=s.replace(/(\\*\\*|__)([^\\n]+?)\\1/g,'<strong>$2</strong>');",
    "s=s.replace(/~~([^\\n]+?)~~/g,'<s>$1</s>');",
    "s=s.replace(/(^|[^*])\\*([^*\\n]+)\\*/g,'$1<em>$2</em>');",
    "s=s.replace(/(^|[^_])_([^_\\n]+)_/g,'$1<em>$2</em>');",
    "return mdRestore(s,a);}",
    "function mdLangClass(line){var l=line.replace(/^\\s*```\\s*/,'')",
    ".replace(/\\s+$/,'');return /^[A-Za-z0-9_-]+$/.test(l)?' class=\"language-'+l+'\"':'';}",
    "function mdParagraph(lines){return '<p>'+mdInline(lines.join('\\n'))",
    ".replace(/\\n/g,'<br />')+'</p>';}",
    "function mdList(lines,start,ordered){var out=[];var i=start;var m;var r;",
    "if(ordered)r=/^\\s*\\d+\\.\\s+(.+)$/;else r=/^\\s*[-*+]\\s+(.+)$/;",
    "for(;i<lines.length;i++){",
    "m=r.exec(lines[i]);if(!m)break;out[out.length]='<li>'+mdInline(m[1])+'</li>';}",
    "return {html:'<'+(ordered?'ol':'ul')+'>'+out.join('')+'</'+(ordered?'ol':'ul')+'>',next:i};}",
    "function mdQuote(lines,start){var out=[];var i=start;var m;",
    "for(;i<lines.length;i++){m=/^\\s*>\\s?(.*)$/.exec(lines[i]);if(!m)break;",
    "out[out.length]=mdInline(m[1]);}return {html:'<blockquote>'+out.join('<br />')+'</blockquote>',next:i};}",
    "function mdTrim(s){return String(s).replace(/^\\s+|\\s+$/g,'');}",
    "function mdEscaped(s,i){var n=0;i--;while(i>=0&&s.charAt(i)=='\\\\'){n++;i--;}return n%2==1;}",
    "function mdHasPipe(s){for(var i=0;i<s.length;i++){",
    "if(s.charAt(i)=='|'&&!mdEscaped(s,i))return true;}return false;}",
    "function mdSplitTableRow(line){var s=mdTrim(line);var c=[];var p='';var i,ch;",
    "if(s.charAt(0)=='|'&&!mdEscaped(s,0))s=s.substring(1);",
    "if(s.length&&s.charAt(s.length-1)=='|'&&!mdEscaped(s,s.length-1))",
    "s=s.substring(0,s.length-1);",
    "for(i=0;i<s.length;i++){ch=s.charAt(i);if(ch=='|'&&!mdEscaped(s,i)){",
    "c[c.length]=mdTrim(p).replace(/\\\\\\|/g,'|');p='';}else p+=ch;}",
    "c[c.length]=mdTrim(p).replace(/\\\\\\|/g,'|');return c;}",
    "function mdTableAligns(line){var c=mdSplitTableRow(line);var a=[];var i,t;",
    "if(!mdHasPipe(line))return null;for(i=0;i<c.length;i++){",
    "t=c[i].replace(/\\s/g,'');if(!/^:?-{3,}:?$/.test(t))return null;",
    "if(t.charAt(0)==':'&&t.charAt(t.length-1)==':')a[i]='center';",
    "else if(t.charAt(t.length-1)==':')a[i]='right';else a[i]='';}return a;}",
    "function mdTableCell(tag,text,align){var a=align?",
    "' style=\"text-align:'+align+'\"':'';return '<'+tag+a+'>'+mdInline(text)+'</'+tag+'>';}",
    "function mdTable(lines,start){var h=mdSplitTableRow(lines[start]);",
    "var a=mdTableAligns(lines[start+1]);var cols=Math.max(h.length,a.length);",
    "var out=['<div class=\"table-wrap\"><table><thead><tr>'];var i,j,c;",
    "for(j=0;j<cols;j++)out[out.length]=mdTableCell('th',h[j]||'',a[j]);",
    "out[out.length]='</tr></thead><tbody>';",
    "for(i=start+2;i<lines.length&&!/^\\s*$/.test(lines[i])&&mdHasPipe(lines[i]);i++){",
    "c=mdSplitTableRow(lines[i]);out[out.length]='<tr>';",
    "for(j=0;j<cols;j++)out[out.length]=mdTableCell('td',c[j]||'',a[j]);",
    "out[out.length]='</tr>';}out[out.length]='</tbody></table></div>';",
    "return {html:out.join(''),next:i};}",
    "function mdToHTML(raw){var text=(raw||'').replace(/\\r\\n/g,'\\n').replace(/\\r/g,'\\n');",
    "var lines=text.split('\\n');var out=[];var para=[];var i,line,m,r,code;",
    "function flush(){if(para.length){out[out.length]=mdParagraph(para);para=[];}}",
    "for(i=0;i<lines.length;i++){line=lines[i];if(/^\\s*$/.test(line)){flush();continue;}",
    "if(/^\\s*```/.test(line)){flush();code=[];var cls=mdLangClass(line);",
    "for(i=i+1;i<lines.length&&!/^\\s*```\\s*$/.test(lines[i]);i++)code[code.length]=lines[i];",
    "out[out.length]='<pre><code'+cls+'>'+code.join('\\n')+'</code></pre>';continue;}",
    "m=/^(#{1,6})\\s+(.+)$/.exec(line);if(m){flush();",
    "out[out.length]='<h'+m[1].length+'>'+mdInline(m[2])+'</h'+m[1].length+'>';continue;}",
    "if((i+1)<lines.length&&mdHasPipe(line)&&mdTableAligns(lines[i+1])){",
    "flush();r=mdTable(lines,i);out[out.length]=r.html;i=r.next-1;continue;}",
    "if(/^\\s*[-*+]\\s+/.test(line)){flush();r=mdList(lines,i,0);out[out.length]=r.html;i=r.next-1;continue;}",
    "if(/^\\s*\\d+\\.\\s+/.test(line)){flush();r=mdList(lines,i,1);out[out.length]=r.html;i=r.next-1;continue;}",
    "if(/^\\s*>/.test(line)){flush();r=mdQuote(lines,i);out[out.length]=r.html;i=r.next-1;continue;}",
    "para[para.length]=line;}flush();return out.join('');}",
    "function renderMarkdownNode(n){if(!n)return;if(typeof n._strappyMarkdown=='undefined')",
    "n._strappyMarkdown=n.innerHTML;n.innerHTML=mdToHTML(n._strappyMarkdown);}",
    "function renderMarkdown(root){root=root||document;var n=root.getElementsByTagName('*');",
    "for(var i=0;i<n.length;i++){if(hasClass(n[i],'bubble')||",
    "hasClass(n[i],'reasoning-body'))renderMarkdownNode(n[i]);}}",
    "function isObj(v){return v&&typeof v=='object'&&",
    "Object.prototype.toString.call(v)!='[object Array]';}",
    "function jsonText(v){if(v===null||typeof v=='undefined')return '';",
    "if(typeof v=='string')return v;if(typeof v=='number'||typeof v=='boolean')",
    "return String(v);if(typeof JSON!='undefined'&&JSON.stringify)return JSON.stringify(v);",
    "return String(v);}",
    "function addMetaLine(lines,label,value){var t=jsonText(value);",
    "if(t!=='')lines[lines.length]=label+': '+t;}",
    "function addNestedMetaLine(lines,label,root,parent,key){",
    "if(isObj(root)&&isObj(root[parent]))addMetaLine(lines,label,root[parent][key]);}",
    "function formatMetadata(root){var lines=[];var usage;var gen;",
    "if(!isObj(root))return jsonText(root);",
    "addMetaLine(lines,'Response ID',root.response_id);",
    "addMetaLine(lines,'Model',root.model);",
    "addMetaLine(lines,'Created',root.created);",
    "addMetaLine(lines,'Finish reason',root.finish_reason);",
    "addMetaLine(lines,'Native finish reason',root.native_finish_reason);",
    "addMetaLine(lines,'Service tier',root.service_tier);",
    "addMetaLine(lines,'System fingerprint',root.system_fingerprint);",
    "addMetaLine(lines,'HTTP status',root.http_status);",
    "usage=root.usage;if(isObj(usage)){",
    "addMetaLine(lines,'Cost',usage.cost);",
    "addMetaLine(lines,'Prompt tokens',usage.prompt_tokens);",
    "addMetaLine(lines,'Completion tokens',usage.completion_tokens);",
    "addMetaLine(lines,'Total tokens',usage.total_tokens);",
    "addNestedMetaLine(lines,'Cached prompt tokens',usage,'prompt_tokens_details','cached_tokens');",
    "addNestedMetaLine(lines,'Cache write tokens',usage,'prompt_tokens_details','cache_write_tokens');",
    "addNestedMetaLine(lines,'Prompt audio tokens',usage,'prompt_tokens_details','audio_tokens');",
    "addNestedMetaLine(lines,'Reasoning tokens',usage,'completion_tokens_details','reasoning_tokens');",
    "addNestedMetaLine(lines,'Completion audio tokens',usage,'completion_tokens_details','audio_tokens');",
    "addNestedMetaLine(lines,'Accepted prediction tokens',usage,'completion_tokens_details','accepted_prediction_tokens');",
    "addNestedMetaLine(lines,'Rejected prediction tokens',usage,'completion_tokens_details','rejected_prediction_tokens');",
    "addNestedMetaLine(lines,'Upstream inference cost',usage,'cost_details','upstream_inference_cost');}",
    "gen=root.generation;if(isObj(gen)){if(isObj(gen.data))gen=gen.data;",
    "addMetaLine(lines,'Provider',gen.provider_name);",
    "addMetaLine(lines,'Router',gen.router);",
    "addMetaLine(lines,'OpenRouter request ID',gen.request_id);",
    "addMetaLine(lines,'Upstream ID',gen.upstream_id);",
    "addMetaLine(lines,'Streamed',gen.streamed);",
    "addMetaLine(lines,'Data region',gen.data_region);",
    "addMetaLine(lines,'Latency (ms)',gen.latency);",
    "addMetaLine(lines,'Generation time (ms)',gen.generation_time);",
    "addMetaLine(lines,'Moderation latency (ms)',gen.moderation_latency);",
    "addMetaLine(lines,'Native prompt tokens',gen.native_tokens_prompt);",
    "addMetaLine(lines,'Native completion tokens',gen.native_tokens_completion);",
    "addMetaLine(lines,'Native reasoning tokens',gen.native_tokens_reasoning);",
    "addMetaLine(lines,'Native cached tokens',gen.native_tokens_cached);",
    "addMetaLine(lines,'Total cost',gen.total_cost);",
    "addMetaLine(lines,'Usage',gen.usage);",
    "addMetaLine(lines,'Upstream inference cost',gen.upstream_inference_cost);",
    "addMetaLine(lines,'Generation finish reason',gen.finish_reason);",
    "addMetaLine(lines,'Generation native finish reason',gen.native_finish_reason);",
    "addMetaLine(lines,'Web search engine',gen.web_search_engine);",
    "addMetaLine(lines,'Search results',gen.num_search_results);}",
    "if(lines.length)return lines.join('\\n');return jsonText(root);}",
    "function parseMetadata(raw){if(typeof JSON!='undefined'&&JSON.parse)",
    "return JSON.parse(raw);return eval('('+raw+')');}",
    "function renderMetadata(root){root=root||document;var n=root.getElementsByTagName('*');",
    "for(var i=0;i<n.length;i++){if(!hasClass(n[i],'request-metadata'))continue;",
    "var raw=n[i].getAttribute('data-metadata');var body=firstByClass(n[i],'request-metadata-body');",
    "if(!raw||!body)continue;try{setNodeText(body,formatMetadata(parseMetadata(raw)));}",
    "catch(e){setNodeText(body,raw);}}}",
    "function clearEmpty(){var e=byId('empty');if(e)e.style.display='none';}",
    "function nodesFromHTML(html){var d=document.createElement('div');d.innerHTML=html;return d;}",
    "function scrollBottom(){setTimeout(function(){window.scrollTo(0,document.body.scrollHeight);},0);}",
    "function appendMessage(html){clearEmpty();var m=byId('messages');if(!m)return;",
    "if(m.insertAdjacentHTML){m.insertAdjacentHTML('beforeend',html);}",
    "else{var d=nodesFromHTML(html);while(d.firstChild)m.appendChild(d.firstChild);}",
    "renderMarkdown(m);renderMetadata(m);scrollBottom();}",
    "function replaceMessage(id,html){clearEmpty();var old=byId(id);",
    "if(!old){appendMessage(html);return;}var d=nodesFromHTML(html);",
    "if(d.firstChild)old.parentNode.replaceChild(d.firstChild,old);",
    "renderMarkdown(document);renderMetadata(document);scrollBottom();}",
    "function prependMessages(html,hasMore){var m=byId('messages');if(!m)return;",
    "var d=nodesFromHTML(html);while(d.lastChild)m.insertBefore(d.lastChild,m.firstChild);",
    "var l=byId('load-more');if(l&&!hasMore)l.parentNode.removeChild(l);",
    "renderMarkdown(m);renderMetadata(m);}",
    "function setMessageState(id,status,state){var r=byId(id);if(!r)return;",
    "r.className=r.className.replace(/\\sstate-[^\\s]+/g,'')+' state-'+state;",
    "var s=firstByClass(r,'status');if(!s){s=document.createElement('div');",
    "s.className='meta status';r.appendChild(s);}s.innerHTML=status;scrollBottom();}",
    "function appendTextToNode(n,t){if(!n)return;if(typeof n.textContent!='undefined')",
    "n.textContent=n.textContent+t;else n.innerText=n.innerText+t;}",
    "function appendMessageText(id,t){var r=byId(id);if(!r)return;",
    "var b=firstByClass(r,'bubble');if(!b)return;",
    "if(typeof b._strappyMarkdown=='undefined')b._strappyMarkdown=b.innerHTML;",
    "b._strappyMarkdown+=escHTML(t);renderMarkdownNode(b);scrollBottom();}",
    "function appendReasoningText(id,t){var r=byId(id);if(!r)return;",
    "var box=firstByClass(r,'reasoning');var body=firstByClass(r,'reasoning-body');",
    "if(box)box.style.display='block';if(!body)return;",
    "if(typeof body._strappyMarkdown=='undefined')body._strappyMarkdown=body.innerHTML;",
    "body._strappyMarkdown+=escHTML(t);renderMarkdownNode(body);scrollBottom();}",
    "function removeMessage(id){var r=byId(id);if(r&&r.parentNode)r.parentNode.removeChild(r);}",
    "</script>",
    NULL
  };

  return strappy_webview_append_chunks(buffer, chunks);
}

void strappy_webview_free(char *value)
{
  free(value);
}

char *strappy_webview_status_html(const char *text,
                                  int retry,
                                  const strappy_webview_labels *labels)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_append_html_escaped(&buffer, text)) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }

  if (retry) {
    if (!strappy_webview_buffer_append_cstring(
          &buffer,
          " &middot; <a href=\"strappy-action://retry\">") ||
        !strappy_webview_append_html_escaped(&buffer,
                                             strappy_webview_retry_label(labels)) ||
        !strappy_webview_buffer_append_cstring(&buffer, "</a>")) {
      strappy_webview_buffer_destroy(&buffer);
      return NULL;
    }
  }

  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_message_html(const strappy_webview_message *message,
                                   const strappy_webview_labels *labels,
                                   const char *state,
                                   const char *status_html)
{
  strappy_webview_buffer buffer;
  const char *role;
  const char *text;
  const char *reasoning;
  const char *created_at;
  const char *metadata_json;
  const char *status_to_render;
  char *owned_status_html;
  char http_status_text[64];
  int has_state;
  int ok;

  role = ((message != NULL) && (message->role != NULL) &&
          (message->role[0] != '\0')) ? message->role : "assistant";
  text = (message != NULL) ? strappy_webview_string_or_empty(message->text) : "";
  reasoning = (message != NULL) ?
    strappy_webview_string_or_empty(message->reasoning) : "";
  created_at = (message != NULL) ?
    strappy_webview_string_or_empty(message->created_at) : "";
  metadata_json = (message != NULL) ?
    strappy_webview_string_or_empty(message->metadata_json) : "";
  status_to_render = strappy_webview_string_or_empty(status_html);
  owned_status_html = NULL;
  has_state = (state != NULL) && (state[0] != '\0');

  if (!has_state && (status_to_render[0] == '\0') &&
      (message != NULL) && (message->http_status >= 400L)) {
    snprintf(http_status_text, sizeof(http_status_text), "HTTP %ld",
             message->http_status);
    owned_status_html = strappy_webview_status_html(http_status_text, 0, labels);
    if (owned_status_html == NULL) {
      return NULL;
    }
    status_to_render = owned_status_html;
    state = "error";
    has_state = 1;
  }

  strappy_webview_buffer_init(&buffer);
  ok = strappy_webview_buffer_append_cstring(&buffer, "<div id=\"") &&
       strappy_webview_append_element_id(&buffer, message) &&
       strappy_webview_buffer_append_cstring(&buffer, "\" class=\"row ") &&
       strappy_webview_append_html_escaped(&buffer, role);

  if (ok && has_state) {
    ok = strappy_webview_buffer_append_cstring(&buffer, " state-") &&
         strappy_webview_append_html_escaped(&buffer, state);
  }

  ok = ok &&
       strappy_webview_buffer_append_cstring(&buffer, "\"><div class=\"role\">") &&
       strappy_webview_append_html_escaped(&buffer,
                                           strappy_webview_role_label(role, labels)) &&
       strappy_webview_buffer_append_cstring(&buffer, "</div>");

  if (ok && strappy_webview_is_assistant_role(role)) {
    ok = strappy_webview_append_reasoning_html(&buffer, reasoning, 0, labels);
  }

  ok = ok &&
       strappy_webview_buffer_append_cstring(&buffer, "<div class=\"bubble\">") &&
       strappy_webview_append_html_escaped(&buffer, text) &&
       strappy_webview_buffer_append_cstring(&buffer, "</div>") &&
       strappy_webview_append_metadata_html(&buffer, role, metadata_json, labels);

  if (ok && (status_to_render[0] != '\0')) {
    ok = strappy_webview_buffer_append_cstring(
           &buffer,
           "<div class=\"meta status\">") &&
         strappy_webview_buffer_append_cstring(&buffer, status_to_render) &&
         strappy_webview_buffer_append_cstring(&buffer, "</div>");
  } else if (ok && (created_at[0] != '\0')) {
    ok = strappy_webview_buffer_append_cstring(&buffer, "<div class=\"meta\">") &&
         strappy_webview_append_html_escaped(&buffer, created_at) &&
         strappy_webview_buffer_append_cstring(&buffer, "</div>");
  }

  ok = ok && strappy_webview_buffer_append_cstring(&buffer, "</div>");

  free(owned_status_html);
  if (!ok) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_pending_message_html(
  const char *prompt,
  const char *element_id,
  const char *state,
  const char *status_html,
  const strappy_webview_labels *labels)
{
  strappy_webview_message message;

  memset(&message, 0, sizeof(message));
  message.element_id = element_id;
  message.role = "user";
  message.text = strappy_webview_string_or_empty(prompt);
  return strappy_webview_message_html(&message, labels, state, status_html);
}

char *strappy_webview_streaming_assistant_message_html(
  const char *element_id,
  const char *text,
  const char *reasoning,
  const char *state,
  const char *status_html,
  const strappy_webview_labels *labels)
{
  strappy_webview_buffer buffer;
  int has_state;
  int ok;

  if ((element_id == NULL) || (element_id[0] == '\0')) {
    element_id = "streaming-assistant";
  }
  has_state = (state != NULL) && (state[0] != '\0');

  strappy_webview_buffer_init(&buffer);
  ok = strappy_webview_buffer_append_cstring(&buffer, "<div id=\"") &&
       strappy_webview_append_html_escaped(&buffer, element_id) &&
       strappy_webview_buffer_append_cstring(&buffer,
                                             "\" class=\"row assistant");
  if (ok && has_state) {
    ok = strappy_webview_buffer_append_cstring(&buffer, " state-") &&
         strappy_webview_append_html_escaped(&buffer, state);
  }
  ok = ok &&
       strappy_webview_buffer_append_cstring(&buffer, "\"><div class=\"role\">") &&
       strappy_webview_append_html_escaped(&buffer,
                                           strappy_webview_agent_label(labels)) &&
       strappy_webview_buffer_append_cstring(&buffer, "</div>") &&
       strappy_webview_append_reasoning_html(&buffer, reasoning, 1, labels) &&
       strappy_webview_buffer_append_cstring(&buffer, "<div class=\"bubble\">") &&
       strappy_webview_append_html_escaped(&buffer, text) &&
       strappy_webview_buffer_append_cstring(&buffer, "</div>");

  if (ok && (status_html != NULL) && (status_html[0] != '\0')) {
    ok = strappy_webview_buffer_append_cstring(
           &buffer,
           "<div class=\"meta status\">") &&
         strappy_webview_buffer_append_cstring(&buffer, status_html) &&
         strappy_webview_buffer_append_cstring(&buffer, "</div>");
  }

  ok = ok && strappy_webview_buffer_append_cstring(&buffer, "</div>");
  if (!ok) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_message_html_with_reasoning(
  const strappy_webview_message *message,
  const char *reasoning,
  const strappy_webview_labels *labels)
{
  strappy_webview_message copy;

  if ((reasoning == NULL) || (reasoning[0] == '\0') ||
      (message == NULL) ||
      !strappy_webview_is_assistant_role(message->role)) {
    return strappy_webview_message_html(message, labels, NULL, NULL);
  }

  copy = *message;
  copy.reasoning = reasoning;
  return strappy_webview_message_html(&copy, labels, NULL, NULL);
}

char *strappy_webview_messages_page_html(const char *messages_html,
                                         const char *empty_text,
                                         int has_messages,
                                         int has_more,
                                         const char *load_more_label)
{
  strappy_webview_buffer buffer;
  int ok;

  strappy_webview_buffer_init(&buffer);
  ok = strappy_webview_buffer_append_cstring(
         &buffer,
         "<!doctype html><html><head><meta charset=\"utf-8\">") &&
       strappy_webview_append_styles(&buffer) &&
       strappy_webview_append_scripts(&buffer) &&
       strappy_webview_buffer_append_cstring(
         &buffer,
         "</head><body><div class=\"page\">");

  if (ok && has_more) {
    ok = strappy_webview_buffer_append_cstring(
           &buffer,
           "<a id=\"load-more\" class=\"load-more\" "
           "href=\"strappy-action://load-more\">") &&
         strappy_webview_append_html_escaped(&buffer, load_more_label) &&
         strappy_webview_buffer_append_cstring(&buffer, "</a>");
  }

  if (ok && !has_messages) {
    ok = strappy_webview_buffer_append_cstring(
           &buffer,
           "<div id=\"empty\" class=\"empty\">") &&
         strappy_webview_append_html_escaped(&buffer, empty_text) &&
         strappy_webview_buffer_append_cstring(&buffer, "</div>");
  } else if (ok) {
    ok = strappy_webview_buffer_append_cstring(
      &buffer,
      "<div id=\"empty\" class=\"empty\" style=\"display:none\"></div>");
  }

  ok = ok &&
       strappy_webview_buffer_append_cstring(&buffer, "<div id=\"messages\">") &&
       strappy_webview_buffer_append_cstring(&buffer, messages_html) &&
       strappy_webview_buffer_append_cstring(
         &buffer,
         "</div></div><script>renderMarkdown(document);renderMetadata(document);"
         "</script></body></html>");

  if (!ok) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_append_message_js(const char *message_html)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "appendMessage(") ||
      !strappy_webview_append_js_string(&buffer, message_html) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_replace_message_js(const char *element_id,
                                         const char *message_html)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "replaceMessage(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, message_html) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_set_message_state_js(const char *element_id,
                                           const char *status_html,
                                           const char *state)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "setMessageState(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, status_html) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, state) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_append_message_text_js(const char *element_id,
                                             const char *delta)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "appendMessageText(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, delta) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_append_reasoning_text_js(const char *element_id,
                                               const char *delta)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "appendReasoningText(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ",") ||
      !strappy_webview_append_js_string(&buffer, delta) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_remove_message_js(const char *element_id)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "removeMessage(") ||
      !strappy_webview_append_js_string(&buffer, element_id) ||
      !strappy_webview_buffer_append_cstring(&buffer, ");")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

char *strappy_webview_prepend_messages_js(const char *messages_html,
                                          int has_more)
{
  strappy_webview_buffer buffer;

  strappy_webview_buffer_init(&buffer);
  if (!strappy_webview_buffer_append_cstring(&buffer, "prependMessages(") ||
      !strappy_webview_append_js_string(&buffer, messages_html) ||
      !strappy_webview_buffer_append_cstring(&buffer, has_more ? ",true);" : ",false);")) {
    strappy_webview_buffer_destroy(&buffer);
    return NULL;
  }
  return strappy_webview_buffer_finish(&buffer);
}

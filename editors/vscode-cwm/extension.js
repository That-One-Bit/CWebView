const vscode = require('vscode');

const TOKEN_TYPES = ['keyword', 'type', 'function', 'variable', 'parameter', 'string', 'number', 'operator', 'comment', 'property', 'enumMember', 'class'];
const legend = new vscode.SemanticTokensLegend(TOKEN_TYPES, ['declaration', 'definition', 'readonly', 'defaultLibrary', 'deprecated', 'modification']);
const elementKinds = new Set(['window','panel','header','button','text','underline','slider','checkbox','segmented','badge','footer','decoration','icon','progress','value','list','shape_rectangle','shape_rounded_rectangle','shape_circle','line','scrollbar']);
const styles = new Set(['normal','light','dim']);
const keys = new Set(['action','stops','brightness','tint','border','anchor','size','corner','crop','pair','scale','when']);
const directives = new Set(['CWMenu','menu','element','theme','project','var','end','open_script','content','scrollbar','track','item','items','when']);
const enums = new Set(['rounded','sharp','normal','light','dim','top','right','bottom','left','all','fill','none','repeat','no-repeat']);
const docs = {
  'CWMenu':'File format header. The host records the version and can warn if it is newer than supported.',
  'menu':'Starts a named menu block. App-defined menu names are registered dynamically.',
  'element':'Declares an element: name, kind, x, y, width, height, style, quoted label, then optional keys.',
  'theme':'Starts the theme block. Supported slots include accent0..accent5, label0..label5, text-normal/light/dim, neutral, and overlay.',
  'project':'Declares project defaults. Host-injected values take precedence.',
  'var':'Declares a reactive numeric variable, e.g. var pitch_stops 6.',
  'action':'Host dispatch key for an interactive element. Actions are implemented by the host application.',
  'when':'Conditionally includes an element if a project feature is enabled; prefix with ! to negate.',
  'pair':'Names a passive partner menu to render alongside the active menu. It does not create a second interactive window.',
  'open_script':'Host action convention that links another .cwm script at runtime.',
  'stops':'Slider stop count or the name of a numeric variable that controls it.',
  'brightness':'Brightness multiplier or variable reference.',
  'tint':'Accent tint value from 0 to 1 or a variable reference.',
  'border':'Optional element border sides: top, right, bottom, left, all, fill, none.',
  'anchor':'Window screen position in x/y coordinates.',
  'size':'Window chrome scale; documented clamp is 0.75 to 1.25.',
  'corner':'Window corner style: sharp or rounded.',
  'crop':'Window chrome insets: left, top, right, bottom.'
};

function parseDocument(document, collection) {
  const lines = document.getText().split(/\r?\n/);
  const menus = new Set(); const variables = new Set(); const declaredElements = new Map(); let currentMenu = ''; let inTheme = false; let inProject = false;
  const pairs = []; const refs = []; const diagnostics = []; const tokens = [];
  const addToken = (line, start, length, type, mods = []) => { if (length > 0) tokens.push({line,start,length,type,mods}); };
  const diag = (line, start, length, message, severity = vscode.DiagnosticSeverity.Warning, code = 'cwm') => {
    const range = new vscode.Range(line, Math.max(0,start), line, Math.max(0,start)+Math.max(1,length));
    const d = new vscode.Diagnostic(range, message, severity); d.source = 'CWMenu'; d.code = code; diagnostics.push(d);
  };
  lines.forEach((raw, lineNo) => {
    const comment = raw.indexOf('#');
    const trimmed = raw.trim(); if (!trimmed || trimmed.startsWith('#')) { if (trimmed.startsWith('#')) addToken(lineNo, raw.indexOf('#'), raw.length-raw.indexOf('#'), 'comment'); return; }
    const head = /^\s*(CWMenu|menu|element|theme|project|var|end|open_script|content|scrollbar|track|item|items|when)\b/.exec(raw);
    if (head) {
      const t = head[1]; addToken(lineNo, head.index, t.length, 'keyword', ['defaultLibrary']);
      if (t === 'menu') { const m = /^\s*menu\s+([\w.-]+)/.exec(raw); if (m) { currentMenu = m[1]; menus.add(m[1]); addToken(lineNo, m.index+m[0].lastIndexOf(m[1]), m[1].length, 'class', ['declaration']); } }
      if (t === 'end') { inTheme = false; inProject = false; currentMenu = ''; }
      if (t === 'theme') inTheme = true; if (t === 'project') inProject = true;
      if (t === 'var') { const m = /^\s*var\s+([\w.-]+)/.exec(raw); if (m) { variables.add(m[1]); addToken(lineNo, m.index+m[0].lastIndexOf(m[1]), m[1].length, 'variable', ['declaration']); } }
    }
    const strings = [...raw.matchAll(/"(?:\\.|[^"\\])*"/g)];
    strings.forEach(m => addToken(lineNo, m.index, m[0].length, 'string'));
    for (const m of raw.matchAll(/\$[A-Za-z_][\w.-]*/g)) addToken(lineNo,m.index,m[0].length,'variable',['readonly']);
    for (const m of raw.matchAll(/#[0-9a-fA-F]{6}\b/g)) addToken(lineNo,m.index,m[0].length,'number');
    const em = /^\s*element\s+([\w.-]+)\s+([\w.-]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\s+(normal|light|dim)\s+("(?:\\.|[^"\\])*"|\S+)/.exec(raw);
    if (em) {
      const name = em[1], kind = em[2]; const nameAt = raw.indexOf(name, em.index);
      addToken(lineNo,nameAt,name.length,'property',['declaration']);
      const kindAt = raw.indexOf(kind,nameAt+name.length); addToken(lineNo,kindAt,kind.length,elementKinds.has(kind)?'type':'type',['defaultLibrary']);
      if (!elementKinds.has(kind)) diag(lineNo,kindAt,kind.length,`Unknown element kind '${kind}'.`,vscode.DiagnosticSeverity.Error,'unknown-kind');
      const prev = declaredElements.get(`${currentMenu}:${name}`); if (prev !== undefined) diag(lineNo,nameAt,name.length,`Duplicate element name '${name}' in this menu (also declared on line ${prev+1}).`,vscode.DiagnosticSeverity.Warning,'duplicate-element');
      declaredElements.set(`${currentMenu}:${name}`,lineNo);
      const labelMatch = strings.find(s => s.index > kindAt);
      const tailStart = labelMatch ? labelMatch.index+labelMatch[0].length : em.index+em[0].length;
      const tail = raw.slice(tailStart);
      for (const km of tail.matchAll(/\b([A-Za-z_][\w-]*)\b/g)) {
        const k = km[1]; if (keys.has(k)) addToken(lineNo,tailStart+km.index,k.length,'property');
      }
      const pair = /\bpair\s+([\w.-]+)/.exec(tail); if (pair) pairs.push({name:pair[1],line:lineNo,start:tailStart+pair.index+pair[0].lastIndexOf(pair[1])});
      const when = /\bwhen\s+(!?[\w.-]+)/.exec(tail); if (when) refs.push({name:when[1].replace(/^!/,''),line:lineNo,start:tailStart+when.index+when[0].lastIndexOf(when[1]),type:'project feature'});
      const action = /\baction\s+([^\s]+)/.exec(tail); if (action) addToken(lineNo,tailStart+action.index+action[0].lastIndexOf(action[1]),action[1].length,'function');
      const unknownEnabled = vscode.workspace.getConfiguration('cwmenu').get('diagnostics.unknownKeys', true);
      if (unknownEnabled) {
        const knownTail = new Set(['action','stops','brightness','tint','border','anchor','size','corner','crop','pair','scale','when']);
        for (const km of tail.matchAll(/\b([A-Za-z_][\w-]*)\b/g)) {
          const k = km[1]; if (knownTail.has(k) || enums.has(k) || /^\d/.test(k) || k.startsWith('$') || ['true','false'].includes(k)) continue;
          // Only warn when token plausibly occurs in key position, not values after known keys.
          const before = tail.slice(0,km.index).trimEnd();
          if (!before || /(?:^|\s)(?:action|stops|brightness|tint|border|anchor|size|corner|crop|pair|scale|when)\s+[^\s]*$/.test(before)) continue;
          if (/^\s*\w+\s*$/.test(before)) diag(lineNo,tailStart+km.index,k.length,`Unrecognized CWMenu key or value '${k}'. Check spelling and key syntax.`,vscode.DiagnosticSeverity.Information,'unknown-key');
        }
      }
    }
    const pairOnly = /\bpair\s+([\w.-]+)/.exec(raw); if (pairOnly && !em) pairs.push({name:pairOnly[1],line:lineNo,start:pairOnly.index+pairOnly[0].lastIndexOf(pairOnly[1])});
    if (/\b(open_script:)([^\s"']+)/.test(raw)) { const m=/\bopen_script:([^\s"']+)/.exec(raw); addToken(lineNo,m.index,m[1].length,'function'); addToken(lineNo,m.index+m[1].length,m[2].length,'string'); }
  });
  // Recheck element-local names and menu pairs after all declarations are known.
  for (const p of pairs) if (!menus.has(p.name)) diag(p.line,p.start,p.name.length,`Paired menu '${p.name}' is not declared in this file. It may be supplied by a linked script.`,vscode.DiagnosticSeverity.Warning,'unresolved-pair');
  const seenVars = new Set();
  lines.forEach((raw,lineNo) => { for (const m of raw.matchAll(/\$([A-Za-z_][\w.-]*)/g)) { if (!variables.has(m[1]) && !seenVars.has(m[1])) diag(lineNo,m.index,m[0].length,`Variable '$${m[1]}' is not declared with 'var'. Host-provided values may be intentional.`,vscode.DiagnosticSeverity.Information,'unresolved-variable'); } });
  collection.set(document.uri, diagnostics);
  tokens.sort((a,b)=>a.line-b.line||a.start-b.start);
  const builder = new vscode.SemanticTokensBuilder(legend);
  for (const t of tokens) builder.push(t.line,t.start,t.length,t.type,TOKEN_TYPES.indexOf(t.type) < 0 ? 0 : t.mods.reduce((bits,m)=>bits | (1 << ['declaration','definition','readonly','defaultLibrary','deprecated','modification'].indexOf(m)),0));
  return builder.build();
}

function activate(context) {
  const diagnostics = vscode.languages.createDiagnosticCollection('cwmenu'); context.subscriptions.push(diagnostics);
  const selector = {language:'cwm', scheme:'file'};
  const refresh = doc => { if (doc && doc.languageId === 'cwm') parseDocument(doc, diagnostics); };
  for (const doc of vscode.workspace.textDocuments) refresh(doc);
  context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(refresh), vscode.workspace.onDidChangeTextDocument(e=>refresh(e.document)), vscode.workspace.onDidSaveTextDocument(refresh));
  context.subscriptions.push(vscode.languages.registerDocumentSemanticTokensProvider(selector,{provideDocumentSemanticTokens(doc){return parseDocument(doc,diagnostics);}},legend));
  context.subscriptions.push(vscode.languages.registerHoverProvider(selector,{provideHover(doc,pos){
    const line=doc.lineAt(pos.line).text; const word=doc.getWordRangeAtPosition(pos,/[$A-Za-z_][\w.-]*/); if(!word)return;
    const key=doc.getText(word).replace(/^\$/,''); if(docs[key])return new vscode.Hover(new vscode.MarkdownString(`**CWMenu: \`${key}\`**\n\n${docs[key]}`));
    if(elementKinds.has(key))return new vscode.Hover(new vscode.MarkdownString(`**Element kind: \`${key}\`**\n\n${kindHelp(key)}`));
    if(styles.has(key))return new vscode.Hover(new vscode.MarkdownString(`**Style: \`${key}\`**\n\nControls the theme color family used by this element.`));
  }}));
  context.subscriptions.push(vscode.languages.registerCompletionItemProvider(selector,{provideCompletionItems(doc,pos){
    const line=doc.lineAt(pos.line).text; const items=[];
    if(/^\s*element\s+[^\s]*\s+[^\s]*\s+/.test(line)) for(const kind of elementKinds){const i=new vscode.CompletionItem(kind,vscode.CompletionItemKind.Type);i.detail='CWMenu element kind';items.push(i);}
    for(const k of keys){const i=new vscode.CompletionItem(k,vscode.CompletionItemKind.Property);i.detail=docs[k]||'CWMenu element key';items.push(i);}
    for(const d of directives){const i=new vscode.CompletionItem(d,vscode.CompletionItemKind.Keyword);i.detail=docs[d]||'CWMenu directive';items.push(i);}
    return items;
  }}));
}
function kindHelp(k){return ({window:'Window geometry and optional anchor/size/corner/crop/pair settings.',panel:'Inset background panel.',header:'Window header chrome.',footer:'Footer chrome.',button:'Actionable button; add `action name` to dispatch an action.',text:'Static text; wraps within its declared width and height.',slider:'Numeric choice control; use `stops N` or `stops $var`.',checkbox:'Boolean choice control.',segmented:'Segmented choice control.',list:'List choice control.',badge:'Compact status label.',progress:'Progress indicator.',value:'Value readout.',icon:'Icon slot; host rendering depends on platform support.',scrollbar:'Scrollbar group/viewport decoration.',underline:'Thin separator.',decoration:'Decorative accent block.',shape_rectangle:'Rectangle shape.',shape_rounded_rectangle:'Rounded rectangle shape.',shape_circle:'Circle shape.',line:'Line shape.'})[k]||'CWMenu menu element.';}
function deactivate() {}
module.exports = {activate, deactivate};

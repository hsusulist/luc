/* A small LUC syntax highlighter — no dependencies, no build step. */

const KEYWORDS = new Set([
  'and', 'as', 'break', 'command', 'create', 'do', 'else', 'elseif', 'end',
  'false', 'for', 'function', 'get', 'if', 'import', 'in', 'make', 'nil',
  'not', 'or', 'pack', 'repeat', 'return', 'run', 'then', 'true', 'until', 'while'
]);

const BUILTINS = new Set([
  'print', 'tostring', 'tonumber', 'type', 'ipairs', 'pairs', 'next', 'select',
  'error', 'assert', 'pcall', 'xpcall', 'unpack', 'rawget', 'rawset',
  'rawequal', 'rawlen', 'collectgarbage', 'require', 'len', 'setmetatable',
  'getmetatable'
]);

const NAMESPACES = new Set([
  'string', 'table', 'math', 'io', 'os', 'bit32', 'buffer', 'task',
  'coroutine', 'list', 'net', 'window', 'discord', 'ai', 'json', 'libs'
]);

const DIRECTIVES = /^!(strict|karatsuba)\b/;

export function escapeHtml(value) {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#39;');
}

const TOKEN = /--\[\[[\s\S]*?\]\]|--[^\n]*|\[(=*)\[[\s\S]*?\]\1\]|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\b\d*\.?\d+(?:[eE][+-]?\d+)?\b|\b[A-Za-z_]\w*\b|\.{3}|\.\.|==|!=|<=|>=|\+=|-=|\*=|\/=|[+\-*/%^=<>#]|[{}()[\],;:.]/g;

function highlightLine(line, state) {
  if (state.inLongComment) {
    if (line.includes(']]')) state.inLongComment = false;
    return `<span class="tok-comment">${escapeHtml(line)}</span>`;
  }
  if (line.trimStart().startsWith('--[[') && !line.includes(']]')) {
    state.inLongComment = true;
    return `<span class="tok-comment">${escapeHtml(line)}</span>`;
  }

  let out = '';
  let last = 0;
  let match;
  TOKEN.lastIndex = 0;

  const directive = line.match(DIRECTIVES);
  let directiveEnd = 0;
  if (directive) {
    out += `<span class="tok-directive">${escapeHtml(directive[0])}</span>`;
    last = directive[0].length;
    directiveEnd = last;
  }

  while ((match = TOKEN.exec(line))) {
    if (match.index < directiveEnd) continue;
    if (match.index > last) out += escapeHtml(line.slice(last, match.index));

    const raw = match[0];
    let cls = '';
    let title = '';

    if (raw.startsWith('--')) {
      cls = 'tok-comment';
    } else if (raw.startsWith('"') || raw.startsWith("'") || raw.startsWith('[')) {
      cls = 'tok-string';
    } else if (/^\d/.test(raw)) {
      cls = 'tok-number';
    } else if (/^[A-Za-z_]/.test(raw)) {
      const prevWord = (line.slice(0, match.index).match(/([A-Za-z_]\w*)\s*$/s) || [])[1];
      const after = line.slice(match.index + raw.length);
      if (KEYWORDS.has(raw)) {
        cls = 'tok-keyword';
      } else if (BUILTINS.has(raw)) {
        cls = 'tok-builtin';
      } else if (NAMESPACES.has(raw) && after.startsWith('.') || NAMESPACES.has(raw) && after.startsWith(':')) {
        cls = 'tok-namespace';
      } else if (prevWord === 'function' || prevWord === 'command') {
        cls = 'tok-function';
        title = 'name of the thing';
      } else if (/^\s*\(/.test(after)) {
        cls = 'tok-function';
      } else if (raw === 'self') {
        cls = 'tok-self';
      }
    } else if (/^(==|~=|!=|<=|>=|\+=|-=|\*=|\/=|\.\.\.?|[+\-*/%^=<>])$/.test(raw)) {
      cls = 'tok-operator';
    } else {
      cls = 'tok-punct';
    }

    const attr = title ? ` title="${title}"` : '';
    out += cls
      ? `<span class="${cls}"${attr}>${escapeHtml(raw)}</span>`
      : escapeHtml(raw);
    last = match.index + raw.length;
  }

  if (last < line.length) out += escapeHtml(line.slice(last));
  return out;
}

export function highlightLuc(code) {
  const state = { inLongComment: false };
  return String(code ?? '')
    .replace(/\r\n/g, '\n')
    .split('\n')
    .map((line) => highlightLine(line, state))
    .join('\n');
}

export function highlight(code, lang = 'luc') {
  if (lang && /^(luc|lua)$/i.test(lang)) return highlightLuc(code);
  return escapeHtml(code);
}

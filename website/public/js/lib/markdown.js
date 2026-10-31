/* Minimal, safe Markdown renderer for package READMEs. */

import { escapeHtml, highlight } from './highlight.js';

function safeUrl(raw) {
  const url = String(raw || '').trim();
  if (/^(https?:\/\/|mailto:|\/|#)/i.test(url)) return escapeHtml(url);
  return '#';
}

function inline(input) {
  const codes = [];
  let text = String(input ?? '').replace(/`([^`]+)`/g, (_, code) => {
    codes.push(`<code>${escapeHtml(code)}</code>`);
    return `\u0000${codes.length - 1}\u0000`;
  });

  text = escapeHtml(text);

  text = text
    .replace(/!\[([^\]]*)\]\(([^)\s]+)(?:\s+&quot;[^&]*&quot;)?\)/g, (_, alt, url) =>
      `<img src="${safeUrl(url)}" alt="${escapeHtml(alt)}" loading="lazy" />`)
    .replace(/\[([^\]]+)\]\(([^)\s]+)(?:\s+&quot;[^&]*&quot;)?\)/g, (_, label, url) => {
      const href = safeUrl(url);
      const external = /^https?:\/\//i.test(url) ? ' target="_blank" rel="noopener noreferrer"' : '';
      return `<a href="${href}"${external}>${label}</a>`;
    })
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/__([^_]+)__/g, '<strong>$1</strong>')
    .replace(/(^|[\s(])\*([^*\n]+)\*/g, '$1<em>$2</em>')
    .replace(/(^|[\s(])_([^_\n]+)_/g, '$1<em>$2</em>')
    .replace(/~~([^~]+)~~/g, '<del>$1</del>');

  return text.replace(/\u0000(\d+)\u0000/g, (_, i) => codes[Number(i)]);
}

function tableBlock(lines) {
  const cells = (line) => line.replace(/^\s*\|/, '').replace(/\|\s*$/, '').split('|').map((c) => c.trim());
  const head = cells(lines[0]);
  const rows = lines.slice(2).map(cells);
  const align = cells(lines[1]).map((c) => {
    const left = c.startsWith(':');
    const right = c.endsWith(':');
    if (left && right) return 'center';
    if (right) return 'right';
    if (left) return 'left';
    return '';
  });
  const th = head.map((c, i) => `<th${align[i] ? ` style="text-align:${align[i]}"` : ''}>${inline(c)}</th>`).join('');
  const tr = rows.map((row) => `<tr>${head.map((_, i) => `<td${align[i] ? ` style="text-align:${align[i]}"` : ''}>${inline(row[i] || '')}</td>`).join('')}</tr>`).join('');
  return `<div class="md-table-wrap"><table><thead><tr>${th}</tr></thead><tbody>${tr}</tbody></table></div>`;
}

export function renderMarkdown(source) {
  const src = String(source || '').replace(/\r\n/g, '\n').trim();
  if (!src) return '';

  const lines = src.split('\n');
  const out = [];
  let i = 0;

  while (i < lines.length) {
    const line = lines[i];

    /* fenced code */
    const fence = line.match(/^\s*```(\w*)/);
    if (fence) {
      const lang = fence[1] || '';
      const buf = [];
      i++;
      while (i < lines.length && !/^\s*```/.test(lines[i])) {
        buf.push(lines[i]);
        i++;
      }
      i++; // closing fence
      const code = buf.join('\n');
      const body = highlight(code, lang);
      out.push(
        `<div class="md-code"><div class="md-code-bar"><span>${escapeHtml(lang || 'text')}</span></div><pre><code>${body}</code></pre></div>`
      );
      continue;
    }

    /* blank */
    if (!line.trim()) {
      i++;
      continue;
    }

    /* heading */
    const heading = line.match(/^(#{1,6})\s+(.*)$/);
    if (heading) {
      const level = heading[1].length;
      const id = heading[2]
        .toLowerCase()
        .replace(/[^\w\s-]/g, '')
        .trim()
        .replace(/\s+/g, '-');
      out.push(`<h${level} id="${escapeHtml(id)}">${inline(heading[2])}</h${level}>`);
      i++;
      continue;
    }

    /* horizontal rule */
    if (/^\s*([-*_])\s*\1\s*\1[\s\-*_]*$/.test(line)) {
      out.push('<hr />');
      i++;
      continue;
    }

    /* blockquote */
    if (/^\s*>/.test(line)) {
      const buf = [];
      while (i < lines.length && /^\s*>/.test(lines[i])) {
        buf.push(lines[i].replace(/^\s*>\s?/, ''));
        i++;
      }
      out.push(`<blockquote>${renderMarkdown(buf.join('\n'))}</blockquote>`);
      continue;
    }

    /* table */
    if (/\|/.test(line) && i + 1 < lines.length && /^\s*\|?[\s:|-]+\|[\s:|-]*$/.test(lines[i + 1])) {
      const buf = [];
      while (i < lines.length && /\|/.test(lines[i]) && lines[i].trim()) {
        buf.push(lines[i]);
        i++;
      }
      out.push(tableBlock(buf));
      continue;
    }

    /* lists */
    const listMatch = line.match(/^\s*([-*+]|\d+\.)\s+/);
    if (listMatch) {
      const ordered = /\d+\./.test(listMatch[1]);
      const items = [];
      let start = 1;
      if (ordered) start = Number(listMatch[1].replace('.', '')) || 1;
      while (i < lines.length) {
        const m = lines[i].match(/^\s*([-*+]|\d+\.)\s+(.*)$/);
        if (!m) break;
        let content = m[2];
        i++;
        while (i < lines.length && /^\s{2,}\S/.test(lines[i]) && !/^\s*([-*+]|\d+\.)\s+/.test(lines[i])) {
          content += `\n${lines[i].trim()}`;
          i++;
        }
        const task = content.match(/^\[( |x|X)\]\s+(.*)$/);
        if (task) {
          items.push(
            `<li class="task"><span class="task-box ${task[1].toLowerCase() === 'x' ? 'done' : ''}"></span>${inline(task[2])}</li>`
          );
        } else {
          items.push(`<li>${inline(content).replace(/\n/g, '<br />')}</li>`);
        }
      }
      out.push(ordered
        ? `<ol${start !== 1 ? ` start="${start}"` : ''}>${items.join('')}</ol>`
        : `<ul>${items.join('')}</ul>`);
      continue;
    }

    /* paragraph */
    const buf = [line];
    i++;
    while (
      i < lines.length &&
      lines[i].trim() &&
      !/^\s*(#{1,6}\s|>|```|\s*([-*+]|\d+\.)\s+|([-*_])\s*\3\s*\3)/.test(lines[i])
    ) {
      buf.push(lines[i]);
      i++;
    }
    out.push(`<p>${inline(buf.join(' '))}</p>`);
  }

  return out.join('\n');
}

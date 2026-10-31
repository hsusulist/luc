import { icon } from '../lib/icons.js';
import { codeBlock, commandBox, sectionHead, pageHero, observeReveal, toast } from '../lib/ui.js';

const RELEASE = 'https://github.com/hsusulist/luc/releases/download/v0.2.0-beta-1';

const COMPONENTS = [
  { id: 'window', label: 'window', desc: 'SDL2 graphics, input & sound' },
  { id: 'ai', label: 'ai', desc: 'Tensors, autograd & LM training' },
  { id: 'discord', label: 'discord', desc: 'Chat & slash-command bots' },
  { id: 'lcode', label: 'lcode', desc: 'The bundled code editor' },
  { id: 'vscode', label: 'vscode', desc: 'Syntax + snippets for VS Code' },
  { id: 'source', label: 'source', desc: 'Interpreter C sources' }
];

export function renderDownload() {
  return {
    title: 'Download LUC — Windows, Linux and npm',
    description: 'Install LUC 0.2.0-beta-1 with the Windows installer, the portable zip, npm or a build from source.',
    html: `
      ${pageHero({
        eyebrow: 'Download',
        title: 'Install once. <span class="grad">Start writing.</span>',
        lede: 'LUC ships as a single small interpreter. Pick the installer that matches your machine — everything below is version 0.2.0-beta-1.',
        meta: `
          <span>${icon('shield', { size: 14 })} Signed release assets</span>
          <span>${icon('scale', { size: 14 })} Apache 2.0</span>
          <span>${icon('windows', { size: 14 })} Windows 10+</span>
          <span>${icon('linux', { size: 14 })} Linux x64</span>`
      })}

      <section class="section-tight">
        <div class="container">
          <div class="download-grid">
            <article class="download-card is-primary reveal">
              <div class="download-card-top">
                <div>
                  <span class="download-icon">${icon('windows', { size: 22 })}</span>
                  <h3>Windows installer</h3>
                </div>
                <span class="pill pill-accent">${icon('star', { size: 12 })} recommended</span>
              </div>
              <p>The full setup: interpreter, PATH, and an optional component picker for window, AI, Discord and LCode.</p>
              <div class="download-meta"><span>v0.2.0-beta-1</span><span>9.2 MB</span><span>.exe</span></div>
              <a class="btn btn-primary" href="${RELEASE}/luc-installer.exe">${icon('download', { size: 16 })} Download installer</a>
            </article>

            <article class="download-card reveal" style="--d:60ms">
              <div class="download-card-top">
                <div>
                  <span class="download-icon">${icon('package', { size: 22 })}</span>
                  <h3>Windows portable</h3>
                </div>
              </div>
              <p>Unzip anywhere and run. No installer, no PATH changes, no admin rights — ideal for a USB stick or a locked-down machine.</p>
              <div class="download-meta"><span>v0.2.0-beta-1</span><span>17 MB</span><span>.zip</span></div>
              <a class="btn btn-ghost" href="${RELEASE}/luc-portable.zip">${icon('download', { size: 16 })} Download portable</a>
            </article>

            <article class="download-card reveal" style="--d:120ms">
              <div class="download-card-top">
                <div>
                  <span class="download-icon">${icon('terminal', { size: 22 })}</span>
                  <h3>npm launcher</h3>
                </div>
              </div>
              <p>One command for Windows or Linux. On Linux it compiles the interpreter from source during install.</p>
              <div class="download-meta"><span>@hsusulist/luc</span><span>0.3.0</span><span>requires Node 16+</span></div>
              <div class="commandbox" style="margin:0;width:100%">
                <div class="commandbox-row">
                  <span class="commandbox-prompt">$</span>
                  <code>npm install -g @hsusulist/luc</code>
                  <button class="copy-btn copy-btn-ghost" data-copy data-copy-text="npm install -g @hsusulist/luc" type="button">${icon('copy', { size: 14 })}<span>Copy</span></button>
                </div>
              </div>
            </article>

            <article class="download-card reveal" style="--d:180ms">
              <div class="download-card-top">
                <div>
                  <span class="download-icon">${icon('linux', { size: 22 })}</span>
                  <h3>Linux from source</h3>
                </div>
              </div>
              <p>Install GCC or Clang and build the three C files. No package manager required, no external libraries beyond libm and libdl.</p>
              <div class="download-meta"><span>src/*.c</span><span>C99</span><span>~10 seconds</span></div>
              <a class="btn btn-ghost" href="https://github.com/hsusulist/luc/tree/main/src" target="_blank" rel="noopener noreferrer">${icon('code', { size: 16 })} Get the sources</a>
            </article>
          </div>
          <p class="download-note">${icon('info', { size: 13 })} The portable archive and installer are signed and published on GitHub Releases. Checksums and verification steps are below.</p>
        </div>
      </section>

      <section class="section">
        <div class="container">
          ${sectionHead({
            eyebrow: 'Terminal installer',
            title: 'Prefer PowerShell? Same installer.',
            lede: 'The setup script downloads the current release, verifies it, and opens the same interactive component picker.'
          })}
          <div class="split">
            <div>
              ${commandBox('Invoke-WebRequest https://raw.githubusercontent.com/hsusulist/luc/main/install.ps1 -OutFile .\\install.ps1', { label: 'Step 1 — fetch the script', note: 'Windows PowerShell 5.1 or PowerShell 7.' })}
              ${commandBox('powershell -NoProfile -ExecutionPolicy Bypass -File .\\install.ps1', { label: 'Step 2 — run it' })}
              <div class="callout callout-tip">
                ${icon('shield', { size: 16 })}
                <div>Every asset is verified with SHA-256 and an Ed25519 signature (minisign format) before it is unpacked. Read the <a href="https://github.com/hsusulist/luc/blob/main/install.ps1" target="_blank" rel="noopener noreferrer">installer source</a> if you like to check first.</div>
              </div>
            </div>
            <div class="reveal" style="--d:80ms">
              ${codeBlock(`# Install the core plus the window library, unattended
luc --install -c window -d "D:\\Tools\\LUC" -y

# Add the AI and Discord components too
luc --install -c window,ai,discord -y

# No PATH changes, no environment changes
luc --install --no-path --no-env -y

# Verify, then start the REPL
luc --version
luc`, { filename: 'PowerShell', lang: 'powershell', lineNumbers: true })}
            </div>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container">
          ${sectionHead({
            eyebrow: 'Build your install',
            title: 'Pick only what you need.',
            lede: 'The core interpreter is always installed. Tick a few components and copy the exact unattended command.'
          })}

          <div class="installer-builder reveal">
            <div>
              <div class="component-list">
                ${COMPONENTS.map((component, index) => `
                  <label class="component">
                    <input type="checkbox" value="${component.id}" ${index < 2 ? 'checked' : ''} data-component />
                    <span class="component-box">${icon('check', { size: 11 })}</span>
                    <span class="component-text">
                      <strong>${component.label}</strong>
                      <span>${component.desc}</span>
                    </span>
                  </label>`).join('')}
              </div>
              <div class="builder-field" style="margin-top:14px">
                <label for="install-dir">Install folder</label>
                <input id="install-dir" type="text" value="C:\\Program Files\\LUC" data-dir spellcheck="false" />
              </div>
              <label class="component" style="background:transparent">
                <input type="checkbox" data-path checked />
                <span class="component-box">${icon('check', { size: 11 })}</span>
                <span class="component-text">
                  <strong>Add to PATH</strong>
                  <span>Uncheck to pass --no-path</span>
                </span>
              </label>
            </div>

            <div class="builder-output">
              <div>
                <div class="builder-field">
                  <label>Your command</label>
                </div>
                <code id="builder-command">luc --install -c window,ai -d "C:\\Program Files\\LUC" -y</code>
                <p class="builder-note">Want the prompts instead? Run <code>luc --install</code> with no flags.</p>
              </div>
              <button class="copy-btn" type="button" id="builder-copy" data-copy data-copy-text="">${icon('copy', { size: 14 })}<span>Copy command</span></button>
            </div>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container">
          ${sectionHead({
            eyebrow: 'Verify & install details',
            title: 'Trust, but check the checksum.',
            lede: 'Releases are signed. Here is exactly how to confirm the archive you downloaded matches the one we published.'
          })}

          <div class="verify-grid">
            <div class="verify-card reveal">
              <h3>${icon('shield', { size: 16 })} SHA-256 checksums</h3>
              <p>Compare the hash with the published <code>.sha256</code> file before running anything.</p>
              ${codeBlock(`Get-FileHash .\\luc-portable.zip -Algorithm SHA256`, { lang: 'powershell', filename: 'PowerShell', lineNumbers: false })}
              <div class="download-meta" style="margin-bottom:0">
                <span>luc-portable: ab1fa6f0…fcb4e</span>
                <span>lcode-portable: 5be0426b…22d5</span>
              </div>
            </div>
            <div class="verify-card reveal" style="--d:70ms">
              <h3>${icon('check', { size: 16 })} minisign signature</h3>
              <p>The public key ships with the project. Verify the archive and its checksum file.</p>
              ${codeBlock(`minisign -Vm .\\luc-portable.zip -p minisign.pub`, { lang: 'powershell', filename: 'PowerShell', lineNumbers: false })}
              <div class="download-meta" style="margin-bottom:0">
                <span>key: 4f2c99cb76bc2ac2</span>
              </div>
            </div>
          </div>

          <div class="bento" style="margin-top:16px">
            <article class="bento-card span-6 reveal">
              <span class="card-icon">${icon('terminal', { size: 20 })}</span>
              <h3>After installing</h3>
              <p>Check the version, open the REPL, or run a file. The first launch also opens the interactive installer if the runtime is incomplete.</p>
              ${commandBox('luc --version', { label: 'Version' })}
              ${commandBox('luc', { label: 'REPL', note: 'Type help, credit, license or exit inside the prompt.' })}
            </article>
            <article class="bento-card span-6 reveal" style="--d:70ms">
              <span class="card-icon violet">${icon('refresh', { size: 20 })}</span>
              <h3>Update or remove</h3>
              <p>Run the installer again to repair or add components. Uninstall keeps your project files and libraries exactly where they are.</p>
              ${commandBox('luc --install', { label: 'Repair / add components' })}
              ${commandBox('luc uninstall', { label: 'Uninstall', note: 'Use -y to confirm, -n to cancel.' })}
            </article>
          </div>
        </div>
      </section>

      <section class="section">
        <div class="container">
          ${sectionHead({
            eyebrow: 'Editors',
            title: 'Bring your own editor, or use ours.',
            lede: 'LCode is a small project editor bundled with LUC. Syntax highlighting and snippets are also available for VS Code.'
          })}
          <div class="download-grid">
            <article class="download-card reveal">
              <div class="download-card-top">
                <div>
                  <span class="download-icon">${icon('code', { size: 22 })}</span>
                  <h3>LCode</h3>
                </div>
                <span class="pill">standalone</span>
              </div>
              <p>A terminal editor with a file tree, find across files, a bottom shell and F5 to run. Ships with LUC, also installable alone.</p>
              <div class="download-meta"><span>lcode-portable.zip</span><span>1.9 MB</span><span>npm: @hsusulist/lcode</span></div>
              <div class="hero-actions" style="margin-top:4px">
                <a class="btn btn-ghost btn-sm" href="${RELEASE}/lcode-portable.zip" download>${icon('download', { size: 15 })} Download</a>
                <a class="btn btn-ghost btn-sm" href="https://www.npmjs.com/package/@hsusulist/lcode" target="_blank" rel="noopener noreferrer">${icon('external', { size: 15 })} npm</a>
              </div>
            </article>
            <article class="download-card reveal" style="--d:70ms">
              <div class="download-card-top">
                <div>
                  <span class="download-icon">${icon('layers', { size: 22 })}</span>
                  <h3>VS Code support</h3>
                </div>
                <span class="pill">syntax + snippets</span>
              </div>
              <p>TextMate grammar, file icons, bracket rules and snippets for <code>create</code>, <code>command</code>, <code>repeat</code>, <code>pack</code> and more.</p>
              <div class="download-meta"><span>vscode/</span><span>grammar + snippets</span><span>Apache 2.0</span></div>
              <a class="btn btn-ghost btn-sm" href="https://github.com/hsusulist/luc/tree/main/vscode" target="_blank" rel="noopener noreferrer">${icon('github', { size: 15 })} Get the extension</a>
            </article>
          </div>

          <div style="margin-top:26px">
            <h3 style="font-size:16px;margin-bottom:12px">Build from source (Linux)</h3>
            ${codeBlock(`# Debian / Ubuntu
sudo apt install build-essential

# Build the interpreter
cc -O2 -std=gnu99 -o luc \\
  src/luc_core.c src/luc_libs.c src/luc_trans.c \\
  -lm -ldl

./luc --version
./luc hello.luc`, { filename: 'bash', lang: 'bash', lineNumbers: true })}
            <p class="download-note">The Linux backend includes the terminal LCode editor. The SDL2 window library and the Windows x64 JIT are part of the Windows build.</p>
          </div>
        </div>
      </section>
    `,
    after() {
      observeReveal();

      const output = document.getElementById('builder-command');
      const copy = document.getElementById('builder-copy');
      const dir = document.querySelector('[data-dir]');
      const pathToggle = document.querySelector('[data-path]');
      const boxes = [...document.querySelectorAll('[data-component]')];
      if (!output || !copy) return;

      const update = () => {
        const selected = boxes.filter((box) => box.checked).map((box) => box.value);
        const parts = ['luc --install'];
        if (selected.length) parts.push(`-c ${selected.join(',')}`);
        const folder = (dir?.value || '').trim();
        if (folder) parts.push(`-d "${folder}"`);
        if (pathToggle && !pathToggle.checked) parts.push('--no-path');
        parts.push('-y');
        const command = parts.join(' ');
        output.textContent = command;
        copy.setAttribute('data-copy-text', command);
      };

      boxes.forEach((box) => box.addEventListener('change', update));
      dir?.addEventListener('input', update);
      pathToggle?.addEventListener('change', update);
      copy.addEventListener('click', () => {
        setTimeout(() => toast('Installer command copied. Unattended — no prompts.'), 10);
      });
      update();
    }
  };
}

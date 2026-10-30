import { Component, inject } from '@angular/core';
import { RouterLink } from '@angular/router';
import { MatTabsModule } from '@angular/material/tabs';
import { MatButtonModule } from '@angular/material/button';
import { REPO_URL, Seo } from '../seo';
import { SITE_INFO } from '../../generated/site';

const v = SITE_INFO.version;
const release = `${REPO_URL}/releases/download/v${v}`;

@Component({
  selector: 'app-download',
  imports: [RouterLink, MatTabsModule, MatButtonModule],
  template: `
    <div class="page">
      <h1>Download Dessa {{ version }}</h1>
      <p>
        Dessa turns programs into C, so it needs gcc and g++ 13 or newer on the computer
        (MinGW on Windows). The installer checks for them, installs <code>dessa</code> and <code>riv</code>,
        and puts them on your PATH.
      </p>

      <mat-tab-group animationDuration="0ms" mat-stretch-tabs="false">
        @for (os of systems; track os.name) {
          <mat-tab [label]="os.name">
            <div class="tab">
              <a mat-flat-button [href]="os.url">Download {{ os.file }}</a>
              <p>Unpack it, then run this in the unpacked folder:</p>
              <pre class="code"><code>{{ os.install }}</code></pre>
              @if (os.note) { <p class="muted small">{{ os.note }}</p> }
            </div>
          </mat-tab>
        }
      </mat-tab-group>

      <p>Check that it works:</p>
      <pre class="code"><code>dessa --version
dessa run hello.ds</code></pre>
      <p>
        The full steps for every system, building from source and uninstalling are in
        <a routerLink="/docs/installation">Installing Dessa</a>.
      </p>

      <h2>Other downloads</h2>
      <div class="table-wrap">
        <table>
          <thead><tr><th>What</th><th>How to get it</th></tr></thead>
          <tbody>
            <tr>
              <td>Node only (no C compiler)</td>
              <td>
                <code>npm install dessalang</code>, then <code>npx dessalang run app.ds</code>: programs and pages compiled to WebAssembly.
                See <a href="https://www.npmjs.com/package/dessalang" rel="noopener">npmjs.com/package/dessalang</a>.
              </td>
            </tr>
            <tr>
              <td>VS Code extension</td>
              <td>
                <a [href]="release + '/dessa-vscode-' + version + '.vsix'">dessa-vscode-{{ version }}.vsix</a>.
                In VS Code: Extensions, the ... menu, Install from VSIX. See <a routerLink="/docs/installation" fragment="vs-code">VS Code</a>.
              </td>
            </tr>
            <tr>
              <td>WebAssembly compiler</td>
              <td>The installer gets it (about 50 MB, once). Otherwise run <code>dessa setup wasm</code>; web pages need it.</td>
            </tr>
            <tr>
              <td>Older versions</td>
              <td><a [href]="repo + '/releases'" rel="noopener">All releases on GitHub</a></td>
            </tr>
          </tbody>
        </table>
      </div>

      <h2>Updating</h2>
      <pre class="code"><code>riv update</code></pre>
      <p>Downloads the newest release and installs it over the old one.</p>

      <h2>Building from source</h2>
      <pre class="code"><code>git clone {{ repo }}.git
cd Dessa
cmake -S . -B build
cmake --build build
cmake --build build --target riv</code></pre>
    </div>
  `,
  styles: `
    .tab { padding: 16px 0 4px; }
  `,
})
export class Download {
  readonly version = v;
  readonly repo = REPO_URL;
  readonly release = release;
  readonly systems = [
    {
      name: 'Windows',
      file: `dessa-${v}-windows-x64.zip`,
      install: 'powershell -ExecutionPolicy Bypass -File install.ps1',
      note: 'The installer is a wizard. If g++ or CMake is missing, it offers to install it with winget.',
    },
    {
      name: 'Linux',
      file: `dessa-${v}-linux-x64.tar.gz`,
      install: 'sh install.sh',
      note: 'Install a compiler first, for example on Ubuntu: sudo apt install g++ cmake make',
    },
    {
      name: 'macOS',
      file: `dessa-${v}-macos-arm64.tar.gz`,
      install: 'sh install.sh',
      note: 'For Apple silicon. Needs the Xcode command line tools: xcode-select --install',
    },
  ].map((os) => ({ ...os, url: `${release}/${os.file}` }));

  constructor() {
    inject(Seo).set({
      title: `Download Dessa ${v} for Windows, Linux and macOS`,
      description: `Download Dessa ${v}: installers for Windows, Linux and macOS, the npm package dessalang for WebAssembly from Node, and the VS Code extension.`,
      path: '/download',
    });
  }
}

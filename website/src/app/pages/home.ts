import { Component, inject } from '@angular/core';
import { RouterLink } from '@angular/router';
import { MatButtonModule } from '@angular/material/button';
import { DomSanitizer } from '@angular/platform-browser';
import { BarChart, Bar } from '../bar-chart';
import { REPO_URL, SITE_URL, Seo } from '../seo';
import { SITE_INFO } from '../../generated/site';
import { BENCH } from '../../generated/bench';
import { COVERAGE } from '../../generated/coverage';
import { benchTotals } from './benchmarks';

@Component({
  selector: 'app-home',
  imports: [RouterLink, MatButtonModule, BarChart],
  template: `
    <div class="page">
      <section class="intro">
        <img class="logo" src="/logo.svg" alt="The Dessa turtle" width="112" height="112" />
        <h1>Dessa</h1>
        <p class="lead">
          A statically typed programming language that compiles to C for fast native programs
          and to WebAssembly for web pages. The goal is C++ speed without C++'s learning curve.
        </p>
        <div class="actions">
          <a mat-flat-button routerLink="/download">Download {{ version }}</a>
          <a mat-stroked-button routerLink="/docs/language">Read the language guide</a>
          <a mat-button [href]="repo" rel="noopener">Source on GitHub</a>
        </div>
      </section>

      <section class="facts" aria-label="Dessa in numbers">
        @for (fact of facts; track fact.label) {
          <div class="fact">
            <span class="fact-value">{{ fact.value }}</span>
            <span class="fact-label">{{ fact.label }}</span>
          </div>
        }
      </section>

      <section class="split">
        <div>
          <h2>What it looks like</h2>
          <pre class="code"><code [innerHTML]="sample"></code></pre>
          <p class="small muted">
            Output: <code>2 items, total 175.49</code>. More programs on the <a routerLink="/examples">examples</a> page.
          </p>
        </div>
        <div>
          <h2>Features</h2>
          <ul>
            @for (feature of features; track $index) {
              <li [innerHTML]="feature"></li>
            }
          </ul>
        </div>
      </section>

      <section>
        <h2>Speed</h2>
        <p>
          The same eight tasks in each language, total time in milliseconds (lower is faster).
          Measured {{ bench.date }}.
        </p>
        <app-bar-chart [rows]="totals" caption="All benchmark tasks together" />
        <p><a routerLink="/benchmarks">Every task, the versions used and how it was measured</a></p>
      </section>

      <section>
        <h2>Quick start</h2>
        <pre class="code"><code>dessa run hello.ds                 compile and run
dessa build hello.ds               make hello.exe (or hello)
dessa check hello.ds               errors only
dessa run page.dhtml               a web page written in Dessa, opened in the browser

riv init myapp                     a new project
riv add sql                        add a package
riv run</code></pre>
        <p>Install it from the <a routerLink="/download">download page</a>, or with <code>npm install dessalang</code> to build WebAssembly from Node.</p>
      </section>

      <section>
        <h2>Documentation</h2>
        <div class="table-wrap">
          <table>
            <tbody>
              @for (doc of docs; track doc.slug) {
                <tr>
                  <td><a [routerLink]="['/docs', doc.slug]">{{ doc.title }}</a></td>
                  <td class="muted">{{ doc.description }}</td>
                </tr>
              }
            </tbody>
          </table>
        </div>
      </section>
    </div>
  `,
  styles: `
    .intro { padding: 24px 0 8px; display: grid; grid-template-columns: auto 1fr; column-gap: 24px; align-items: center; }
    .intro h1 { font-size: 2.8rem; margin: 0; color: var(--mat-sys-primary); }
    .intro .lead, .intro .actions { grid-column: 1 / -1; }
    .facts { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 1px; background: var(--line); border: 1px solid var(--line); border-radius: 8px; overflow: hidden; margin: 28px 0 8px; }
    .fact { background: var(--mat-sys-surface-container-low); padding: 14px 16px; display: flex; flex-direction: column; gap: 2px; }
    .fact-value { font-size: 1.35rem; font-weight: 700; color: var(--mat-sys-primary); }
    .fact-label { font-size: 0.875rem; color: var(--text-muted); }
    @media (max-width: 720px) { .facts { grid-template-columns: repeat(2, minmax(0, 1fr)); } .intro .logo { width: 72px; height: 72px; } }
    .lead { font-size: 1.2rem; max-width: 720px; }
    .actions { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 20px; }
    .split { display: grid; grid-template-columns: minmax(0, 1.2fr) minmax(0, 1fr); gap: 32px; }
    .split ul { padding-left: 20px; }
    @media (max-width: 860px) { .split { grid-template-columns: minmax(0, 1fr); gap: 0; } }
  `,
})
export class Home {
  readonly version = SITE_INFO.version;
  readonly repo = REPO_URL;
  readonly docs = SITE_INFO.docs;
  readonly bench = BENCH;
  readonly totals: Bar[] = benchTotals();
  readonly facts = [
    { value: this.totals.find((b) => b.label === 'Dessa')?.note?? '', label: 'native, total benchmark time' },
    { value: this.totals.find((b) => b.label === 'Dessa (wasm)')?.note?? '', label: 'as WebAssembly in a page' },
    { value: String(COVERAGE.features.length), label: 'feature tests in every backend' },
    { value: '3', label: 'systems: Windows, Linux, macOS' },
  ];
  private readonly sanitizer = inject(DomSanitizer);
  readonly sample = this.sanitizer.bypassSecurityTrustHtml(SITE_INFO.sample);
  readonly features = SITE_INFO.features.map((f) => this.sanitizer.bypassSecurityTrustHtml(f));

  constructor() {
    const description =
      'Dessa is a statically typed programming language that compiles to C for native programs and to WebAssembly for web pages. C++ speed without C++\'s learning curve.';
    inject(Seo).set({
      title: 'Dessa programming language',
      description,
      path: '/',
      jsonLd: [
        { '@type': 'WebSite', name: 'Dessa', url: SITE_URL + '/', description },
        {
          '@type': 'SoftwareApplication',
          name: 'Dessa',
          applicationCategory: 'DeveloperApplication',
          applicationSubCategory: 'Programming language',
          operatingSystem: 'Windows, Linux, macOS',
          softwareVersion: SITE_INFO.version,
          description,
          url: SITE_URL + '/',
          downloadUrl: SITE_URL + '/download/',
          license: 'https://www.apache.org/licenses/LICENSE-2.0',
          codeRepository: REPO_URL,
          offers: { '@type': 'Offer', price: '0', priceCurrency: 'USD' },
          author: { '@type': 'Person', name: 'd-ustindwayne', url: 'https://dustindwayn.ee/' },
        },
        {
          '@type': 'ComputerLanguage',
          name: 'Dessa',
          url: SITE_URL + '/',
          description,
        },
      ],
    });
  }
}

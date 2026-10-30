import { Component, inject } from '@angular/core';
import { RouterLink } from '@angular/router';
import { BarChart, Bar } from '../bar-chart';
import { REPO_URL, Seo } from '../seo';
import { COVERAGE } from '../../generated/coverage';

const BACKENDS = [
  { key: 'native', label: 'Native', about: 'the default: through C (C++ only if that C ever failed to compile)' },
  { key: 'c', label: 'C only', about: '--target c: through C and nothing else' },
  { key: 'wasm', label: 'WebAssembly', about: '--target wasm (what pages use), run with Node' },
] as const;

type Result = { status: string; reason?: string } | undefined;

@Component({
  selector: 'app-features',
  imports: [BarChart, RouterLink],
  template: `
    <div class="page">
      <h1>Features and backends</h1>
      <p>
        Every feature of the language has a test program in
        <a [href]="repo + '/tree/main/tests/cases/run'" rel="noopener">tests/cases/run</a>.
        The test suite runs each one through every backend and compares the output with the expected output line by line.
        This page shows what each backend can do today, from the test run of {{ coverage.date }}.
      </p>

      <app-bar-chart [rows]="summary" unit="" [legend]="false" caption="Test programs that pass, out of {{ total }}" />

      <h2>Backends</h2>
      <ul>
        @for (b of backends; track b.key) {
          <li><strong>{{ b.label }}</strong>: {{ b.about }}</li>
        }
      </ul>
      <p>
        A skipped test is something that can't run there
        (desktop windows in WebAssembly, or a SQL Server that isn't there). The reason is shown next to it.
        There are also {{ coverage.errorTests }} tests of compile errors, which check that wrong programs
        get the right message. See <a routerLink="/docs/compiler">Using the compiler</a> for the targets.
      </p>

      <h2>Every test</h2>
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Feature</th>
              @for (b of backends; track b.key) { <th>{{ b.label }}</th> }
            </tr>
          </thead>
          <tbody>
            @for (f of features; track f.name) {
              <tr>
                <td>
                  {{ f.label }}<br />
                  <a class="small" [href]="repo + '/blob/main/' + f.file" rel="noopener">{{ f.name }}.ds</a>
                </td>
                @for (b of backends; track b.key) {
                  @let r = result(f.modes, b.key);
                  <td class="status" [class]="r?.status ?? 'none'">
                    @switch (r?.status) {
                      @case ('pass') { <span>&#10003; Pass</span> }
                      @case ('fail') { <span>&#10007; Fails</span> }
                      @case ('skip') { <span>Skipped</span><br /><span class="small muted">{{ r?.reason }}</span> }
                      @default { <span class="muted">Not run</span> }
                    }
                  </td>
                }
              </tr>
            }
          </tbody>
        </table>
      </div>
    </div>
  `,
  styles: `
    td.status { min-width: 120px; }
    td.pass span:first-child { color: var(--status-pass); }
    td.fail span:first-child { color: var(--status-fail); font-weight: 600; }
  `,
})
export class Features {
  readonly repo = REPO_URL;
  readonly coverage = COVERAGE;
  readonly backends = BACKENDS;
  readonly features = COVERAGE.features;
  readonly total = COVERAGE.features.length;
  readonly summary: Bar[] = BACKENDS.map((b) => ({
    label: b.label,
    value: COVERAGE.features.filter((f) => this.result(f.modes, b.key)?.status === 'pass').length,
    highlight: true,
  }));

  result(modes: object, key: string): Result {
    return (modes as Record<string, Result>)[key];
  }

  constructor() {
    const pass = (key: string) => this.summary.find((s) => s.label === BACKENDS.find((b) => b.key === key)!.label)!.value;
    inject(Seo).set({
      title: 'Features and backend support',
      description: `Which Dessa features work natively, through C and in WebAssembly. ${this.total} test programs: ${pass('native')} pass natively, ${pass('wasm')} in WebAssembly.`,
      path: '/features',
    });
  }
}

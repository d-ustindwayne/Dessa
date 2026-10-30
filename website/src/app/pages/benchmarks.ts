import { Component, computed, inject, signal } from '@angular/core';
import { MatChipsModule } from '@angular/material/chips';
import { DomSanitizer } from '@angular/platform-browser';
import { BarChart, Bar } from '../bar-chart';
import { REPO_URL, Seo } from '../seo';
import { BENCH } from '../../generated/bench';

const isDessa = (language: string) => language.startsWith('Dessa');

function bars(values: number[]): Bar[] {
  const cpp = values[BENCH.languages.indexOf('C++')];
  return BENCH.languages
    .map((label, i) => ({
      label,
      value: values[i],
      highlight: isDessa(label),
      note: cpp ? `${(values[i] / cpp).toFixed(1)}x C++` : undefined,
    }))
    .sort((a, b) => a.value - b.value);
}

/** The total of every task for each language, fastest first. */
export function benchTotals(): Bar[] {
  return bars(BENCH.languages.map((_, i) => BENCH.tasks.reduce((sum, t) => sum + t.ms[i], 0)));
}

@Component({
  selector: 'app-benchmarks',
  imports: [BarChart, MatChipsModule],
  template: `
    <div class="page">
      <h1>Benchmarks</h1>
      <p>
        Eight small programs, written the same way in C++, Java, JavaScript, Python and Dessa.
        Every language runs the same algorithm and prints the same result. Times are milliseconds,
        the best of 3 runs, lower is faster.
      </p>
      <p class="muted small">Measured {{ bench.date }} on {{ bench.machine }}.</p>

      <app-bar-chart [rows]="totals" caption="All tasks together" />

      <h2>One task at a time</h2>
      <mat-chip-listbox [value]="task()" (change)="$event.value && task.set($event.value)" aria-label="Task" hideSingleSelectionIndicator>
        @for (t of bench.tasks; track t.name) {
          <mat-chip-option [value]="t.name">{{ t.name }}</mat-chip-option>
        }
      </mat-chip-listbox>
      <p>{{ current().about }}</p>
      <app-bar-chart [rows]="taskBars()" [caption]="current().name" />

      <h2>All numbers</h2>
      <div class="table-wrap">
        <table>
          <thead>
            <tr>
              <th>Task</th>
              @for (l of bench.languages; track l) { <th class="num">{{ l }}</th> }
            </tr>
          </thead>
          <tbody>
            @for (t of bench.tasks; track t.name) {
              <tr>
                <td>{{ t.name }}</td>
                @for (ms of t.ms; track $index) { <td class="num">{{ ms.toLocaleString('en-US') }}</td> }
              </tr>
            }
            <tr class="total">
              <td>all tasks</td>
              @for (ms of totalRow; track $index) { <td class="num">{{ ms.toLocaleString('en-US') }}</td> }
            </tr>
          </tbody>
        </table>
      </div>

      <h2>The tasks</h2>
      <div class="table-wrap">
        <table>
          <tbody>
            @for (t of bench.tasks; track t.name) {
              <tr><td><strong>{{ t.name }}</strong></td><td>{{ t.about }}</td></tr>
            }
          </tbody>
        </table>
      </div>

      <h2>Versions</h2>
      <ul>
        @for (v of bench.versions; track v.language) {
          <li><strong>{{ v.language }}</strong>: {{ v.version }}</li>
        }
      </ul>

      <h2>Notes</h2>
      <div [innerHTML]="notes"></div>

      <h2>Running it yourself</h2>
      <pre class="code"><code>python tests/bench/run_bench.py</code></pre>
      <p>
        The programs are in <a [href]="repo + '/tree/main/tests/bench'" rel="noopener">tests/bench</a>.
        A full run takes about four minutes and writes RESULTS.md and results.csv.
      </p>
    </div>
  `,
  styles: `
    mat-chip-listbox { display: block; margin: 4px 0 8px; }
    tr.total td { font-weight: 600; }
  `,
})
export class Benchmarks {
  readonly bench = BENCH;
  readonly repo = REPO_URL;
  readonly totals = benchTotals();
  readonly totalRow = BENCH.languages.map((_, i) => BENCH.tasks.reduce((sum, t) => sum + t.ms[i], 0));
  readonly notes = inject(DomSanitizer).bypassSecurityTrustHtml(BENCH.notes);
  readonly task = signal(BENCH.tasks[0].name);
  readonly current = computed(() => BENCH.tasks.find((t) => t.name === this.task())!);
  readonly taskBars = computed(() => bars(this.current().ms));

  constructor() {
    const total = this.totals.find((b) => b.label === 'Dessa');
    inject(Seo).set({
      title: 'Benchmarks: Dessa vs C++, Java, JavaScript and Python',
      description: `Dessa against C++, Java, JavaScript and Python on eight tasks. Native Dessa ran all of them in ${total?.value.toLocaleString('en-US')} ms, ${total?.note}.`,
      path: '/benchmarks',
      jsonLd: [
        {
          '@type': 'Dataset',
          name: 'Dessa benchmark results',
          description: 'Run times in milliseconds of eight tasks in C++, Java, JavaScript, Python and Dessa.',
          url: 'https://dessa.dustindwayn.ee/benchmarks/',
          dateModified: BENCH.date.slice(0, 10),
          license: 'https://www.apache.org/licenses/LICENSE-2.0',
          distribution: {
            '@type': 'DataDownload',
            encodingFormat: 'text/csv',
            contentUrl: `${REPO_URL}/blob/main/tests/bench/results.csv`,
          },
        },
      ],
    });
  }
}

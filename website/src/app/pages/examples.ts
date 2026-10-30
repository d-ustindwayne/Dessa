import { Component, inject } from '@angular/core';
import { RouterLink } from '@angular/router';
import { MatExpansionModule } from '@angular/material/expansion';
import { DomSanitizer } from '@angular/platform-browser';
import { REPO_URL, Seo } from '../seo';
import { EXAMPLES } from '../../generated/examples';

@Component({
  selector: 'app-examples',
  imports: [RouterLink, MatExpansionModule],
  template: `
    <div class="page">
      <h1>Examples</h1>
      <p>
        These programs come from the test suite, so the output shown under each one is exactly what
        Dessa prints, natively and as WebAssembly. Run one with <code>dessa run file.ds</code>.
        The whole language is described in <a routerLink="/docs/language">the language guide</a>.
      </p>

      <mat-accordion multi>
        @for (e of examples; track e.name; let first = $first) {
          <mat-expansion-panel [expanded]="first">
            <mat-expansion-panel-header>
              <mat-panel-title>{{ e.title }}</mat-panel-title>
              <mat-panel-description>{{ e.name }}.ds</mat-panel-description>
            </mat-expansion-panel-header>
            <pre class="code"><code [innerHTML]="e.code"></code></pre>
            <p class="small"><strong>Output</strong></p>
            <pre class="code out"><code [innerHTML]="e.output"></code></pre>
            <a class="small" [href]="repo + '/blob/main/' + e.file" rel="noopener">{{ e.file }} on GitHub</a>
          </mat-expansion-panel>
        }
      </mat-accordion>

      <h2>A web page in Dessa</h2>
      <p>
        In a <code>.dhtml</code> page a plain <code>&lt;script&gt;</code> is Dessa. <code>dessa build counter.dhtml</code>
        writes counter.html, which runs the script as WebAssembly. See <a routerLink="/docs/web">Dessa on the web</a>.
      </p>
      <pre class="code"><code [innerHTML]="page"></code></pre>
    </div>
  `,
  styles: `
    .out { max-height: 360px; }
    mat-expansion-panel { margin-bottom: 8px; }
    @media (max-width: 600px) { mat-panel-description { display: none; } }
  `,
})
export class Examples {
  readonly repo = REPO_URL;
  private readonly sanitizer = inject(DomSanitizer);
  readonly examples = EXAMPLES.list.map((e) => ({
    ...e,
    code: this.sanitizer.bypassSecurityTrustHtml(e.code),
    output: this.sanitizer.bypassSecurityTrustHtml(e.output),
  }));
  readonly page = this.sanitizer.bypassSecurityTrustHtml(EXAMPLES.page);

  constructor() {
    inject(Seo).set({
      title: 'Dessa code examples',
      description:
        'Dessa programs with their output: variables and types, nil safety, functions and lambdas, classes, generics, maps, errors, Unicode text and a web page.',
      path: '/examples',
    });
  }
}

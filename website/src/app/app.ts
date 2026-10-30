import { Component } from '@angular/core';
import { RouterLink, RouterLinkActive, RouterOutlet } from '@angular/router';
import { MatToolbarModule } from '@angular/material/toolbar';
import { MatButtonModule } from '@angular/material/button';
import { REPO_URL } from './seo';
import { SITE_INFO } from '../generated/site';

@Component({
  selector: 'app-root',
  imports: [RouterOutlet, RouterLink, RouterLinkActive, MatToolbarModule, MatButtonModule],
  template: `
    <a class="skip" href="#main">Skip to content</a>
    <mat-toolbar class="bar">
      <div class="bar-inner">
        <a class="brand" routerLink="/"><img src="/logo.svg" alt="" width="32" height="32" />Dessa</a>
        <nav aria-label="Main">
          @for (link of links; track link.path) {
            <a mat-button [routerLink]="link.path" routerLinkActive="active">{{ link.label }}</a>
          }
          <a mat-button [href]="repo" rel="noopener">GitHub</a>
        </nav>
      </div>
    </mat-toolbar>
    <main id="main">
      <router-outlet />
    </main>
    <footer>
      <div class="foot-inner">
        <span class="foot-brand"><img src="/logo.svg" alt="" width="20" height="20" />Dessa {{ version }}. Apache 2.0 license.</span>
        <span>
          <a [href]="repo" rel="noopener">Source</a> &middot;
          <a [href]="repo + '/releases'" rel="noopener">Releases</a> &middot;
          <a href="https://www.npmjs.com/package/dessalang" rel="noopener">npm</a> &middot;
          <a [href]="repo + '/issues'" rel="noopener">Issues</a> &middot;
          <a href="https://dustindwayn.ee/">dustindwayn.ee</a>
        </span>
      </div>
    </footer>
  `,
  styles: `
    :host { display: flex; flex-direction: column; min-height: 100vh; }
    .skip { position: absolute; left: -9999px; }
    .skip:focus { left: 16px; top: 8px; z-index: 10; background: var(--mat-sys-surface); padding: 8px; }
    :host::before { content: ""; display: block; height: 3px; background: linear-gradient(90deg, #14743c, #1c614d 50%, #d2cb71); }
    .bar { border-bottom: 1px solid var(--line); background: var(--mat-sys-surface); height: auto; min-height: 56px; padding: 0; }
    .bar-inner { max-width: var(--page-width); width: 100%; margin: 0 auto; padding: 4px 16px; display: flex; align-items: center; gap: 16px; flex-wrap: wrap; }
    .brand { display: inline-flex; align-items: center; gap: 8px; font-weight: 700; font-size: 1.3rem; text-decoration: none; color: var(--mat-sys-primary); margin-right: auto; }
    .foot-brand { display: inline-flex; align-items: center; gap: 8px; }
    nav { display: flex; flex-wrap: wrap; gap: 2px; }
    nav a.active { background: var(--mat-sys-secondary-container); }
    main { flex: 1; }
    footer { background: var(--mat-sys-surface-container-low); border-top: 1px solid var(--line); font-size: 0.875rem; color: var(--text-muted); }
    .foot-inner { max-width: var(--page-width); margin: 0 auto; padding: 20px 16px; display: flex; flex-wrap: wrap; gap: 8px 24px; justify-content: space-between; }
    @media (max-width: 720px) {
      .brand { width: 100%; }
      nav { margin: 0 -8px 4px; }
    }
  `,
})
export class App {
  readonly repo = REPO_URL;
  readonly version = SITE_INFO.version;
  readonly links = [
    { path: '/docs', label: 'Docs' },
    { path: '/playground', label: 'Playground' },
    { path: '/download', label: 'Download' },
    { path: '/benchmarks', label: 'Benchmarks' },
    { path: '/features', label: 'Features' },
    { path: '/examples', label: 'Examples' },
  ];
}

import { Component, computed, effect, inject, input } from '@angular/core';
import { Router, RouterLink, RouterLinkActive } from '@angular/router';
import { DomSanitizer } from '@angular/platform-browser';
import { REPO_URL, SITE_URL, Seo, canonical } from '../seo';
import { DOCS } from '../../generated/docs';

@Component({
  selector: 'app-doc-page',
  imports: [RouterLink, RouterLinkActive],
  template: `
    <div class="page layout">
      <aside>
        <nav aria-label="Documentation">
          <p class="side-title">Documentation</p>
          @for (d of docs; track d.slug) {
            <a [routerLink]="['/docs', d.slug]" routerLinkActive="active">{{ d.title }}</a>
          }
        </nav>
      </aside>
      <article>
        @if (doc(); as doc) {
          <h1>{{ doc.title }}</h1>
          @if (doc.headings.length > 2) {
            <details class="toc">
              <summary>On this page</summary>
              <ul>
                @for (h of doc.headings; track h.id) {
                  <li><a [routerLink]="[]" [fragment]="h.id">{{ h.text }}</a></li>
                }
              </ul>
            </details>
          }
          <div class="doc-body" [innerHTML]="html()" (click)="follow($event)"></div>
          <p class="small muted edit">
            <a [href]="repo + '/blob/main/docs/' + doc.slug + '.md'" rel="noopener">Edit this page on GitHub</a>
          </p>
        } @else {
          <h1>Page not found</h1>
          <p>There is no documentation page with that name. Pick one from the list.</p>
        }
      </article>
    </div>
  `,
  styles: `
    .layout { display: grid; grid-template-columns: 210px minmax(0, 1fr); gap: 40px; }
    aside nav { position: sticky; top: 16px; display: flex; flex-direction: column; gap: 2px; }
    .side-title { font-weight: 600; margin: 0.6em 0 8px; }
    aside a { text-decoration: none; color: var(--mat-sys-on-surface); padding: 5px 10px; border-radius: 6px; font-size: 0.925rem; }
    aside a:hover { background: var(--code-bg); }
    aside a.active { background: var(--mat-sys-secondary-container); font-weight: 500; }
    .toc { border: 1px solid var(--line); border-radius: 8px; padding: 8px 14px; margin: 8px 0 24px; }
    .toc summary { cursor: pointer; font-weight: 500; }
    .toc ul { columns: 2; margin: 8px 0 0; padding-left: 18px; }
    .toc li { line-height: 1.7; }
    .edit { margin-top: 40px; }
    @media (max-width: 860px) {
      .layout { grid-template-columns: minmax(0, 1fr); gap: 0; }
      aside nav { position: static; flex-direction: row; flex-wrap: wrap; border-bottom: 1px solid var(--line); padding-bottom: 8px; }
      .side-title { width: 100%; margin-top: 0; }
      .toc ul { columns: 1; }
    }
  `,
})
export class DocPage {
  readonly slug = input('');
  readonly docs = DOCS;
  readonly repo = REPO_URL;
  private readonly router = inject(Router);
  private readonly sanitizer = inject(DomSanitizer);
  private readonly seo = inject(Seo);

  readonly doc = computed(() => DOCS.find((d) => d.slug === this.slug()));
  readonly html = computed(() => this.sanitizer.bypassSecurityTrustHtml(this.doc()?.html ?? ''));

  constructor() {
    effect(() => {
      const doc = this.doc();
      if (!doc) {
        this.seo.set({ title: 'Page not found', description: 'This documentation page does not exist.', path: '/404', noindex: true });
        return;
      }
      const path = `/docs/${doc.slug}`;
      this.seo.set({
        title: doc.title,
        description: doc.description,
        path,
        type: 'article',
        jsonLd: [
          {
            '@type': 'TechArticle',
            headline: doc.title,
            description: doc.description,
            url: canonical(path),
            inLanguage: 'en',
            isPartOf: { '@type': 'WebSite', name: 'Dessa', url: SITE_URL + '/' },
            about: { '@type': 'ComputerLanguage', name: 'Dessa' },
            author: { '@type': 'Person', name: 'd-ustindwayne', url: 'https://dustindwayn.ee/' },
          },
        ],
      });
    });
  }

  /** Links between docs stay inside the app instead of loading the page again. */
  follow(event: MouseEvent): void {
    const link = (event.target as HTMLElement).closest('a');
    const href = link?.getAttribute('href');
    if (!href || !href.startsWith('/') || event.ctrlKey || event.metaKey || event.shiftKey || event.button !== 0) return;
    event.preventDefault();
    this.router.navigateByUrl(href);
  }
}

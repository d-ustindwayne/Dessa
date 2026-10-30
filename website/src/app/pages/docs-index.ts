import { Component, inject } from '@angular/core';
import { RouterLink } from '@angular/router';
import { Seo } from '../seo';
import { SITE_INFO } from '../../generated/site';

@Component({
  selector: 'app-docs-index',
  imports: [RouterLink],
  template: `
    <div class="page">
      <h1>Documentation</h1>
      <p>
        Start with <a routerLink="/docs/installation">Installing Dessa</a>, then read
        <a routerLink="/docs/language">the language guide</a>. Every page is also in the
        <code>docs</code> folder of the repository.
      </p>
      <div class="table-wrap">
        <table>
          <thead><tr><th>Page</th><th>Contents</th></tr></thead>
          <tbody>
            @for (doc of docs; track doc.slug) {
              <tr>
                <td><a [routerLink]="['/docs', doc.slug]">{{ doc.title }}</a></td>
                <td>{{ doc.description }}</td>
              </tr>
            }
          </tbody>
        </table>
      </div>
    </div>
  `,
})
export class DocsIndex {
  readonly docs = SITE_INFO.docs;

  constructor() {
    inject(Seo).set({
      title: 'Documentation',
      description:
        'The Dessa documentation: installing, the language, the compiler, libraries, the riv package manager, web pages, VS Code support and how it all works.',
      path: '/docs',
    });
  }
}

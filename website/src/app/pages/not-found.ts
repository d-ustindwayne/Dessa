import { Component, inject } from '@angular/core';
import { RouterLink } from '@angular/router';
import { Seo } from '../seo';

@Component({
  selector: 'app-not-found',
  imports: [RouterLink],
  template: `
    <div class="page">
      <h1>Page not found</h1>
      <p>There is nothing at this address. Try the <a routerLink="/">home page</a> or the <a routerLink="/docs">documentation</a>.</p>
    </div>
  `,
})
export class NotFound {
  constructor() {
    inject(Seo).set({ title: 'Page not found', description: 'This page does not exist.', path: '/404', noindex: true });
  }
}

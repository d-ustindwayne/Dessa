import { Routes } from '@angular/router';

export const routes: Routes = [
  { path: '', loadComponent: () => import('./pages/home').then((m) => m.Home) },
  { path: 'download', loadComponent: () => import('./pages/download').then((m) => m.Download) },
  { path: 'benchmarks', loadComponent: () => import('./pages/benchmarks').then((m) => m.Benchmarks) },
  { path: 'features', loadComponent: () => import('./pages/features').then((m) => m.Features) },
  { path: 'examples', loadComponent: () => import('./pages/examples').then((m) => m.Examples) },
  { path: 'playground', loadComponent: () => import('./pages/playground/playground').then((m) => m.Playground) },
  { path: 'docs', loadComponent: () => import('./pages/docs-index').then((m) => m.DocsIndex) },
  { path: 'docs/:slug', loadComponent: () => import('./pages/doc-page').then((m) => m.DocPage) },
  { path: '404', loadComponent: () => import('./pages/not-found').then((m) => m.NotFound) },
  { path: '**', loadComponent: () => import('./pages/not-found').then((m) => m.NotFound) },
];

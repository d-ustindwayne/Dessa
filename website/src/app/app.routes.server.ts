import { RenderMode, ServerRoute } from '@angular/ssr';
import { SITE_INFO } from '../generated/site';

// Every page is rendered to HTML at build time (GitHub Pages only serves files).
export const serverRoutes: ServerRoute[] = [
  {
    path: 'docs/:slug',
    renderMode: RenderMode.Prerender,
    getPrerenderParams: async () => SITE_INFO.docs.map((d) => ({ slug: d.slug })),
  },
  { path: '**', renderMode: RenderMode.Prerender },
];

import { DOCUMENT, Injectable, inject } from '@angular/core';
import { Meta, Title } from '@angular/platform-browser';

export const SITE_URL = 'https://dessa.dustindwayn.ee';
export const REPO_URL = 'https://github.com/d-ustindwayne/Dessa';
const SITE_NAME = 'Dessa';

export interface PageMeta {
  title: string;
  description: string;
  /** the route, like "/docs/language" */
  path: string;
  type?: 'website' | 'article';
  /** structured data for search engines (schema.org) */
  jsonLd?: object[];
  noindex?: boolean;
}

/** Sets the title, description, canonical link, Open Graph and Twitter tags and JSON-LD of a page. */
@Injectable({ providedIn: 'root' })
export class Seo {
  private readonly title = inject(Title);
  private readonly meta = inject(Meta);
  private readonly document = inject(DOCUMENT);

  set(page: PageMeta): void {
    const fullTitle = page.path === '/' ? page.title : `${page.title} | ${SITE_NAME}`;
    const url = canonical(page.path);
    this.title.setTitle(fullTitle);
    this.meta.updateTag({ name: 'description', content: page.description });
    this.meta.updateTag({ name: 'robots', content: page.noindex ? 'noindex, follow' : 'index, follow, max-image-preview:large' });
    this.meta.updateTag({ property: 'og:type', content: page.type ?? 'website' });
    this.meta.updateTag({ property: 'og:title', content: fullTitle });
    this.meta.updateTag({ property: 'og:description', content: page.description });
    this.meta.updateTag({ property: 'og:url', content: url });
    this.meta.updateTag({ name: 'twitter:title', content: fullTitle });
    this.meta.updateTag({ name: 'twitter:description', content: page.description });

    const head = this.document.head;
    let link = head.querySelector<HTMLLinkElement>('link[rel="canonical"]');
    if (!link) {
      link = this.document.createElement('link');
      link.rel = 'canonical';
      head.appendChild(link);
    }
    link.href = url;

    head.querySelectorAll('script[data-seo]').forEach((s) => s.remove());
    const data = [...(page.jsonLd ?? []), breadcrumbs(page.path, page.title)].filter(Boolean);
    for (const item of data) {
      const script = this.document.createElement('script');
      script.type = 'application/ld+json';
      script.setAttribute('data-seo', '');
      script.textContent = JSON.stringify({ '@context': 'https://schema.org', ...item });
      head.appendChild(script);
    }
  }
}

/** GitHub Pages serves /docs/x/ (it redirects /docs/x there), so canonical URLs end with a slash. */
export function canonical(path: string): string {
  return path === '/' ? SITE_URL + '/' : `${SITE_URL}${path.replace(/\/$/, '')}/`;
}

function breadcrumbs(path: string, title: string): object | null {
  if (path === '/') return null;
  const items = [{ name: 'Dessa', url: canonical('/') }];
  if (path.startsWith('/docs/')) items.push({ name: 'Documentation', url: canonical('/docs') });
  items.push({ name: title, url: canonical(path) });
  return {
    '@type': 'BreadcrumbList',
    itemListElement: items.map((item, i) => ({ '@type': 'ListItem', position: i + 1, name: item.name, item: item.url })),
  };
}

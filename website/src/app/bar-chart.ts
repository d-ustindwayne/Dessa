import { Component, computed, input } from '@angular/core';

export interface Bar {
  label: string;
  value: number;
  /** drawn in the accent colour (Dessa); the rest are grey */
  highlight?: boolean;
  /** shown after the value, like "0.9x C++" */
  note?: string;
}

/** A horizontal bar chart in plain HTML, so it is in the prerendered page and works without JavaScript. */
@Component({
  selector: 'app-bar-chart',
  template: `
    <figure class="chart">
      @if (caption()) {
        <figcaption>{{ caption() }}</figcaption>
      }
      @if (legend()) {
        <div class="legend small muted" aria-hidden="true">
          <span><i class="swatch hi"></i>Dessa</span>
          <span><i class="swatch"></i>Other languages</span>
        </div>
      }
      <div class="rows" role="list">
        @for (bar of bars(); track bar.label) {
          <div class="row" role="listitem" [attr.title]="bar.label + ': ' + format(bar.value) + ' ' + unit()">
            <span class="label">{{ bar.label }}</span>
            <span class="track">
              <span class="fill" [class.hi]="bar.highlight" [style.width]="'calc((100% - 150px) * ' + bar.ratio + ')'"></span>
              <span class="value">{{ format(bar.value) }} {{ unit() }}@if (bar.note) {<span class="muted">, {{ bar.note }}</span>}</span>
            </span>
          </div>
        }
      </div>
    </figure>
  `,
  styles: `
    .chart { margin: 16px 0 24px; }
    figcaption { font-weight: 600; margin-bottom: 8px; }
    .legend { display: flex; gap: 16px; margin-bottom: 10px; }
    .swatch { display: inline-block; width: 12px; height: 12px; border-radius: 2px; background: var(--series-other); margin-right: 6px; vertical-align: -1px; }
    .swatch.hi { background: var(--series-1); }
    .rows { display: grid; gap: 2px; }
    .row { display: grid; grid-template-columns: minmax(90px, 160px) 1fr; align-items: center; gap: 12px; padding: 3px 0; border-radius: 4px; }
    .row:hover { background: var(--code-bg); }
    .label { font-size: 0.875rem; text-align: right; }
    .track { display: flex; overflow: hidden; align-items: center; gap: 8px; min-width: 0; }
    .fill { display: block; height: 20px; min-width: 2px; background: var(--series-other); border-radius: 0 4px 4px 0; flex: none; }
    .fill.hi { background: var(--series-1); }
    .value { font-size: 0.8125rem; white-space: nowrap; font-variant-numeric: tabular-nums; }
    @media (max-width: 600px) {
      .row { grid-template-columns: 84px 1fr; gap: 8px; }
      .label { font-size: 0.8125rem; }
    }
  `,
})
export class BarChart {
  readonly rows = input.required<Bar[]>();
  readonly unit = input('ms');
  readonly caption = input('');
  readonly legend = input(true);
  readonly bars = computed(() => {
    const max = Math.max(...this.rows().map((r) => r.value), 1);
    return this.rows().map((r) => ({ ...r, ratio: r.value / max }));
  });

  format(value: number): string {
    return value.toLocaleString('en-US');
  }
}

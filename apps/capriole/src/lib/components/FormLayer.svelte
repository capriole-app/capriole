<script lang="ts">
  // The form on the page shown large: over each widget of a field that can be filled, the control that
  // fills it, placed in percentages of the page as shown (the engine lists widgets so). A value is
  // committed when it changes (a text when the control is left); the engine writes it with its
  // appearance, so that every reader shows it. Controls stay enabled while it writes (moving on with Tab
  // must not lose the focus): the editor queues the edits.
  import type { FieldInfo, FieldWidget } from '@capriole/engine-client';
  import { getEditor } from '$lib/editor.svelte';

  let { width, height }: { width: number; height: number } = $props();
  const editor = getEditor();
  const index = $derived(editor.viewing ?? 0);

  const placed = $derived(
    editor.fields.flatMap((field) =>
      field.fillable ? field.widgets.filter((w) => w.page === index).map((widget) => ({ field, widget })) : [],
    ),
  );

  function box(w: FieldWidget): string {
    const [x0, y0, x1, y1] = w.rect;
    return `left:${(x0 / width) * 100}%;top:${(y0 / height) * 100}%;width:${((x1 - x0) / width) * 100}%;height:${((y1 - y0) / height) * 100}%`;
  }

  /** A text size that fits the widget (in units of the page's width, which follow its size on screen). */
  function fontSize(w: FieldWidget, multiline: boolean): string {
    const h = w.rect[3] - w.rect[1];
    const points = multiline ? 11 : Math.max(6, Math.min(h * 0.62, 14));
    return `${(points / width) * 100}cqw`;
  }

  const label = (f: FieldInfo) => f.label || f.name || 'Campo';
  const key = (f: FieldInfo, w: FieldWidget) => `${w.ref[0]} ${w.ref[1]} ${f.value.join('\u0000')}`;

  function commit(field: FieldInfo, value: string | string[] | null) {
    const current = field.value;
    const next = value === null ? [] : Array.isArray(value) ? value : [value];
    if (next.length === current.length && next.every((v, i) => v === current[i])) return;
    editor.fillField(field, value);
  }

  const control =
    'absolute box-border rounded-[1px] border border-sky-500/60 bg-sky-50 px-[0.3cqw] text-black outline-none focus:border-sky-600 focus:bg-white focus:ring-2 focus:ring-sky-400/50 disabled:opacity-60';
</script>

<div class="absolute inset-0" data-testid="form-layer">
  {#each placed as { field, widget } (key(field, widget))}
    {#if field.type === 'text'}
      {#if field.multiline}
        <textarea
          class={[control, 'resize-none leading-tight']}
          style="{box(widget)};font-size:{fontSize(widget, true)}"
          data-field="{field.ref[0]} {field.ref[1]}"
          aria-label={label(field)}
          title={label(field)}
          required={field.required}
          maxlength={field.maxLength ?? undefined}
          value={field.value[0] ?? ''}
          onchange={(e) => commit(field, e.currentTarget.value || null)}></textarea>
      {:else}
        <input
          class={[control, field.comb && 'tracking-[0.5em]']}
          style="{box(widget)};font-size:{fontSize(widget, false)}"
          type={field.password ? 'password' : 'text'}
          data-field="{field.ref[0]} {field.ref[1]}"
          aria-label={label(field)}
          title={label(field)}
          required={field.required}
          maxlength={field.maxLength ?? undefined}
          value={field.value[0] ?? ''}
          onchange={(e) => commit(field, e.currentTarget.value || null)}
          onkeydown={(e) => e.key === 'Enter' && e.currentTarget.blur()}
        />
      {/if}
    {:else if field.type === 'checkbox'}
      <input
        class="absolute m-0 cursor-pointer accent-sky-600"
        style={box(widget)}
        type="checkbox"
        data-field="{field.ref[0]} {field.ref[1]}"
        aria-label={label(field)}
        title={label(field)}
        checked={field.value[0] === (widget.onState || 'Yes')}
        onchange={(e) => commit(field, e.currentTarget.checked ? widget.onState || 'Yes' : 'Off')}
      />
    {:else if field.type === 'radio'}
      <input
        class="absolute m-0 cursor-pointer accent-sky-600"
        style={box(widget)}
        type="radio"
        name="field-{field.ref[0]}-{field.ref[1]}"
        data-field="{field.ref[0]} {field.ref[1]}"
        aria-label="{label(field)}: {field.options.find((o) => o.value === widget.onState)?.display ?? widget.onState}"
        disabled={!widget.onState}
        checked={field.value[0] === widget.onState}
        onchange={() => commit(field, widget.onState)}
      />
    {:else if field.type === 'combobox' && field.editable}
      <input
        class={control}
        style="{box(widget)};font-size:{fontSize(widget, false)}"
        list="options-{field.ref[0]}-{field.ref[1]}"
        data-field="{field.ref[0]} {field.ref[1]}"
        aria-label={label(field)}
        title={label(field)}
        value={field.options.find((o) => o.value === field.value[0])?.display ?? field.value[0] ?? ''}
        onchange={(e) => {
          const text = e.currentTarget.value;
          commit(field, field.options.find((o) => o.display === text)?.value ?? (text || null));
        }}
      />
      <datalist id="options-{field.ref[0]}-{field.ref[1]}">
        {#each field.options as option (option.value)}
          <option value={option.display}></option>
        {/each}
      </datalist>
    {:else if field.type === 'listbox' && field.multiSelect}
      <!-- multiple is static: set after the options, it would keep only the last one chosen. -->
      <select
        class={control}
        style="{box(widget)};font-size:{fontSize(widget, true)}"
        data-field="{field.ref[0]} {field.ref[1]}"
        aria-label={label(field)}
        title={label(field)}
        multiple
        size={Math.max(2, field.options.length)}
        onchange={(e) =>
          commit(
            field,
            [...e.currentTarget.selectedOptions].map((o) => o.value),
          )}
      >
        {#each field.options as option (option.value)}
          <option value={option.value} selected={field.value.includes(option.value)}>{option.display}</option>
        {/each}
      </select>
    {:else if field.type === 'combobox' || field.type === 'listbox'}
      <select
        class={control}
        style="{box(widget)};font-size:{fontSize(widget, field.type === 'listbox')}"
        data-field="{field.ref[0]} {field.ref[1]}"
        aria-label={label(field)}
        title={label(field)}
        size={field.type === 'listbox' ? Math.max(2, field.options.length) : undefined}
        onchange={(e) => commit(field, e.currentTarget.value || null)}
      >
        {#if field.type === 'combobox'}
          <option value="" selected={field.value.length === 0}></option>
        {/if}
        {#each field.options as option (option.value)}
          <option value={option.value} selected={field.value.includes(option.value)}>{option.display}</option>
        {/each}
      </select>
    {/if}
  {/each}
</div>

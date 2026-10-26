import { expect, test, type Page } from '@playwright/test';
import { withoutFileAccess } from './downloads';
import { readFileSync } from 'node:fs';

/**
 * A PDF with one page per label (objects 3, 4, ... in page order), Helvetica text naming each page.
 * Classic xref table; nothing compressed, so saved files can be read as text.
 */
function pdfWithPages(labels: string[]): Buffer {
  const pages = labels.map((_, i) => `${3 + i} 0 R`).join(' ');
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    `<</Type/Pages/Kids[${pages}]/Count ${labels.length}/MediaBox[0 0 300 400]>>`,
    ...labels.map(
      (_, i) =>
        `<</Type/Page/Parent 2 0 R/Contents ${3 + labels.length + i} 0 R/Resources<</Font<</F1<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>>>>>>>`,
    ),
    ...labels.map((label) => {
      const content = `BT /F1 36 Tf 40 200 Td (${label}) Tj ET`;
      return `<</Length ${content.length}>>\nstream\n${content}\nendstream`;
    }),
  ];
  let pdf = '%PDF-1.7\n';
  const offsets: number[] = [];
  bodies.forEach((body, i) => {
    offsets.push(pdf.length);
    pdf += `${i + 1} 0 obj\n${body}\nendobj\n`;
  });
  const xref = pdf.length;
  pdf += `xref\n0 ${bodies.length + 1}\n0000000000 65535 f \n`;
  pdf += offsets.map((o) => `${String(o).padStart(10, '0')} 00000 n \n`).join('');
  pdf += `trailer\n<</Size ${bodies.length + 1}/Root 1 0 R>>\nstartxref\n${xref}\n%%EOF\n`;
  return Buffer.from(pdf, 'latin1');
}

async function open(page: Page, name: string, buffer: Buffer) {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.getByTestId('open-file').setInputFiles({ name, mimeType: 'application/pdf', buffer });
}

/** Clicks an action and returns the text of the PDF it downloads, with its file name. */
async function downloaded(page: Page, action: () => Promise<void>): Promise<{ name: string; text: string }> {
  const download = page.waitForEvent('download');
  await action();
  const file = await download;
  return { name: file.suggestedFilename(), text: readFileSync(await file.path()).toString('latin1') };
}

/** The order of the pages in a saved file, by the label their content shows. */
function labelsOf(text: string): string[] {
  const kids = /\/Kids \[([^\]]*)\]/.exec(text)?.[1] ?? '';
  return [...kids.matchAll(/(\d+) 0 R/g)].map(([, number]) => {
    const page = new RegExp(`\\n${number} 0 obj\\n<<[^]*?/Contents (\\d+) 0 R`).exec(text)?.[1];
    return new RegExp(`\\n${page} 0 obj[^]*?\\((\\w+)\\) Tj`).exec(text)?.[1] ?? '?';
  });
}

// Saving here is a download (the path of browsers without file access).
test.beforeEach(({ page }) => withoutFileAccess(page));

test('opens a file and edits its pages: rotate, delete, undo, move, save', async ({ page }) => {
  await open(page, 'tre.pdf', pdfWithPages(['Uno', 'Due', 'Tre']));
  const pages = page.getByTestId('page');
  await expect(pages).toHaveCount(3);
  await expect(page.locator('header').getByText('tre.pdf')).toBeVisible();
  await expect(page.locator('canvas').first()).toBeVisible(); // thumbnails drawn by the engine

  // Rotate the first page.
  await pages.nth(0).click();
  await expect(pages.nth(0)).toHaveAttribute('aria-pressed', 'true');
  await page.getByRole('button', { name: 'Ruota a destra' }).click();
  await expect(page.getByText('3 pagine · 1 selezionate')).toBeVisible();

  // Delete the second page, then undo.
  await pages.nth(1).click();
  await page.getByRole('button', { name: 'Elimina' }).click();
  await expect(pages).toHaveCount(2);
  await page.getByRole('button', { name: 'Annulla' }).click();
  await expect(pages).toHaveCount(3);

  // The third page one place earlier, from the toolbar (the keyboard's way).
  await pages.nth(2).click();
  await page.getByRole('button', { name: 'Sposta prima' }).click();
  await expect(pages.nth(1)).toHaveAttribute('aria-pressed', 'true');

  const saved = await downloaded(page, () => page.getByRole('button', { name: 'Salva' }).click());
  expect(saved.name).toBe('tre.pdf');
  expect(labelsOf(saved.text)).toEqual(['Uno', 'Tre', 'Due']);
  expect(saved.text).toContain('/Rotate 90');
});

test('drags pages, inserts another file, extracts pages', async ({ page }) => {
  await open(page, 'tre.pdf', pdfWithPages(['Uno', 'Due', 'Tre']));
  const pages = page.getByTestId('page');
  await expect(pages).toHaveCount(3);

  // Drag the first page onto the right half of the last one: it goes to the end.
  const last = await pages.nth(2).boundingBox();
  await pages.nth(0).dragTo(pages.nth(2), { targetPosition: { x: (last?.width ?? 100) - 5, y: 20 } });
  await expect(pages.nth(2)).toHaveAttribute('aria-pressed', 'true');

  // Another PDF after the selection (the moved page, now last): its page ends up selected.
  await page.getByTestId('insert-file').setInputFiles({
    name: 'altro.pdf',
    mimeType: 'application/pdf',
    buffer: pdfWithPages(['Quattro']),
  });
  await expect(pages).toHaveCount(4);
  await expect(pages.nth(3)).toHaveAttribute('aria-pressed', 'true');

  // The first and last pages (Ctrl-click) as a new file.
  await pages.nth(0).click({ modifiers: ['ControlOrMeta'] });
  const extracted = await downloaded(page, () => page.getByRole('button', { name: 'Estrai' }).click());
  expect(extracted.name).toBe('tre-pagine.pdf');
  expect(labelsOf(extracted.text)).toEqual(['Due', 'Quattro']);

  const saved = await downloaded(page, () => page.getByRole('button', { name: 'Salva' }).click());
  expect(labelsOf(saved.text)).toEqual(['Due', 'Tre', 'Uno', 'Quattro']);
});

test('«Apri» puts another file in place of the open one; unsaved changes are asked about first', async ({ page }) => {
  await open(page, 'tre.pdf', pdfWithPages(['Uno', 'Due', 'Tre']));
  const pages = page.getByTestId('page');
  await expect(pages).toHaveCount(3);
  const another = (name: string, labels: string[]) => ({
    name,
    mimeType: 'application/pdf',
    buffer: pdfWithPages(labels),
  });

  // Nothing changed: the file is chosen and opens.
  let chooser = page.waitForEvent('filechooser');
  await page.getByRole('button', { name: 'Apri', exact: true }).click();
  await (await chooser).setFiles(another('uno.pdf', ['Solo']));
  await expect(pages).toHaveCount(1);
  await expect(page.locator('header').getByText('uno.pdf')).toBeVisible();

  // A change not saved: declined, the document stays, and no file is asked for.
  await pages.nth(0).click();
  await page.getByRole('button', { name: 'Ruota a destra' }).click();
  await expect(page.locator('header').getByText('•')).toBeVisible();
  page.once('dialog', (dialog) => {
    expect(dialog.message()).toBe('Chiudere senza salvare le modifiche?');
    void dialog.dismiss();
  });
  let asked = false;
  page.once('filechooser', () => (asked = true));
  await page.getByRole('button', { name: 'Apri', exact: true }).click();
  await expect(page.locator('header').getByText('uno.pdf')).toBeVisible();
  expect(asked).toBe(false);

  // Accepted, from the page shown large and with Ctrl+O: the new file, with nothing of the old one.
  await pages.nth(0).dblclick();
  await expect(page.getByTestId('page-sheet')).toBeVisible();
  // The page's annotations read first: while the editor is busy, «Apri» (and Ctrl+O) waits.
  await expect(page.getByTestId('open-another')).toBeEnabled();
  page.once('dialog', (dialog) => void dialog.accept());
  chooser = page.waitForEvent('filechooser');
  await page.keyboard.press('ControlOrMeta+o');
  await (await chooser).setFiles(another('due.pdf', ['A', 'B']));
  await expect(pages).toHaveCount(2);
  await expect(page.locator('header').getByText('due.pdf')).toBeVisible();
  await expect(page.locator('header').getByText('•')).toHaveCount(0);
  await expect(page.getByText('2 pagine', { exact: true })).toBeVisible();
});

test('a damaged file is repaired before editing, and says so', async ({ page }) => {
  // The cross-reference table points nowhere: the engine rebuilds it by scanning the file.
  const broken = Buffer.from(
    pdfWithPages(['Uno', 'Due'])
      .toString('latin1')
      .replace(/startxref\n\d+/, 'startxref\n999999'),
    'latin1',
  );
  await open(page, 'rotto.pdf', broken);
  await expect(page.getByTestId('page')).toHaveCount(2);
  await expect(page.getByText(/Il file era danneggiato/)).toBeVisible();
  await page.getByTestId('page').nth(1).click();
  await page.getByRole('button', { name: 'Elimina' }).click();
  await expect(page.getByTestId('page')).toHaveCount(1);
});

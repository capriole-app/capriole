import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { inflateSync } from 'node:zlib';
import { withoutFileAccess } from './downloads';

/** One page, 300 × 400 points: "Uno segreto Due" in 24 pt Helvetica at (30, 300), i.e. y ≈ 78–106 as shown. */
function pdf(): Buffer {
  const content = 'BT /F1 24 Tf 30 300 Td (Uno segreto Due) Tj ET';
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    '<</Type/Page/Parent 2 0 R/MediaBox[0 0 300 400]/Contents 4 0 R/Resources<</Font<</F1<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>>>>>>>',
    `<</Length ${content.length}>>\nstream\n${content}\nendstream`,
  ];
  let text = '%PDF-1.7\n';
  const offsets: number[] = [];
  bodies.forEach((body, i) => {
    offsets.push(text.length);
    text += `${i + 1} 0 obj\n${body}\nendobj\n`;
  });
  const xref = text.length;
  text += `xref\n0 ${bodies.length + 1}\n0000000000 65535 f \n`;
  text += offsets.map((o) => `${String(o).padStart(10, '0')} 00000 n \n`).join('');
  text += `trailer\n<</Size ${bodies.length + 1}/Root 1 0 R>>\nstartxref\n${xref}\n%%EOF\n`;
  return Buffer.from(text, 'latin1');
}

/** Every stream of a file, decoded when it is Flate (else as it is), and the rest of the file. */
function everything(file: Buffer): string {
  const text = file.toString('latin1');
  let out = text;
  for (const m of text.matchAll(/stream\r?\n([\s\S]*?)\r?\nendstream/g)) {
    try {
      out += '\n' + inflateSync(Buffer.from(m[1]!, 'latin1')).toString('latin1');
    } catch {
      // not Flate
    }
  }
  return out;
}

async function at(page: Page, x: number, y: number): Promise<{ x: number; y: number }> {
  const box = (await page.getByTestId('page-surface').boundingBox())!;
  return { x: box.x + (x * box.width) / 300, y: box.y + (y * box.height) / 400 };
}

async function dragOnPage(page: Page, from: [number, number], to: [number, number]) {
  const a = await at(page, ...from);
  const b = await at(page, ...to);
  await page.mouse.move(a.x, a.y);
  await page.mouse.down();
  await page.mouse.move(b.x, b.y, { steps: 6 });
  await page.mouse.up();
}

test.beforeEach(({ page }) => withoutFileAccess(page));

test('marks text and an area, applies the redaction, and the saved file no longer has them', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'segreto.pdf', mimeType: 'application/pdf', buffer: pdf() });
  await page.getByTestId('page').first().dblclick();
  const list = page.getByTestId('annotation-list');

  // "segreto" marked by dragging over it; an empty area by a rectangle.
  await page.getByRole('button', { name: 'Oscura (redaction)' }).click();
  await dragOnPage(page, [90, 92], [150, 92]);
  await expect(list).toContainText('Da oscurare');
  await dragOnPage(page, [40, 200], [120, 260]);
  await expect(page.getByRole('button', { name: /Applica la redaction \(2\)/ })).toBeVisible();

  // Applied, after the confirmation: the marks go, and what they covered.
  page.once('dialog', (dialog) => {
    expect(dialog.message()).toContain('non si recuperano');
    void dialog.accept();
  });
  await page.getByRole('button', { name: /Applica la redaction/ }).click();
  await expect(list).not.toContainText('Da oscurare');
  await expect(page.getByRole('button', { name: /Applica la redaction/ })).toHaveCount(0);

  // Undo brings the marks back (until the file is saved); applied again.
  await page.getByRole('button', { name: 'Annulla' }).click();
  await expect(page.getByRole('button', { name: /Applica la redaction \(2\)/ })).toBeVisible();
  page.once('dialog', (dialog) => void dialog.accept());
  await page.getByRole('button', { name: /Applica la redaction/ }).click();
  await expect(page.getByRole('button', { name: /Applica la redaction/ })).toHaveCount(0);

  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Salva' }).click();
  const saved = everything(readFileSync(await (await download).path()));
  expect(saved).not.toContain('segreto');
  expect(saved).not.toMatch(/\/Subtype\s*\/Redact/);
  expect(saved).toContain('Uno');
  expect(saved).toContain('Due');
});

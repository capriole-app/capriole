import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { constants, inflateSync } from 'node:zlib';
import { withoutFileAccess } from './downloads';

/**
 * One page, 400 × 400 points: a line of text at the top, a red box (as shown from (20, 260) to (80, 300))
 * and a grey image (as shown from (300, 80) to (320, 100)).
 */
function pdf(): Buffer {
  const content = [
    'BT /F1 12 Tf 20 370 Td (Una riga di testo che resta ferma.) Tj ET',
    '1 0 0 rg 20 100 60 40 re f 0 g',
    'q 20 0 0 20 300 300 cm BI /W 2 /H 2 /CS /G /BPC 8 ID \x80\x80\x80\x80 EI Q',
  ].join('\n');
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    '<</Type/Page/Parent 2 0 R/MediaBox[0 0 400 400]/Contents 4 0 R/Resources<</Font<</F1 5 0 R>>>>>>',
    `<</Length ${content.length}>>\nstream\n${content}\nendstream`,
    '<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>',
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
      // A last byte of the data may be taken for the line break before endstream: what inflates is enough.
      out +=
        '\n' + inflateSync(Buffer.from(m[1]!, 'latin1'), { finishFlush: constants.Z_SYNC_FLUSH }).toString('latin1');
    } catch {
      // not Flate
    }
  }
  return out;
}

async function at(page: Page, x: number, y: number): Promise<{ x: number; y: number }> {
  const box = (await page.getByTestId('page-surface').boundingBox())!;
  return { x: box.x + (x * box.width) / 400, y: box.y + (y * box.height) / 400 };
}

async function open(page: Page) {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'oggetti.pdf', mimeType: 'application/pdf', buffer: pdf() });
  await page.getByTestId('page').first().dblclick();
  await expect(page.getByTestId('page-surface')).toBeVisible();
}

/** Drags from (x0, y0) to (x1, y1), page points as shown. */
async function drag(page: Page, x0: number, y0: number, x1: number, y1: number) {
  const a = await at(page, x0, y0);
  const b = await at(page, x1, y1);
  await page.mouse.move(a.x, a.y);
  await page.mouse.down();
  await page.mouse.move(b.x, b.y, { steps: 5 });
  await page.mouse.up();
}

/** The selected object's outline, page points as shown. */
async function selected(page: Page): Promise<[number, number, number, number]> {
  const r = page.getByTestId('object-selected');
  const [x, y, w, h] = await Promise.all(
    ['x', 'y', 'width', 'height'].map(async (a) => Number(await r.getAttribute(a))),
  );
  return [x!, y!, x! + w!, y! + h!];
}

async function click(page: Page, x: number, y: number) {
  const p = await at(page, x, y);
  await page.mouse.click(p.x, p.y);
}

/** Clicks (x, y) until the object there is selected, named so (after an edit the page's model is read again). */
async function select(page: Page, x: number, y: number, name: string) {
  const heading = page.getByTestId('object-panel').getByRole('heading');
  await expect(async () => {
    await click(page, x, y);
    await expect(heading).toHaveText(name, { timeout: 500 });
  }).toPass();
}

async function saved(page: Page): Promise<string> {
  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Salva' }).click();
  return everything(readFileSync(await (await download).path()));
}

test.beforeEach(({ page }) => withoutFileAccess(page));

test('an image is selected, moved and resized; saved; undone', async ({ page }) => {
  await open(page);
  await select(page, 310, 90, 'Immagine');

  await drag(page, 310, 90, 340, 110); // 30 right, 20 down
  await expect(page.getByRole('alert')).toHaveCount(0);
  await expect(async () => {
    const [x0, y0, x1, y1] = await selected(page);
    expect([x0, y0, x1, y1].map(Math.round)).toEqual([330, 100, 350, 120]);
  }).toPass();

  // From its bottom right corner: 10 larger each way.
  await drag(page, 350, 120, 360, 130);
  await expect(async () => {
    expect((await selected(page)).map(Math.round)).toEqual([330, 100, 360, 130]);
  }).toPass();

  const file = await saved(page);
  expect(file).toContain('(Una riga di testo che resta ferma.) Tj'); // the text as it was
  expect(file).toMatch(/ cm\nBI /); // the image drawn through a matrix around it

  await page.getByRole('button', { name: 'Annulla' }).click();
  await page.getByRole('button', { name: 'Annulla' }).click();
  await select(page, 310, 90, 'Immagine');
  expect((await selected(page)).map(Math.round)).toEqual([300, 80, 320, 100]);
});

test('a box is deleted with Canc; undone', async ({ page }) => {
  await open(page);
  await select(page, 50, 280, 'Riquadro');
  await page.keyboard.press('Delete');
  await expect(page.getByTestId('object-panel')).toHaveCount(0);
  await expect(page.getByRole('alert')).toHaveCount(0);
  await click(page, 50, 280);
  await expect(page.getByTestId('object-panel')).toHaveCount(0); // nothing there now
  expect(await saved(page)).not.toContain(' re\nf');

  await page.getByRole('button', { name: 'Annulla' }).click();
  await select(page, 50, 280, 'Riquadro');
});

test('an object moved off the page is refused, in Italian, with no step of undo', async ({ page }) => {
  await open(page);
  await select(page, 50, 280, 'Riquadro');
  await drag(page, 50, 280, 440, 280); // its left side beyond the page's right one
  await expect(page.getByRole('alert')).toContainText('uscirebbe dalla pagina');
  await expect(page.getByRole('button', { name: 'Annulla' })).toBeDisabled();
  expect((await selected(page)).map(Math.round)).toEqual([20, 260, 80, 300]); // where it was, still selected
});

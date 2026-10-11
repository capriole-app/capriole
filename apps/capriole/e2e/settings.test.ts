import { expect, test, type Page } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { deflateSync } from 'node:zlib';
import { withoutFileAccess } from './downloads';

// The settings (kept on the device, used where they apply), the default colours, and graphic signatures:
// pictures kept (at most four) and put on a page as stamps, moved and resized keeping their proportions.

/** Pages of 300 × 400 points with a line of text each. */
function pdf(): Buffer {
  const content = 'BT /F1 24 Tf 30 300 Td (Contratto firmato) Tj ET';
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

const crcTable = Array.from({ length: 256 }, (_, n) => {
  let c = n;
  for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
  return c >>> 0;
});
function crc32(data: Buffer): number {
  let c = 0xffffffff;
  for (const b of data) c = crcTable[(c ^ b) & 0xff]! ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}

/** A PNG (RGB, 8 bits) of width × height: white, with a black stroke from corner to corner — a signature on paper. */
function signaturePng(width: number, height: number): Buffer {
  const rows: number[] = [];
  for (let y = 0; y < height; y++) {
    rows.push(0); // filter: none
    for (let x = 0; x < width; x++) {
      const ink = Math.abs(y - (x * height) / width) < 3;
      rows.push(...(ink ? [0, 0, 0] : [255, 255, 255]));
    }
  }
  const chunk = (type: string, data: Buffer) => {
    const length = Buffer.alloc(4);
    length.writeUInt32BE(data.length);
    const body = Buffer.concat([Buffer.from(type, 'latin1'), data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(body));
    return Buffer.concat([length, body, crc]);
  };
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header.set([8, 2, 0, 0, 0], 8);
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk('IHDR', header),
    chunk('IDAT', deflateSync(Buffer.from(rows))),
    chunk('IEND', Buffer.alloc(0)),
  ]);
}

async function open(page: Page) {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'contratto.pdf', mimeType: 'application/pdf', buffer: pdf() });
  await expect(page.getByTestId('page')).toHaveCount(1);
}

async function savedText(page: Page): Promise<string> {
  const download = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Salva', exact: true }).click();
  return readFileSync(await (await download).path()).toString('latin1');
}

test.beforeEach(({ page }) => withoutFileAccess(page));

test('the highlighter is yellow, what writes and draws is black', async ({ page }) => {
  await open(page);
  await page.getByTestId('page').dblclick();
  const colours = page.getByRole('radiogroup', { name: 'Colore' });
  await page.getByRole('button', { name: 'Evidenzia' }).click();
  await expect(colours.getByRole('radio', { name: 'Giallo' })).toHaveAttribute('aria-checked', 'true');
  for (const tool of ['Penna', 'Rettangolo', 'Testo', 'Sottolinea']) {
    await page.getByRole('button', { name: tool, exact: true }).click();
    await expect(colours.getByRole('radio', { name: 'Nero' })).toHaveAttribute('aria-checked', 'true');
  }
  // A colour chosen for drawing leaves the highlighter's as it was.
  await colours.getByRole('radio', { name: 'Rosso' }).click();
  await page.getByRole('button', { name: 'Nota' }).click();
  await expect(colours.getByRole('radio', { name: 'Giallo' })).toHaveAttribute('aria-checked', 'true');
  await page.getByRole('button', { name: 'Penna' }).click();
  await expect(colours.getByRole('radio', { name: 'Rosso' })).toHaveAttribute('aria-checked', 'true');
});

test('settings are kept on the device and used: zoom, author, colours, a signature’s reason', async ({ page }) => {
  await page.goto('/');
  await page.getByRole('button', { name: 'Impostazioni' }).click();
  const dialog = page.getByTestId('settings-dialog');
  await dialog.getByLabel('Zoom delle miniature (%)').fill('150');
  await dialog.getByLabel('Zoom delle miniature (%)').press('Tab');
  await dialog.getByLabel('Zoom della pagina all’apertura').selectOption('125');
  await dialog.getByLabel('Autore').fill('Maria Rossi');
  await dialog.getByLabel('Autore').press('Tab');
  await dialog
    .getByRole('radiogroup', { name: 'Colore dell’evidenziatore' })
    .getByRole('radio', { name: 'Verde' })
    .click();
  await dialog.getByLabel('Motivo predefinito').fill('Approvazione');
  await dialog.getByLabel('Motivo predefinito').press('Tab');
  await dialog.getByRole('button', { name: 'Chiudi' }).click();

  // Kept: read again after a reload.
  await page.reload();
  await open(page);
  await expect(page.getByTestId('zoom').getByLabel('Percentuale di zoom')).toHaveValue('150%');
  await page.getByTestId('page').dblclick();
  await expect(page.getByTestId('zoom').getByLabel('Percentuale di zoom')).toHaveValue('125%');
  await page.getByRole('button', { name: 'Evidenzia' }).click();
  await expect(page.getByRole('radiogroup', { name: 'Colore' }).getByRole('radio', { name: 'Verde' })).toHaveAttribute(
    'aria-checked',
    'true',
  );
  // The author goes into what is added.
  await page.getByRole('button', { name: 'Rettangolo' }).click();
  const surface = page.getByTestId('page-surface');
  const box = (await surface.boundingBox())!;
  await page.mouse.move(box.x + 40, box.y + 40);
  await page.mouse.down();
  await page.mouse.move(box.x + 120, box.y + 100);
  await page.mouse.up();
  await expect(page.locator('[data-annotation="Square"]')).toHaveCount(1);
  expect(await savedText(page)).toMatch(/\/T\s*\(Maria Rossi\)|\/T\s*<FEFF/);

  // The reason of a digital signature, filled in.
  await page.getByRole('button', { name: 'Pagine' }).click();
  await page.getByRole('button', { name: /^Firme/ }).click();
  await page.getByTestId('signatures-panel').getByRole('button', { name: 'Firma il documento…' }).click();
  await expect(page.getByRole('dialog').getByLabel('Motivo')).toHaveValue('Approvazione');
  await page.getByRole('dialog').getByRole('button', { name: 'Annulla' }).click();

  // Back to the defaults (graphic signatures are not settings: they stay).
  await page.getByRole('button', { name: 'Impostazioni' }).click();
  page.once('dialog', (d) => d.accept());
  await dialog.getByRole('button', { name: 'Ripristina i predefiniti' }).click();
  await expect(dialog.getByLabel('Autore')).toHaveValue('');
  await expect(dialog.getByLabel('Zoom delle miniature (%)')).toHaveValue('100');
});

test('graphic signatures: at most four kept, one put on the page, resized keeping its proportions, saved', async ({
  page,
}) => {
  await open(page);
  await page.getByTestId('page').dblclick();
  // No signature yet: the stamp tool leads to where they are added.
  await page.getByRole('button', { name: 'Firma grafica' }).click();
  await page.getByTestId('stamp-choice').getByRole('button', { name: 'Aggiungi una firma…' }).click();
  const dialog = page.getByTestId('settings-dialog');
  const kept = dialog.getByTestId('graphic-signatures');
  await expect(kept).toContainText('Non è una firma digitale');
  for (let i = 1; i <= 4; i++) {
    await kept
      .getByTestId('graphic-signature-file')
      .setInputFiles({ name: `firma${i}.png`, mimeType: 'image/png', buffer: signaturePng(200, 60) });
    await expect(kept.getByRole('listitem')).toHaveCount(i);
  }
  await expect(kept).toContainText('4 di 4');
  await expect(kept.getByTestId('graphic-signature-file')).toHaveCount(0); // full
  await kept.getByRole('button', { name: 'Elimina la firma «firma4.png»' }).click();
  await expect(kept.getByRole('listitem')).toHaveCount(3);
  // Not a picture: said.
  await kept
    .getByTestId('graphic-signature-file')
    .setInputFiles({ name: 'firma.png', mimeType: 'image/png', buffer: Buffer.from('not a picture') });
  await expect(kept.getByRole('alert')).toContainText('non si legge come immagine');
  await dialog.getByRole('button', { name: 'Chiudi' }).click();

  // Put with a click: as wide as 150 points at most, its proportions kept (200 × 60 pixels: the stroke
  // reaches the corners, nothing to cut away), then selected with the selection tool.
  await page.getByTestId('stamp-choice').getByRole('radio', { name: 'firma1.png' }).click();
  const surface = page.getByTestId('page-surface');
  const box = (await surface.boundingBox())!;
  const scale = box.width / 300;
  await page.mouse.click(box.x + 150 * scale, box.y + 200 * scale);
  const stamp = page.locator('[data-annotation="Stamp"]');
  await expect(stamp).toHaveCount(1);
  await expect(page.getByRole('button', { name: 'Seleziona e sposta' })).toHaveAttribute('aria-pressed', 'true');
  const placed = (await stamp.boundingBox())!;
  // 40% of the page's width: 120 points (the outline drawn around it adds a pixel).
  expect(Math.abs(placed.width / scale - 120)).toBeLessThan(2);
  expect(placed.width / placed.height).toBeCloseTo(200 / 60, 1);

  // Resized from its bottom-right corner: wider, the same proportions.
  const handles = page.getByTestId('stamp-handle');
  await expect(handles).toHaveCount(4);
  const corner = (await handles.nth(2).boundingBox())!;
  await page.mouse.move(corner.x + corner.width / 2, corner.y + corner.height / 2);
  await page.mouse.down();
  await page.mouse.move(corner.x + 40 * scale, corner.y + 40 * scale, { steps: 4 });
  await page.mouse.up();
  await expect.poll(async () => (await stamp.boundingBox())!.width).toBeGreaterThan(placed.width + 30 * scale);
  const resized = (await stamp.boundingBox())!;
  expect(resized.width / resized.height).toBeCloseTo(200 / 60, 1);
  // Within the drag: about 40 points wider.
  expect(resized.width - placed.width).toBeLessThan(60 * scale);

  const saved = await savedText(page);
  expect(saved).toMatch(/\/Subtype\s*\/Stamp/);
  expect(saved).toMatch(/\/SMask/);

  // Kept on the device: three after a reload.
  await page.reload();
  await page.getByRole('button', { name: 'Impostazioni' }).click();
  await expect(page.getByTestId('graphic-signatures').getByRole('listitem')).toHaveCount(3);
});

import { expect, test } from '@playwright/test';

/** One page, 200 × 100 points. */
function pdf(): string {
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    '<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 100]>>',
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
  return text + `trailer\n<</Size ${bodies.length + 1}/Root 1 0 R>>\nstartxref\n${xref}\n%%EOF\n`;
}

// The browser's dialogs (File System Access, Chromium), stood in for: the open dialog gives modulo.pdf,
// the save dialog copia.pdf; what is written through each handle, and what the save dialog was asked, is
// kept on the page.
test('opens through the dialog and saves over the same file; "Salva con nome" asks where', async ({ page }) => {
  await page.addInitScript((source: string) => {
    const log = { writes: [] as { name: string; text: string }[], asked: [] as string[] };
    Object.assign(window, { fileLog: log });
    const handle = (name: string, data: string) => ({
      kind: 'file',
      name,
      getFile: async () => new File([Uint8Array.from(data, (c) => c.charCodeAt(0))], name, { type: 'application/pdf' }),
      createWritable: async () => {
        const chunks: ArrayBuffer[] = [];
        return {
          write: async (chunk: ArrayBuffer) => void chunks.push(chunk),
          close: async () => {
            const bytes = new Uint8Array(await new Blob(chunks).arrayBuffer());
            log.writes.push({ name, text: String.fromCharCode(...bytes) });
          },
        };
      },
    });
    Object.assign(window, {
      showOpenFilePicker: async () => [handle('modulo.pdf', source)],
      showSaveFilePicker: async (options: { suggestedName?: string }) => {
        log.asked.push(options.suggestedName ?? '');
        return handle('copia.pdf', '');
      },
    });
  }, pdf());
  const log = () =>
    page.evaluate(
      () => (window as unknown as { fileLog: { writes: { name: string; text: string }[]; asked: string[] } }).fileLog,
    );

  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.getByRole('button', { name: 'Scegli un PDF' }).click();
  await expect(page.getByTestId('page')).toHaveCount(1);
  await expect(page.getByText('modulo.pdf')).toBeVisible();

  await page.getByTestId('page').first().click();
  await page.getByRole('button', { name: 'Ruota a destra' }).click();
  await page.getByRole('button', { name: 'Salva', exact: true }).click();
  await expect.poll(async () => (await log()).writes.length).toBe(1);
  let written = await log();
  expect(written.asked).toEqual([]); // the file it came from: no dialog
  expect(written.writes[0]!.name).toBe('modulo.pdf');
  expect(written.writes[0]!.text).toMatch(/\/Rotate 90/);

  await page.getByRole('button', { name: 'Salva con nome' }).click();
  await expect.poll(async () => (await log()).writes.length).toBe(2);
  written = await log();
  expect(written.asked).toEqual(['modulo.pdf']);
  expect(written.writes[1]!.name).toBe('copia.pdf');
  await expect(page.getByText('copia.pdf')).toBeVisible();

  // From now on "Salva" writes the copy, without asking.
  await page.getByRole('button', { name: 'Ruota a destra' }).click();
  await page.getByRole('button', { name: 'Salva', exact: true }).click();
  await expect.poll(async () => (await log()).writes.length).toBe(3);
  written = await log();
  expect(written.asked).toEqual(['modulo.pdf']);
  expect(written.writes[2]!.name).toBe('copia.pdf');
  expect(written.writes[2]!.text).toMatch(/\/Rotate 180/);
});

import { expect, test } from '@playwright/test';

// The other tests start with the terms accepted (playwright.config.ts); these start without.
test.use({ storageState: { cookies: [], origins: [] } });

const pdf = {
  name: 'vuoto.pdf',
  mimeType: 'application/pdf',
  buffer: Buffer.from(
    '%PDF-1.7\n1 0 obj\n<</Type/Catalog/Pages 2 0 R>>\nendobj\n2 0 obj\n<</Type/Pages/Kids[3 0 R]/Count 1>>\nendobj\n3 0 obj\n<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 200]>>\nendobj\ntrailer\n<</Root 1 0 R>>\n%%EOF\n',
    'latin1',
  ),
};

test('a file opens only once the terms of use are accepted, and they are asked once', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  const choose = page.getByRole('button', { name: 'Scegli un PDF' });
  await expect(choose).toBeDisabled();

  // A file given anyway (a drop, the input) is refused, with the reason.
  await page.getByTestId('open-file').setInputFiles(pdf);
  await expect(page.getByRole('alert')).toHaveText('Per aprire un file, accetta prima le condizioni d’uso.');
  await expect(page.getByTestId('brand-mark')).toHaveCount(0);

  // The link in the sentence opens the text, and does not tick the box.
  const accept = page.getByTestId('accept-terms');
  await accept.getByRole('button', { name: 'condizioni d’uso' }).click();
  const dialog = page.getByTestId('terms-dialog');
  await expect(dialog).toContainText('Bobolar Ltd');
  await expect(dialog).toContainText('hello@capriole.app');
  await expect(dialog).toContainText('Limitazione di responsabilità');
  await expect(dialog).toContainText('l’originale non si recupera');
  await expect(dialog).toContainText('prevale la versione inglese');
  await dialog.getByRole('button', { name: 'Chiudi' }).click();
  await expect(dialog).toBeHidden();
  await expect(accept.getByRole('checkbox')).not.toBeChecked();

  await accept.getByRole('checkbox').click(); // not check(): the box goes away once ticked
  await expect(accept).toBeHidden();
  await expect(page.getByRole('alert')).toHaveCount(0);
  await expect(choose).toBeEnabled();

  // Remembered on this device: not asked again.
  await page.reload();
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await expect(page.getByTestId('accept-terms')).toBeHidden();
  await page.getByTestId('open-file').setInputFiles(pdf);
  await expect(page.getByTestId('brand-mark')).toBeVisible();

  // Still to be read, from «Informazioni e licenze».
  await page.getByTestId('brand-mark').click();
  await page.getByTestId('licenses-dialog').getByRole('button', { name: 'Condizioni d’uso' }).click();
  await expect(page.getByTestId('terms-dialog')).toContainText('Bobolar Ltd');
});

test('the terms in English, the language that prevails', async ({ browser }) => {
  const context = await browser.newContext({ locale: 'en-GB', storageState: { cookies: [], origins: [] } });
  const page = await context.newPage();
  await page.goto('/');
  const accept = page.getByTestId('accept-terms');
  await expect(accept).toContainText('I have read and accept the terms of use.');
  await accept.getByRole('button', { name: 'terms of use' }).click();
  const dialog = page.getByTestId('terms-dialog');
  await expect(dialog.getByRole('heading', { level: 2 })).toHaveText('Terms of use');
  await expect(dialog).toContainText('Version of 13 October 2026');
  await expect(dialog).toContainText('governed by the laws of Ireland');
  await context.close();
});

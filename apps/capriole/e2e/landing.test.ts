import { expect, test } from '@playwright/test';

// The page without a file, rendered at build time: its words are in the HTML (what search engines read,
// before any script), and the app takes it over in the browser.

const taglines = {
  '/': 'Edit, sign and fill in PDFs. Free, no account.',
  '/it/': 'Modifica, firma e compila i PDF. Gratis, senza account.',
  '/en/': 'Edit, sign and fill in PDFs. Free, no account.',
  '/fr/': 'Modifiez, signez et remplissez vos PDF. Gratuit, sans compte.',
  '/de/': 'PDFs bearbeiten, signieren und ausfüllen. Kostenlos, ohne Konto.',
  '/es/': 'Edita, firma y rellena PDF. Gratis, sin cuenta.',
};

for (const [path, tagline] of Object.entries(taglines)) {
  test(`${path}: the landing's words are in the HTML`, async ({ request }) => {
    const html = await (await request.get(path)).text();
    expect(html).toContain(tagline);
    expect(html).toContain('aria-label="Capriole"');
    // The logo's file is asked for by the app running, not by the page: it counts the starts.
    expect(html).not.toContain('/logo-light.svg');
  });
}

test('the app takes the landing over: the logo from its file, a file opened from the card', async ({ page }) => {
  const logo = page.waitForRequest((r) => r.url().endsWith('/logo-light.svg'));
  await page.goto('/it/');
  await logo;
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page.getByTestId('open-file').setInputFiles({
    name: 'vuoto.pdf',
    mimeType: 'application/pdf',
    buffer: Buffer.from(
      '%PDF-1.7\n1 0 obj\n<</Type/Catalog/Pages 2 0 R>>\nendobj\n2 0 obj\n<</Type/Pages/Kids[3 0 R]/Count 1>>\nendobj\n3 0 obj\n<</Type/Page/Parent 2 0 R/MediaBox[0 0 200 200]>>\nendobj\ntrailer\n<</Root 1 0 R>>\n%%EOF\n',
      'latin1',
    ),
  });
  await expect(page.getByTestId('page')).toHaveCount(1);
  await expect(page.getByRole('region', { name: 'Apri un PDF' })).toHaveCount(0);
});

test('in the desktop shell what only the web app has is hidden from the first paint', async ({ page }) => {
  await page.goto('/it/');
  await expect(page.getByTestId('download-open')).toBeVisible();
  // app.html marks the shell on <html> before anything paints; here the mark alone, on a browser.
  await page.evaluate(() => document.documentElement.classList.add('desktop'));
  await expect(page.getByTestId('download-open')).toBeHidden();
  await expect(page.getByTestId('report-button')).toBeHidden();
});

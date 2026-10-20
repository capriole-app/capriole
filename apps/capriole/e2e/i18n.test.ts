import { expect, test } from '@playwright/test';

// The interface's language: the system's (the browser's languages) when it is one the app speaks, else
// English; the person's choice from the menu once made, remembered.

const firstScreen = [
  {
    locale: 'it-IT',
    lang: 'it',
    choose: 'Scegli un PDF',
    about: 'Informazioni e licenze',
    title: 'Capriole — editor PDF nel browser',
  },
  {
    locale: 'en-US',
    lang: 'en',
    choose: 'Choose a PDF',
    about: 'About and licences',
    title: 'Capriole — PDF editor in your browser',
  },
  {
    locale: 'fr-FR',
    lang: 'fr',
    choose: 'Choisir un PDF',
    about: 'À propos et licences',
    title: 'Capriole — éditeur PDF dans le navigateur',
  },
  {
    locale: 'de-DE',
    lang: 'de',
    choose: 'PDF auswählen',
    about: 'Info und Lizenzen',
    title: 'Capriole — PDF-Editor im Browser',
  },
  {
    locale: 'es-ES',
    lang: 'es',
    choose: 'Elegir un PDF',
    about: 'Información y licencias',
    title: 'Capriole — editor de PDF en el navegador',
  },
  {
    locale: 'pt-BR',
    lang: 'en',
    choose: 'Choose a PDF',
    about: 'About and licences',
    title: 'Capriole — PDF editor in your browser',
  }, // not spoken: English
];

for (const { locale, lang, choose, about, title } of firstScreen) {
  test.describe(locale, () => {
    test.use({ locale });
    test(`the first screen in ${lang}`, async ({ page }) => {
      await page.goto('/');
      await expect(page.getByRole('button', { name: choose })).toBeVisible();
      await expect(page.locator('html')).toHaveAttribute('lang', lang);
      await expect(page).toHaveTitle(title);
      await page.getByRole('button', { name: about }).click();
      await expect(page.getByTestId('licenses-dialog').getByRole('heading', { name: about })).toBeVisible();
    });
  });
}

test.describe('a choice', () => {
  test.use({ locale: 'en-US' });
  test('made from the menu, it is remembered', async ({ page }) => {
    await page.goto('/');
    await expect(page.getByRole('button', { name: 'Choose a PDF' })).toBeVisible();
    await page.getByRole('button', { name: 'Language' }).click();
    const deutsch = page.getByRole('menuitemradio', { name: 'Deutsch' });
    await expect(page.getByRole('menuitemradio', { name: 'English' })).toHaveAttribute('aria-checked', 'true');
    await deutsch.click();
    await expect(page.getByRole('button', { name: 'PDF auswählen' })).toBeVisible();
    await expect(page.locator('html')).toHaveAttribute('lang', 'de');
    await expect(page).toHaveTitle('Capriole — PDF-Editor im Browser');

    await page.reload();
    await expect(page.getByRole('button', { name: 'PDF auswählen' })).toBeVisible();
    await page.getByRole('button', { name: 'Sprache' }).click();
    await expect(page.getByRole('menuitemradio', { name: 'Deutsch' })).toHaveAttribute('aria-checked', 'true');
  });
});

test.describe('«About»', () => {
  test.use({ locale: 'en-US' });
  test('shows the mark, the versions and the motto', async ({ page }) => {
    await page.goto('/');
    await expect(page.getByText(/Capriole engine \d+\.\d+\.\d+/)).toBeVisible();
    await page.getByRole('button', { name: 'About and licences' }).click();
    const about = page.getByTestId('about');
    await expect(about.locator('img').first()).toBeVisible();
    expect(
      await about
        .locator('img')
        .first()
        .evaluate((img: HTMLImageElement) => img.naturalWidth),
    ).toBeGreaterThan(0);
    await expect(page.getByTestId('about-versions')).toHaveText(/^version \d+\.\d+\.\d+ · engine \d+\.\d+\.\d+$/);
    await expect(about).toContainText('All the moves. No strings attached.');
    await expect(page.getByTestId('licenses-dialog')).toContainText('free software');
  });
});

// A page per language for search engines (src/lib/page-head.ts): its head written in the file, in its
// language, with the other languages' pages; / for the others (x-default), in English.
const origin = 'https://capriole.app';
const pages = [
  { path: '/', lang: 'en', canonical: `${origin}/`, title: 'Capriole — PDF editor in your browser' },
  ...firstScreen
    .filter(({ locale }) => locale !== 'pt-BR')
    .map(({ lang, title }) => ({ path: `/${lang}/`, lang, canonical: `${origin}/${lang}/`, title })),
];

for (const { path, lang, canonical, title } of pages) {
  test(`${path}: its head as search engines read it`, async ({ request }) => {
    const response = await request.get(path);
    expect(response.status()).toBe(200);
    const html = await response.text();
    expect(html).toContain(`<html lang="${lang}"`);
    expect(html).toContain(`<title>${title}</title>`);
    expect(html).toContain(`<link rel="canonical" href="${canonical}" />`);
    expect(html).toContain(`<meta property="og:url" content="${canonical}" />`);
    for (const other of ['it', 'en', 'fr', 'de', 'es'])
      expect(html).toContain(`<link rel="alternate" hreflang="${other}" href="${origin}/${other}/" />`);
    expect(html).toContain(`<link rel="alternate" hreflang="x-default" href="${origin}/" />`);
    expect(html).not.toContain('%capriole.');
  });
}

test.describe('a language page', () => {
  test.use({ locale: 'en-US' });
  test('opens in its language, over a choice made before, without changing it', async ({ page }) => {
    await page.addInitScript(() => {
      if (!localStorage.getItem('capriole.locale')) localStorage.setItem('capriole.locale', 'fr');
    });
    await page.goto('/it/');
    await expect(page.getByRole('button', { name: 'Scegli un PDF' })).toBeVisible();
    await expect(page.locator('html')).toHaveAttribute('lang', 'it');
    await expect(page).toHaveTitle('Capriole — editor PDF nel browser');
    expect(await page.evaluate(() => localStorage.getItem('capriole.locale'))).toBe('fr');

    await page.goto('/');
    await expect(page.getByRole('button', { name: 'Choisir un PDF' })).toBeVisible();
  });

  test('follows the language chosen from the menu in its address', async ({ page }) => {
    await page.goto('/es/');
    await expect(page.getByRole('button', { name: 'Elegir un PDF' })).toBeVisible();
    await page.getByRole('button', { name: 'Idioma' }).click();
    await page.getByRole('menuitemradio', { name: 'Deutsch' }).click();
    await expect(page.getByRole('button', { name: 'PDF auswählen' })).toBeVisible();
    await expect(page).toHaveURL(/\/de\/$/);

    await page.reload();
    await expect(page.getByRole('button', { name: 'PDF auswählen' })).toBeVisible();
    await page.goto('/');
    await expect(page.getByRole('button', { name: 'PDF auswählen' })).toBeVisible();
  });
});

test('the sitemap lists every page, each with the others as its alternates', async ({ request }) => {
  const xml = await (await request.get('/sitemap.xml')).text();
  for (const { canonical } of pages) expect(xml).toContain(`<loc>${canonical}</loc>`);
  expect(xml.match(/hreflang="x-default"/g)).toHaveLength(pages.length);
});

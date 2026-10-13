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

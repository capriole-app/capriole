import { expect, test } from '@playwright/test';

// What search engines and shared links read of each page, written at build time (src/lib/page-head.ts):
// the picture of the page's language, and the data of JSON-LD — the questions the very ones the page shows.

const origin = 'https://capriole.app';
const pages = [
  { path: '/', lang: 'en' },
  ...['it', 'en', 'fr', 'de', 'es'].map((lang) => ({ path: `/${lang}/`, lang })),
];

/** The JSON-LD blocks of a page, read as JSON. */
function structured(html: string): Record<string, unknown>[] {
  return [...html.matchAll(/<script type="application\/ld\+json">([\s\S]*?)<\/script>/g)].map(
    ([, json]) => JSON.parse(json!) as Record<string, unknown>,
  );
}

for (const { path, lang } of pages) {
  test(`${path}: its picture when shared, and its data`, async ({ request }) => {
    const html = await (await request.get(path)).text();
    const image = `${origin}/og/${lang}.png`;
    expect(html).toContain(`<meta property="og:image" content="${image}" />`);
    expect(html).toContain('<meta property="og:image:width" content="1200" />');
    expect(html).toContain('<meta property="og:image:height" content="630" />');
    expect(html).toContain('<meta name="twitter:card" content="summary_large_image" />');

    // The picture is there, a PNG of 1200 × 630 (its header: the size in the IHDR chunk).
    const png = await request.get(`/og/${lang}.png`);
    expect(png.status()).toBe(200);
    const bytes = await png.body();
    expect(bytes.subarray(1, 4).toString('latin1')).toBe('PNG');
    expect([bytes.readUInt32BE(16), bytes.readUInt32BE(20)]).toEqual([1200, 630]);

    const [app, faq] = structured(html);
    expect(app).toMatchObject({
      '@context': 'https://schema.org',
      '@type': 'SoftwareApplication',
      name: 'Capriole',
      inLanguage: lang,
      operatingSystem: 'Web, Windows, Linux',
      offers: { '@type': 'Offer', price: '0', priceCurrency: 'EUR' },
      publisher: { name: 'Bobolar Ltd' },
      sameAs: ['https://github.com/capriole-app/capriole'],
      image,
    });
    expect(app).not.toHaveProperty('aggregateRating');
    expect(faq).toMatchObject({ '@type': 'FAQPage', inLanguage: lang });
    const questions = faq!.mainEntity as { name: string; acceptedAnswer: { text: string } }[];
    expect(questions).toHaveLength(6);
    for (const q of questions) expect(q.acceptedAnswer.text.length).toBeGreaterThan(20);
  });
}

test('the questions of the data are the ones the page shows, in their order', async ({ page, request }) => {
  for (const lang of ['it', 'de']) {
    const [, faq] = structured(await (await request.get(`/${lang}/`)).text());
    const questions = (faq!.mainEntity as { name: string; acceptedAnswer: { text: string } }[]).map((q) => q.name);
    await page.goto(`/${lang}/`);
    await expect(page.locator('#faq summary')).toHaveText(questions);
    const first = page.locator('#faq details').first();
    await first.locator('summary').click();
    await expect(first.locator('p')).toHaveText(
      (faq!.mainEntity as { acceptedAnswer: { text: string } }[])[0]!.acceptedAnswer.text,
    );
  }
});

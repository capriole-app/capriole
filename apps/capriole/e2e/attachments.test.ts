import { expect, test } from '@playwright/test';
import { readFileSync } from 'node:fs';
import { withoutFileAccess } from './downloads';

/** One blank page. */
function pdf(): Buffer {
  const bodies = [
    '<</Type/Catalog/Pages 2 0 R>>',
    '<</Type/Pages/Kids[3 0 R]/Count 1>>',
    '<</Type/Page/Parent 2 0 R/MediaBox[0 0 595 842]>>',
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

/** An EN 16931 invoice (the shape of corpus-tools/check_attachments.py's, valid against its XSD). */
const invoice = `<?xml version="1.0" encoding="UTF-8"?>
<rsm:CrossIndustryInvoice xmlns:rsm="urn:un:unece:uncefact:data:standard:CrossIndustryInvoice:100"
 xmlns:ram="urn:un:unece:uncefact:data:standard:ReusableAggregateBusinessInformationEntity:100"
 xmlns:udt="urn:un:unece:uncefact:data:standard:UnqualifiedDataType:100">
  <rsm:ExchangedDocumentContext>
    <ram:GuidelineSpecifiedDocumentContextParameter><ram:ID>urn:cen.eu:en16931:2017</ram:ID></ram:GuidelineSpecifiedDocumentContextParameter>
  </rsm:ExchangedDocumentContext>
  <rsm:ExchangedDocument>
    <ram:ID>FA-2026-0042</ram:ID><ram:TypeCode>380</ram:TypeCode>
    <ram:IssueDateTime><udt:DateTimeString format="102">20261003</udt:DateTimeString></ram:IssueDateTime>
  </rsm:ExchangedDocument>
  <rsm:SupplyChainTradeTransaction>
    <ram:ApplicableHeaderTradeAgreement>
      <ram:SellerTradeParty><ram:Name>Capriole S.r.l.</ram:Name>
        <ram:SpecifiedTaxRegistration><ram:ID schemeID="VA">IT01234567890</ram:ID></ram:SpecifiedTaxRegistration>
      </ram:SellerTradeParty>
      <ram:BuyerTradeParty><ram:Name>Cliente S.p.A.</ram:Name></ram:BuyerTradeParty>
    </ram:ApplicableHeaderTradeAgreement>
    <ram:ApplicableHeaderTradeSettlement>
      <ram:InvoiceCurrencyCode>EUR</ram:InvoiceCurrencyCode>
      <ram:SpecifiedTradeSettlementHeaderMonetarySummation>
        <ram:TaxBasisTotalAmount>100.00</ram:TaxBasisTotalAmount>
        <ram:TaxTotalAmount currencyID="EUR">22.00</ram:TaxTotalAmount>
        <ram:GrandTotalAmount>122.00</ram:GrandTotalAmount>
        <ram:DuePayableAmount>122.00</ram:DuePayableAmount>
      </ram:SpecifiedTradeSettlementHeaderMonetarySummation>
    </ram:ApplicableHeaderTradeSettlement>
  </rsm:SupplyChainTradeTransaction>
</rsm:CrossIndustryInvoice>
`;

test.beforeEach(({ page }) => withoutFileAccess(page));

test('attaches, downloads and removes files; makes the PDF a Factur-X invoice; saves', async ({ page }) => {
  await page.goto('/');
  await expect(page.getByText(/motore Capriole \d+\.\d+\.\d+/)).toBeVisible();
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'fattura.pdf', mimeType: 'application/pdf', buffer: pdf() });
  await expect(page.getByTestId('page')).toHaveCount(1);

  await page.getByRole('button', { name: /^Allegati/ }).click();
  const panel = page.getByTestId('attachments-panel');
  await expect(panel).toContainText('Nessun file allegato.');
  const list = page.getByTestId('attachment-list');

  // A file attached, downloaded as it went in.
  await page
    .getByTestId('attach-file')
    .setInputFiles({ name: 'note è.txt', mimeType: 'text/plain', buffer: Buffer.from('ciao, mondo') });
  await expect(list).toContainText('note è.txt');
  await expect(list).toContainText('supplemento');
  await expect(page.getByRole('button', { name: 'Allegati (1)' })).toBeVisible();
  const download = page.waitForEvent('download');
  await list.getByRole('button', { name: 'Scarica' }).click();
  const got = await download;
  expect(got.suggestedFilename()).toBe('note è.txt');
  expect(readFileSync(await got.path()).toString()).toBe('ciao, mondo');

  // Factur-X from an invoice's XML: its summary, and the note that the PDF is not PDF/A-3.
  await page
    .getByTestId('invoice-file')
    .setInputFiles({ name: 'fattura.xml', mimeType: 'text/xml', buffer: Buffer.from(invoice) });
  const card = page.getByTestId('invoice');
  await expect(card).toContainText('Profilo EN 16931');
  await expect(card).toContainText('FA-2026-0042 del 03/10/2026');
  await expect(card).toContainText('Capriole S.r.l. (IT01234567890)');
  await expect(card).toContainText('Cliente S.p.A.');
  await expect(card).toContainText('122,00');
  await expect(card.getByRole('note')).toContainText('non è PDF/A-3');
  await expect(list).toContainText('factur-x.xml');
  await expect(list).toContainText('versione alternativa');

  // Removed, and back with undo.
  await list.getByRole('button', { name: 'Rimuovi note è.txt' }).click();
  await expect(list).not.toContainText('note è.txt');
  await page.getByRole('button', { name: 'Annulla' }).click();
  await expect(list).toContainText('note è.txt');

  const saving = page.waitForEvent('download');
  await page.getByRole('button', { name: 'Salva', exact: true }).click();
  const savedPath = await (await saving).path();
  const saved = readFileSync(savedPath).toString('latin1');
  expect(saved).toMatch(/\/AFRelationship \/Alternative/);
  expect(saved).toMatch(/\/AFRelationship \/Supplement/);
  expect(saved).toMatch(/\(factur-x\.xml\)/);
  expect(saved).toMatch(/<fx:ConformanceLevel>EN 16931<\/fx:ConformanceLevel>/);
  expect(saved).toMatch(/\/Subtype \/text#2Fxml/);

  // Opened again: the invoice is found.
  await page.getByRole('button', { name: 'Chiudi il documento' }).click();
  await page
    .getByTestId('open-file')
    .setInputFiles({ name: 'fattura-x.pdf', mimeType: 'application/pdf', buffer: readFileSync(savedPath) });
  await expect(page.getByTestId('page')).toHaveCount(1);
  await page.getByRole('button', { name: /^Allegati/ }).click();
  await expect(page.getByTestId('invoice')).toContainText('Profilo EN 16931');
  await expect(page.getByTestId('attachment-list')).toContainText('note è.txt');
});

// What a Factur-X invoice says, read from its XML (UN/CEFACT Cross Industry Invoice) with the browser's
// own parser: the summary the app shows. Elements are found by their local names along the paths of
// the standard, whatever the prefixes; what is not there is left empty.

export interface Invoice {
  number: string;
  /** 380 invoice, 381 credit note, ... (UNTDID 1001). */
  typeCode: string;
  /** ISO date (YYYY-MM-DD) when written in format 102; else as written. */
  date: string;
  dueDate: string;
  seller: string;
  sellerVat: string;
  buyer: string;
  currency: string;
  net: string;
  tax: string;
  total: string;
  due: string;
}

function child(parent: Element | null, local: string): Element | null {
  if (!parent) return null;
  for (const element of parent.children) {
    if (element.localName === local) return element;
  }
  return null;
}

function path(root: Element | null, ...locals: string[]): Element | null {
  return locals.reduce<Element | null>((at, local) => child(at, local), root);
}

const text = (element: Element | null) => element?.textContent?.trim() ?? '';

function isoDate(element: Element | null): string {
  const value = text(element);
  return element?.getAttribute('format') === '102' && /^\d{8}$/.test(value)
    ? `${value.slice(0, 4)}-${value.slice(4, 6)}-${value.slice(6, 8)}`
    : value;
}

/** The summary of an invoice's XML; null when it is not a Cross Industry Invoice (or not XML). */
export function parseInvoice(xml: string): Invoice | null {
  const doc = new DOMParser().parseFromString(xml, 'application/xml');
  const root = doc.documentElement;
  if (doc.getElementsByTagName('parsererror').length > 0 || root?.localName !== 'CrossIndustryInvoice') return null;
  const document = child(root, 'ExchangedDocument');
  const transaction = child(root, 'SupplyChainTradeTransaction');
  const agreement = child(transaction, 'ApplicableHeaderTradeAgreement');
  const settlement = child(transaction, 'ApplicableHeaderTradeSettlement');
  const sums = child(settlement, 'SpecifiedTradeSettlementHeaderMonetarySummation');
  return {
    number: text(child(document, 'ID')),
    typeCode: text(child(document, 'TypeCode')),
    date: isoDate(path(document, 'IssueDateTime', 'DateTimeString')),
    dueDate: isoDate(path(settlement, 'SpecifiedTradePaymentTerms', 'DueDateDateTime', 'DateTimeString')),
    seller: text(path(agreement, 'SellerTradeParty', 'Name')),
    sellerVat: text(path(agreement, 'SellerTradeParty', 'SpecifiedTaxRegistration', 'ID')),
    buyer: text(path(agreement, 'BuyerTradeParty', 'Name')),
    currency: text(child(settlement, 'InvoiceCurrencyCode')),
    net: text(child(sums, 'TaxBasisTotalAmount')),
    tax: text(child(sums, 'TaxTotalAmount')),
    total: text(child(sums, 'GrandTotalAmount')),
    due: text(child(sums, 'DuePayableAmount')),
  };
}

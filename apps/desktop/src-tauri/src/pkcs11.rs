//! Smart cards through PKCS#11 (PLAN.md phase 11c, CAPRIOLE.md §6.1): what a web page cannot reach.
//! The card's own module (OpenSC, the CNS or CIE middleware: a shared library the person names) is
//! loaded for each call and finalized after it. The shell lists the cards and their certificates, and
//! has a card sign what the engine prepared (capriole_signing_to_sign_json): the key never leaves the
//! card, the PIN never leaves the shell (kept as a secret, wiped when dropped). Bytes go to and from
//! the page as hexadecimal strings, as the engine's JSON has them.

use cryptoki::context::{CInitializeArgs, CInitializeFlags, Pkcs11};
use cryptoki::error::{Error, RvError};
use cryptoki::mechanism::{Mechanism, MechanismType};
use cryptoki::object::{Attribute, AttributeType, ObjectClass};
use cryptoki::session::{Session, UserType};
use cryptoki::slot::Slot;
use cryptoki::types::AuthPin;
use serde::Serialize;

/// A card in a reader.
#[derive(Serialize, Debug)]
pub struct Token {
    pub slot: u64,
    pub label: String,
    pub manufacturer: String,
    pub model: String,
    pub serial: String,
}

/// A certificate on a card: its id (CKA_ID, which its private key shares), its label, its DER.
#[derive(Serialize, Debug)]
pub struct Certificate {
    pub id: String,
    pub label: String,
    pub der: String,
}

/// What the key signs, as capriole_signing_to_sign_json gives it.
#[derive(serde::Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct ToSign {
    pub hash: String,
    pub digest_info: String,
    pub data: String,
    pub key: String,
}

pub fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

pub fn unhex(text: &str) -> Result<Vec<u8>, String> {
    if !text.len().is_multiple_of(2) {
        return Err("hexadecimal digits in pairs expected".into());
    }
    (0..text.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&text[i..i + 2], 16).map_err(|_| "hexadecimal digits expected".to_string()))
        .collect()
}

/// An error as the page reads it: a code it can say in Italian ("pin-incorrect", "pin-locked",
/// "module", "no-key", "no-mechanism"), then what PKCS#11 said.
fn describe(error: &Error) -> String {
    match error {
        Error::Pkcs11(RvError::PinIncorrect, _) => "pin-incorrect: the PIN is not the card's".into(),
        Error::Pkcs11(RvError::PinLocked, _) => "pin-locked: the card's PIN is locked (too many wrong attempts)".into(),
        Error::Pkcs11(RvError::PinLenRange, _) => "pin-incorrect: the PIN has not the length the card wants".into(),
        Error::LibraryLoading(e) => format!("module: the PKCS#11 module cannot be loaded ({e})"),
        other => format!("pkcs11: {other}"),
    }
}

/// The module, loaded and initialized.
fn open(module: &str) -> Result<Pkcs11, String> {
    let pkcs11 = Pkcs11::new(module).map_err(|e| describe(&e))?;
    match pkcs11.initialize(CInitializeArgs::new(CInitializeFlags::OS_LOCKING_OK)) {
        Ok(()) | Err(Error::Pkcs11(RvError::CryptokiAlreadyInitialized, _)) => Ok(pkcs11),
        Err(e) => Err(describe(&e)),
    }
}

fn slot(pkcs11: &Pkcs11, id: u64) -> Result<Slot, String> {
    pkcs11
        .get_slots_with_token()
        .map_err(|e| describe(&e))?
        .into_iter()
        .find(|s| s.id() == id)
        .ok_or_else(|| "no-token: the card is not in its reader any more".to_string())
}

fn bytes_of(attributes: &[Attribute], kind: AttributeType) -> Vec<u8> {
    attributes
        .iter()
        .find_map(|a| match (a, kind) {
            (Attribute::Id(v), AttributeType::Id) | (Attribute::Label(v), AttributeType::Label) | (Attribute::Value(v), AttributeType::Value) => {
                Some(v.clone())
            }
            _ => None,
        })
        .unwrap_or_default()
}

pub fn tokens(module: &str) -> Result<Vec<Token>, String> {
    let pkcs11 = open(module)?;
    let mut out = Vec::new();
    for slot in pkcs11.get_slots_with_token().map_err(|e| describe(&e))? {
        let Ok(info) = pkcs11.get_token_info(slot) else {
            continue; // a reader whose card cannot be read: not listed
        };
        out.push(Token {
            slot: slot.id(),
            label: info.label().trim().to_string(),
            manufacturer: info.manufacturer_id().trim().to_string(),
            model: info.model().trim().to_string(),
            serial: info.serial_number().trim().to_string(),
        });
    }
    let _ = pkcs11.finalize();
    Ok(out)
}

/// The certificates a card shows without its PIN (those of its keys are public on signature cards).
pub fn certificates(module: &str, slot_id: u64) -> Result<Vec<Certificate>, String> {
    let pkcs11 = open(module)?;
    let session = pkcs11.open_ro_session(slot(&pkcs11, slot_id)?).map_err(|e| describe(&e))?;
    let mut out = Vec::new();
    for object in session.find_objects(&[Attribute::Class(ObjectClass::CERTIFICATE)]).map_err(|e| describe(&e))? {
        let attributes = session
            .get_attributes(object, &[AttributeType::Id, AttributeType::Label, AttributeType::Value])
            .map_err(|e| describe(&e))?;
        let der = bytes_of(&attributes, AttributeType::Value);
        if der.is_empty() {
            continue;
        }
        out.push(Certificate {
            id: hex(&bytes_of(&attributes, AttributeType::Id)),
            label: String::from_utf8_lossy(&bytes_of(&attributes, AttributeType::Label)).into_owned(),
            der: hex(&der),
        });
    }
    drop(session);
    let _ = pkcs11.finalize();
    Ok(out)
}

/// The mechanism a card offers for the key, and what it signs with it: RSA hashing on the card
/// (CKM_SHA256_RSA_PKCS over the signed attributes), else raw PKCS#1 v1.5 over the DigestInfo
/// (CKM_RSA_PKCS, what many national cards have); ECDSA over the hash (CKM_ECDSA), else hashing on
/// the card (CKM_ECDSA_SHA256).
fn mechanism_for(offered: &[MechanismType], to_sign: &ToSign) -> Result<(Mechanism<'static>, Vec<u8>), String> {
    let rsa = to_sign.key.starts_with("RSA");
    let choices: [(MechanismType, Mechanism<'static>, &str); 2] = if rsa {
        [
            (MechanismType::SHA256_RSA_PKCS, Mechanism::Sha256RsaPkcs, &to_sign.data),
            (MechanismType::RSA_PKCS, Mechanism::RsaPkcs, &to_sign.digest_info),
        ]
    } else {
        [(MechanismType::ECDSA, Mechanism::Ecdsa, &to_sign.hash), (MechanismType::ECDSA_SHA256, Mechanism::EcdsaSha256, &to_sign.data)]
    };
    for (kind, mechanism, input) in choices {
        if offered.contains(&kind) {
            return Ok((mechanism, unhex(input)?));
        }
    }
    Err(format!("no-mechanism: the card offers no mechanism to sign with its {} key", to_sign.key))
}

fn sign_in(session: &Session, offered: &[MechanismType], key_id: &[u8], to_sign: &ToSign) -> Result<Vec<u8>, String> {
    let keys = session
        .find_objects(&[Attribute::Class(ObjectClass::PRIVATE_KEY), Attribute::Id(key_id.to_vec())])
        .map_err(|e| describe(&e))?;
    let key = *keys.first().ok_or_else(|| "no-key: the card has no private key for this certificate".to_string())?;
    let (mechanism, input) = mechanism_for(offered, to_sign)?;
    session.sign(&mechanism, key, &input).map_err(|e| describe(&e))
}

/// The card signs with the key of the certificate key_id (hex): the value, hexadecimal — RSA PKCS#1
/// v1.5, or ECDSA as r‖s, which the engine takes as they are.
pub fn sign(module: &str, slot_id: u64, key_id: &str, pin: AuthPin, to_sign: &ToSign) -> Result<String, String> {
    let pkcs11 = open(module)?;
    let slot = slot(&pkcs11, slot_id)?;
    let offered = pkcs11.get_mechanism_list(slot).map_err(|e| describe(&e))?;
    let session = pkcs11.open_ro_session(slot).map_err(|e| describe(&e))?;
    session.login(UserType::User, Some(&pin)).map_err(|e| describe(&e))?;
    let signed = sign_in(&session, &offered, &unhex(key_id)?, to_sign);
    let _ = session.logout();
    drop(session);
    let _ = pkcs11.finalize();
    signed.map(|v| hex(&v))
}

#[cfg(test)]
mod tests {
    //! Against a software token (SoftHSM): CAPRIOLE_PKCS11_MODULE names its module, SOFTHSM2_CONF its
    //! configuration, and the token holds an RSA key and its certificate with id 01, PIN 1234
    //! (corpus-tools/check_signatures.py --pkcs11 makes one; scripts/test_desktop_pkcs11.sh runs these).
    //! Without CAPRIOLE_PKCS11_MODULE they are not run — and say so.
    use super::*;

    fn module() -> Option<String> {
        let m = std::env::var("CAPRIOLE_PKCS11_MODULE").ok();
        if m.is_none() {
            eprintln!("CAPRIOLE_PKCS11_MODULE not set: the PKCS#11 tests are not run");
        }
        m
    }

    #[test]
    fn hex_both_ways() {
        assert_eq!(hex(&[0, 0xab, 0x10]), "00ab10");
        assert_eq!(unhex("00ab10").unwrap(), vec![0, 0xab, 0x10]);
        assert!(unhex("abc").is_err());
        assert!(unhex("zz").is_err());
    }

    #[test]
    fn a_module_that_is_not_one_is_said_so() {
        let error = tokens("/nonexistent/module.so").unwrap_err();
        assert!(error.starts_with("module:"), "{error}");
    }

    #[test]
    fn lists_signs_and_refuses_a_wrong_pin() {
        let Some(module) = module() else { return };
        let found = tokens(&module).unwrap();
        let token = found.first().expect("a token");
        let certs = certificates(&module, token.slot).unwrap();
        let cert = certs.iter().find(|c| c.id == "01").expect("the certificate with id 01");
        assert!(cert.der.starts_with("30"));
        // A SHA-256 DigestInfo and "data" for an RSA key: the card picks its mechanism.
        let to_sign = ToSign {
            hash: "00".repeat(32),
            digest_info: format!("3031300d060960864801650304020105000420{}", "00".repeat(32)),
            data: hex(b"signed attributes"),
            key: "RSA 2048".into(),
        };
        let value = sign(&module, token.slot, "01", AuthPin::from("1234"), &to_sign).unwrap();
        assert_eq!(value.len(), 512, "an RSA 2048 value");
        eprintln!("signed on the token: {}…", &value[..16]);
        let wrong = sign(&module, token.slot, "01", AuthPin::from("0000"), &to_sign).unwrap_err();
        assert!(wrong.starts_with("pin-incorrect"), "{wrong}");
        let none = sign(&module, token.slot, "7f", AuthPin::from("1234"), &to_sign).unwrap_err();
        assert!(none.starts_with("no-key"), "{none}");
    }
}

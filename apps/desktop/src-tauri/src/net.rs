//! The network, from the shell only (PLAN.md phase 11c, CAPRIOLE.md §6.1): the engine makes no calls,
//! and many time-stamping authorities, OCSP responders and CRL servers send no CORS headers, so a page
//! cannot ask them. Four requests, each for one purpose, nothing else: a time stamp (the request the
//! engine made: the document's digest, nothing of the document), an OCSP answer (the certificate's
//! serial), a CRL, an issuer's certificate. http and https only, a size cap and a time limit; what
//! comes back is checked by the engine.

use std::time::Duration;

use ureq::Agent;

/// Answers larger than this are refused (a CRL of a large authority is a few megabytes).
const MAX_BYTES: u64 = 16 * 1024 * 1024;
const TIMEOUT: Duration = Duration::from_secs(20);

/// What the page asks for.
#[derive(serde::Deserialize, Debug, Clone, Copy, PartialEq, Eq)]
#[serde(rename_all = "lowercase")]
pub enum Purpose {
    Timestamp,
    Ocsp,
    Crl,
    Certificate,
}

fn agent() -> Agent {
    Agent::config_builder()
        .timeout_global(Some(TIMEOUT))
        .http_status_as_error(true)
        .max_redirects(3)
        .user_agent("Capriole")
        .build()
        .into()
}

/// The URL, when it may be asked: http or https, nothing else (no file:, no local paths).
pub fn checked(url: &str) -> Result<&str, String> {
    let lower = url.to_ascii_lowercase();
    if (lower.starts_with("http://") || lower.starts_with("https://")) && !url.contains(char::is_whitespace) {
        Ok(url)
    } else {
        Err(format!("url: only http and https addresses are asked ({url})"))
    }
}

/// Sends the request for purpose to url (POST of body for a time stamp or an OCSP answer, GET
/// otherwise) and gives the answer's bytes.
pub fn fetch(purpose: Purpose, url: &str, body: &[u8]) -> Result<Vec<u8>, String> {
    let url = checked(url)?;
    let agent = agent();
    let response = match purpose {
        Purpose::Timestamp => agent.post(url).header("Content-Type", "application/timestamp-query").send(body),
        Purpose::Ocsp => agent.post(url).header("Content-Type", "application/ocsp-request").send(body),
        Purpose::Crl | Purpose::Certificate => agent.get(url).call(),
    };
    let mut response = response.map_err(|e| format!("network: {url}: {e}"))?;
    response
        .body_mut()
        .with_config()
        .limit(MAX_BYTES)
        .read_to_vec()
        .map_err(|e| format!("network: {url}: {e}"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_http_and_https_are_asked() {
        assert!(checked("http://127.0.0.1:9/ocsp").is_ok());
        assert!(checked("HTTPS://tsa.example/").is_ok());
        for bad in ["file:///etc/passwd", "ftp://x/", "/tmp/x", "http://a b/", "javascript:alert(1)"] {
            assert!(checked(bad).is_err(), "{bad}");
        }
    }

    /// A server on a local port that answers one request with answer, and gives back what it read.
    fn serve_once(answer: &'static [u8]) -> (String, std::thread::JoinHandle<Vec<u8>>) {
        use std::io::{Read, Write};
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let url = format!("http://{}/tsa", listener.local_addr().unwrap());
        let handle = std::thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            stream.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
            let mut read = Vec::new();
            let mut buffer = [0u8; 4096];
            // The head, then as much body as it says.
            while let Ok(n) = stream.read(&mut buffer) {
                read.extend_from_slice(&buffer[..n]);
                let text = String::from_utf8_lossy(&read).to_string();
                if let Some(end) = text.find("\r\n\r\n") {
                    let length = text
                        .lines()
                        .find_map(|l| l.to_ascii_lowercase().strip_prefix("content-length:").map(|v| v.trim().parse::<usize>().unwrap_or(0)))
                        .unwrap_or(0);
                    if read.len() >= end + 4 + length {
                        break;
                    }
                }
                if n == 0 {
                    break;
                }
            }
            let head = format!("HTTP/1.1 200 OK\r\nContent-Type: application/timestamp-reply\r\nContent-Length: {}\r\nConnection: close\r\n\r\n", answer.len());
            stream.write_all(head.as_bytes()).unwrap();
            stream.write_all(answer).unwrap();
            read
        });
        (url, handle)
    }

    #[test]
    fn a_time_stamp_request_is_posted_and_its_answer_read() {
        let (url, server) = serve_once(b"\x30\x03\x02\x01\x00");
        let answer = fetch(Purpose::Timestamp, &url, b"query").unwrap();
        assert_eq!(answer, b"\x30\x03\x02\x01\x00");
        let request = String::from_utf8_lossy(&server.join().unwrap()).to_string();
        assert!(request.starts_with("POST /tsa "), "{request}");
        assert!(request.to_ascii_lowercase().contains("content-type: application/timestamp-query"), "{request}");
        assert!(request.ends_with("query"), "{request}");
    }

    #[test]
    fn nothing_listening_is_an_error_not_a_hang() {
        let error = fetch(Purpose::Crl, "http://127.0.0.1:9/ca.crl", &[]).unwrap_err();
        assert!(error.starts_with("network:"), "{error}");
    }
}

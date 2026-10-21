//! `SYSTEM.CNF` parser, the same rules as `scripts/gt4disc.py::read_config`.

/// The fields setup needs from `SYSTEM.CNF`.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SystemConfig {
    /// `/SCUS_973.28;1`, the root-level path of the boot executable.
    pub boot_path: String,
    /// `SCUS-97328`.
    pub serial: String,
    /// `VER` value, for example `2.00`.
    pub version: String,
    /// `VMODE` value, for example `NTSC`.
    pub video_mode: String,
}

/// Parses `SYSTEM.CNF`. Only the root-level `cdrom0:\XXXX_999.99;1` form is supported.
pub fn parse(bytes: &[u8]) -> Result<SystemConfig, String> {
    if !bytes.is_ascii() {
        return Err("SYSTEM.CNF is not ASCII text".into());
    }
    let text = std::str::from_utf8(bytes).map_err(|_| "SYSTEM.CNF is not text".to_string())?;
    let mut fields: Vec<(String, String)> = Vec::new();
    for line in text.lines().filter(|line| !line.trim().is_empty()) {
        let (key, value) = line
            .split_once('=')
            .ok_or_else(|| "SYSTEM.CNF has a line without '='".to_string())?;
        let key = key.trim().to_string();
        if fields.iter().any(|(existing, _)| *existing == key) {
            return Err(format!("SYSTEM.CNF repeats {key}"));
        }
        fields.push((key, value.trim().to_string()));
    }
    let field = |name: &str| fields.iter().find(|(key, _)| key == name).map(|(_, value)| value.clone());
    let boot2 = field("BOOT2").ok_or("SYSTEM.CNF has no BOOT2 line")?;
    let version = field("VER").ok_or("SYSTEM.CNF has no VER line")?;
    let video_mode = field("VMODE").ok_or("SYSTEM.CNF has no VMODE line")?;
    let name = boot2
        .strip_prefix("cdrom0:\\")
        .and_then(|rest| rest.strip_suffix(";1"))
        .filter(|name| is_boot_name(name))
        .ok_or_else(|| format!("unsupported BOOT2 path: {boot2}"))?;
    Ok(SystemConfig {
        boot_path: format!("/{name};1"),
        serial: format!("{}-{}", &name[..4], name[5..].replace('.', "")),
        version,
        video_mode,
    })
}

/// `XXXX_999.99` with uppercase letters and digits as PS2 boot names use.
fn is_boot_name(name: &str) -> bool {
    let bytes = name.as_bytes();
    bytes.len() == 11
        && bytes[..4].iter().all(|byte| byte.is_ascii_uppercase())
        && bytes[4] == b'_'
        && bytes[5..8].iter().all(|byte| byte.is_ascii_digit())
        && bytes[8] == b'.'
        && bytes[9..11].iter().all(|byte| byte.is_ascii_digit())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_the_gt4_boot_line() {
        let text = b"BOOT2 = cdrom0:\\SCUS_973.28;1\r\nVER = 2.00\r\nVMODE = NTSC\r\n";
        let config = parse(text).unwrap();
        assert_eq!(config.serial, "SCUS-97328");
        assert_eq!(config.boot_path, "/SCUS_973.28;1");
        assert_eq!(config.version, "2.00");
        assert_eq!(config.video_mode, "NTSC");
    }

    #[test]
    fn rejects_other_boot_paths_missing_fields_and_duplicates() {
        assert!(parse(b"BOOT2 = cdrom0:\\SCES_500.01;1\nVER = 1.00\nVMODE = PAL\n").is_ok());
        assert!(parse(b"BOOT2 = cdrom0:\\SCUS_973.28\nVER = 2.00\nVMODE = NTSC\n").is_err());
        assert!(parse(b"BOOT2 = host:\\SCUS_973.28;1\nVER = 2.00\nVMODE = NTSC\n").is_err());
        assert!(parse(b"VER = 2.00\nVMODE = NTSC\n").is_err());
        assert!(parse(b"BOOT2 = cdrom0:\\SCUS_973.28;1\nBOOT2 = x\nVER = 2.00\nVMODE = NTSC\n").is_err());
        assert!(parse("BOOT2 = caf\u{e9}\n".as_bytes()).is_err());
    }
}

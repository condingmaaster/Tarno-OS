// SPDX-License-Identifier: GPL-2.0-or-later
//! Disposable VM disks live apart from seeds, releases, logs and screenshots.
use std::{fs, path::{Path, PathBuf}, process::Command, time::{SystemTime, UNIX_EPOCH}};
use std::os::unix::fs::MetadataExt;

pub const RESERVE: u64 = 10 * 1024 * 1024 * 1024;

pub fn require_space(directory: &Path, additional: u64) {
    let out = Command::new("df").args(["-B1", "--output=avail", "--"])
        .arg(directory).output().expect("query available disk space");
    assert!(out.status.success(), "cannot determine available space before writing artifacts");
    let available = String::from_utf8_lossy(&out.stdout).lines().last()
        .and_then(|s| s.trim().parse::<u64>().ok()).expect("parse available disk bytes");
    assert!(available >= RESERVE.saturating_add(additional),
        "disk space guard: {} MiB available; keep 10 GiB free plus {} MiB for this operation",
        available >> 20, additional >> 20);
}

pub struct Session { root: PathBuf, dev: u64, ino: u64, keep: bool }
impl Session {
    pub fn start(target: &Path, keep: bool) -> Self {
        let parent = target.join("test-scratch");
        fs::create_dir_all(&parent).expect("create scratch parent");
        let meta = fs::symlink_metadata(&parent).unwrap();
        assert!(meta.is_dir() && !meta.file_type().is_symlink(), "scratch parent must be a real directory");
        let stamp = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos();
        let root = parent.join(format!("run-{}-{stamp}", std::process::id()));
        fs::create_dir(&root).expect("create unique scratch directory");
        let meta = fs::symlink_metadata(&root).unwrap();
        Self { root, dev: meta.dev(), ino: meta.ino(), keep }
    }
    pub fn root(&self) -> &Path { &self.root }
}
impl Drop for Session {
    fn drop(&mut self) {
        if self.keep || std::thread::panicking() {
            eprintln!("test scratch retained for diagnosis: {}", self.root.display());
            return;
        }
        // Never remove a replacement directory or a release/seed selected by a caller.
        match fs::symlink_metadata(&self.root) {
            Ok(m) if m.is_dir() && !m.file_type().is_symlink() && m.dev() == self.dev && m.ino() == self.ino => {
                if let Err(e) = fs::remove_dir_all(&self.root) {
                    eprintln!("cannot clean test scratch {}: {e}", self.root.display());
                }
            }
            _ => eprintln!("scratch identity changed; refusing cleanup: {}", self.root.display()),
        }
    }
}

#[cfg(test)] mod tests {
    use super::*;
    fn temporary() -> PathBuf {
        let p = std::env::temp_dir().join(format!("thos-scratch-test-{}-{}", std::process::id(), SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos()));
        fs::create_dir(&p).unwrap(); p
    }
    #[test] fn cleanup_preserves_seed_and_logs_and_does_not_follow_links() {
        let t = temporary(); fs::write(t.join("seed.img"), b"seed").unwrap();
        fs::write(t.join("serial.log"), b"proof").unwrap();
        let root;
        { let s = Session::start(&t, false); root = s.root().to_owned();
          fs::write(root.join("run.img"), b"guest writes").unwrap();
          std::os::unix::fs::symlink(t.join("seed.img"), root.join("seed-link")).unwrap(); }
        assert!(!root.exists()); assert_eq!(fs::read(t.join("seed.img")).unwrap(), b"seed");
        assert_eq!(fs::read(t.join("serial.log")).unwrap(), b"proof"); fs::remove_dir_all(t).unwrap();
    }
    #[test] fn explicitly_kept_run_survives() {
        let t = temporary(); let root;
        { let s = Session::start(&t, true); root = s.root().to_owned(); fs::write(root.join("failed.img"), b"diagnosis").unwrap(); }
        assert_eq!(fs::read(root.join("failed.img")).unwrap(), b"diagnosis"); fs::remove_dir_all(t).unwrap();
    }
    #[test] fn replacement_directory_is_never_deleted() {
        let t = temporary(); let s = Session::start(&t, false); let root = s.root().to_owned();
        // Keep the original inode allocated so the filesystem cannot reuse it.
        fs::rename(&root, t.join("original")).unwrap(); fs::create_dir(&root).unwrap();
        fs::write(root.join("unrelated"), b"preserve").unwrap(); drop(s);
        assert_eq!(fs::read(root.join("unrelated")).unwrap(), b"preserve"); fs::remove_dir_all(t).unwrap();
    }
    #[test] fn insufficient_headroom_stops_before_creating_artifacts() {
        let t = temporary();
        assert!(std::panic::catch_unwind(|| require_space(&t, u64::MAX)).is_err());
        assert_eq!(fs::read_dir(&t).unwrap().count(), 0);
        fs::remove_dir_all(t).unwrap();
    }
}

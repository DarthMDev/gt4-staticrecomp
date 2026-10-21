//! The launcher's self-update starts the GT4Recomp setup and exits. The launcher lives in a kill-on-close job, so a
//! setup started inside that job died with it (launcher 1.0.0 bug, 2026-10-21). These tests run the real job code in
//! a child copy of this test binary: the child joins the job, starts a helper that writes a marker file two seconds
//! later, and exits at once. The helper started with `spawn_outside_job` must survive; a plain start must not.
#![cfg(windows)]

use std::os::windows::process::CommandExt;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant};

const CHILD_MARKER: &str = "GT4L_JOB_TEST_MARKER";
const CHILD_MODE: &str = "GT4L_JOB_TEST_MODE";

/// In the child copy: join the launcher job, start the helper, exit without waiting.
fn run_child_role() -> bool {
    let (Ok(marker), Ok(mode)) = (std::env::var(CHILD_MARKER), std::env::var(CHILD_MODE)) else {
        return false;
    };
    gt4l_platform::process::join_kill_on_close_job().expect("join the kill-on-close job");
    let mut helper = Command::new("cmd.exe");
    helper.raw_arg(format!("/d /c ping -n 3 127.0.0.1 >nul & echo survived>\"{marker}\""));
    let started = match mode.as_str() {
        "outside" => gt4l_platform::process::spawn_outside_job(&mut helper),
        _ => helper.spawn(),
    };
    started.expect("start the helper");
    std::process::exit(0);
}

fn marker_path(name: &str) -> PathBuf {
    let path = std::env::temp_dir().join(format!("gt4l-job-{name}-{}.txt", std::process::id()));
    let _ = std::fs::remove_file(&path);
    path
}

/// Runs this test (by name) again as the child role and waits for it to exit.
fn run_child(test_name: &str, marker: &Path, mode: &str) {
    let status = Command::new(std::env::current_exe().unwrap())
        .args(["--exact", test_name, "--test-threads=1"])
        .env(CHILD_MARKER, marker)
        .env(CHILD_MODE, mode)
        .status()
        .expect("run the child copy");
    assert!(status.success(), "the child copy failed: {status}");
}

fn marker_appears(marker: &Path, within: Duration) -> bool {
    let start = Instant::now();
    while start.elapsed() < within {
        if marker.exists() {
            return true;
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    false
}

#[test]
fn a_process_started_outside_the_job_survives_the_launcher_exit() {
    if run_child_role() {
        return;
    }
    let marker = marker_path("outside");
    run_child("a_process_started_outside_the_job_survives_the_launcher_exit", &marker, "outside");
    assert!(marker_appears(&marker, Duration::from_secs(10)), "the setup stand-in died with the launcher job");
    let _ = std::fs::remove_file(&marker);
}

#[test]
fn a_process_started_inside_the_job_ends_with_the_launcher() {
    if run_child_role() {
        return;
    }
    let marker = marker_path("inside");
    run_child("a_process_started_inside_the_job_ends_with_the_launcher", &marker, "inside");
    assert!(!marker_appears(&marker, Duration::from_secs(5)), "a child in the job outlived the launcher");
}

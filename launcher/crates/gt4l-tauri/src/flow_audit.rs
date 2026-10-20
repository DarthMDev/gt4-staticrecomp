//! The launcher flow, driven the way the page drives it (docs/launcher/FLOW-AUDIT.md): the same command bodies the
//! Tauri commands call, in the page's order, without a window.
//!
//! - `synthetic_flow_end_to_end` runs on every `cargo test` with a small synthetic disc (no game data) and a fake
//!   renderer installer.
//! - `real_flow_end_to_end` is ignored by default. It uses the owner's discs (read-only), copies the real image into
//!   a sandbox data folder and downloads the real gt4-gs release from GitHub:
//!     set GT4_AUDIT_DATA_DIR=<fresh scratch folder>
//!     set GT4_REAL_ISO=<Gran Turismo 4 (USA) (v2.00).iso>
//!     set GT4_SPEC2_ISO=<Gran Turismo 4 Spec II v1.10.iso>        (optional)
//!     set GT4_AUDIT_GAME_DIR=<folder with a stand-in gt4vulkan.exe>  (optional; Play is planned, never started)
//!     cargo test -p gt4l-tauri --release real_flow -- --ignored --nocapture
//!
//! Play is never started here: the test stops at the launch plan (program, arguments, environment).

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use gt4l_core::launch::{self, LaunchInputs};
use gt4l_core::paths::DataDirs;
use gt4l_disc::testing::{build_iso, pins_for, synthetic_game, system_cnf};
use gt4l_disc::Pins;
use serde_json::{json, Value};

use crate::commands::{self, Kind};
use crate::setup::{self, Notify, RendererInstaller};
use crate::state::{lock, AppState, Shared};

/// Every event the page would have received, in order.
#[derive(Clone, Default)]
struct Recorder {
    events: Arc<Mutex<Vec<(String, Value)>>>,
}

impl Recorder {
    fn notify(&self) -> Notify {
        let events = self.events.clone();
        Arc::new(move |name: &str, payload: Value| events.lock().unwrap().push((name.to_string(), payload)))
    }

    fn names(&self, name: &str) -> Vec<Value> {
        self.events.lock().unwrap().iter().filter(|(event, _)| event == name).map(|(_, payload)| payload.clone()).collect()
    }
}

fn launch(dirs: &DataDirs, pins: &Pins) -> Shared {
    Arc::new(Mutex::new(AppState::load_with_pins(dirs.clone(), pins.clone()).expect("state loads")))
}

fn screen(shared: &Shared) -> Value {
    commands::app_state_of(&lock(shared))
}

fn setup_state(shared: &Shared) -> String {
    screen(shared)["setup"]["state"].as_str().unwrap().to_string()
}

/// `setup_start`, then wait like the page's poll until the thread ends. Returns the states seen while polling.
fn run_setup(shared: &Shared, recorder: &Recorder, renderer: RendererInstaller) -> Vec<String> {
    let handle = setup::start_with(shared, recorder.notify(), renderer).expect("setup starts");
    let mut seen = Vec::new();
    while !handle.is_finished() {
        let view = screen(shared);
        let label = format!("{}:{}", view["setup"]["state"], view["setup"]["step"]);
        if seen.last() != Some(&label) {
            seen.push(label);
        }
        std::thread::sleep(Duration::from_millis(5));
    }
    handle.join().unwrap();
    seen
}

fn fake_renderer() -> RendererInstaller {
    Arc::new(|_: &DataDirs, progress: &mut dyn FnMut(gt4l_core::gs_release::Step)| {
        use gt4l_core::gs_release::Step;
        progress(Step::Listing);
        progress(Step::Downloading);
        progress(Step::Transfer { done: 50, total: Some(100) });
        progress(Step::Transfer { done: 100, total: Some(100) });
        progress(Step::Installing);
        Ok("1.0.1".to_string())
    })
}

fn offline_renderer() -> RendererInstaller {
    Arc::new(|_: &DataDirs, progress: &mut dyn FnMut(gt4l_core::gs_release::Step)| {
        progress(gt4l_core::gs_release::Step::Listing);
        Err("release list: offline".to_string())
    })
}

fn scratch(name: &str) -> PathBuf {
    let root = std::env::temp_dir().join(format!("gt4l-flow-{name}-{}", std::process::id()));
    let _ = fs::remove_dir_all(&root);
    fs::create_dir_all(&root).unwrap();
    root
}

/// A synthetic memory card with one synthetic save (no game data).
fn synthetic_card(path: &Path) {
    use gt4saves::card::{DirEntry, Node, GAME_SAVE_NAME, MODE_DIR_DEFAULT, MODE_FILE_DEFAULT};
    let stamp = [0, 39, 50, 7, 12, 10, 0xEA, 0x07];
    let file = Node::file(DirEntry::new("icon.sys", MODE_FILE_DEFAULT, stamp, stamp).unwrap(), vec![3; 964]);
    let save = Node::dir(DirEntry::new(GAME_SAVE_NAME, MODE_DIR_DEFAULT, stamp, stamp).unwrap(), vec![file]);
    fs::write(path, gt4saves::Card::build(&[save]).unwrap().to_image()).unwrap();
}

/// The settings, saves, controls and about steps of the walk. Returns a short log.
fn walk_pages(shared: &Shared, card: &Path) -> Vec<String> {
    let mut log = Vec::new();
    let table = commands::get_settings_table_of(shared).expect("settings table");
    let rows = table["rows"].as_array().unwrap();
    assert!(rows.iter().any(|row| row["id"] == "renderer"), "renderer row");
    commands::set_setting_of("fxaa".into(), json!(true), shared).expect("fxaa on");
    let table = commands::get_settings_table_of(shared).unwrap();
    let fxaa = table["rows"].as_array().unwrap().iter().find(|row| row["id"] == "fxaa").unwrap().clone();
    assert_eq!(fxaa["current"], json!(true));
    assert!(commands::set_setting_of("no_such_setting".into(), json!(1), shared).is_err());
    let reset = commands::reset_gs_settings_to_defaults_of(shared).expect("reset");
    log.push(format!("settings: {} rows, fxaa set and read back, reset ok", reset["rows"].as_array().unwrap().len()));

    let before = commands::saves_list_of(shared).unwrap().items.len();
    synthetic_card(card);
    let after = commands::import_into(shared, card, Kind::Saves).expect("import");
    assert!(after.items.len() > before, "the card's save is in the library: {}", after.status);
    let again = commands::import_into(shared, card, Kind::Saves).expect("second import");
    assert_eq!(again.items.len(), after.items.len(), "imports never duplicate: {}", again.status);
    let replays = commands::replays_list_of(shared).unwrap();
    log.push(format!("saves: {} -> {} items ({}); replays: {}", before, after.items.len(), after.status, replays.items.len()));

    let controls = commands::get_controls_of(shared).expect("controls");
    assert!(controls["controls"].as_array().unwrap().len() >= 16);
    commands::reset_binding_of("cross".into(), shared).expect("reset cross");
    log.push(format!("controls: {} inputs, cross reset", controls["controls"].as_array().unwrap().len()));

    let about = commands::about_info_of(shared).expect("about");
    assert!(about.game_files.ends_with("disc"), "{}", about.game_files);
    log.push(format!("about: GT4Recomp {} gt4-gs {} files {}", about.gt4recomp, about.gt4gs, about.game_files));
    log
}

/// What Play would run, without running it.
fn play_plan(shared: &Shared, program_dir: &Path) -> Result<launch::LaunchPlan, String> {
    let inputs = commands::prepare_play(shared, program_dir)?;
    let session = inputs.dirs.session("session-plan-only");
    let card = inputs.dirs.runtime_card();
    let launch_inputs = LaunchInputs {
        game_executable: &inputs.executable,
        elf: &inputs.elf,
        disc: &inputs.disc,
        card: &card,
        session_dir: &session,
    };
    let inherited: Vec<String> = std::env::vars().map(|(name, _)| name).collect();
    Ok(match &inputs.pcsx2 {
        Some(installed) => {
            let paths = launch::Pcsx2Paths::for_session(installed, &inputs.dirs.root().join("gs"), &session);
            launch::plan_pcsx2_launch(&launch_inputs, &inputs.settings, &paths, &inherited)
        }
        None => launch::plan_launch(&launch_inputs, &inputs.settings, &inherited),
    })
}

#[test]
fn synthetic_flow_end_to_end() {
    let root = scratch("synthetic");
    let data = root.join("data");
    let dirs = DataDirs::new(&data);
    let game = synthetic_game(0x0010_0000, 0x0010_0008, b"synthetic guest code, not game data, 0123456789abcdef");
    let image = |version: &str, serial_boot: &str| {
        build_iso(
            "GT4 USA",
            &[
                ("SYSTEM.CNF;1", system_cnf(serial_boot, version, "NTSC")),
                ("CORE.GT4;1", game.core.clone()),
                ("SCUS_973.28;1", vec![0xA5; 3000]),
            ],
        )
    };
    let good = image("2.00", "SCUS_973.28");
    let pins = pins_for(&good, &game, "SCUS-97328", "2.00");
    let picked = root.join("picked").join("Gran Turismo 4 (USA) (v2.00).iso");
    fs::create_dir_all(picked.parent().unwrap()).unwrap();
    fs::write(&picked, &good).unwrap();

    // First launch: Setup, never Home.
    let shared = launch(&dirs, &pins);
    assert_eq!(setup_state(&shared), "needed");

    // Refused files: not an ISO, a truncated copy, Spec II. The page shows the message and "Choose another file".
    let text = root.join("notes.iso");
    fs::write(&text, b"not a disc image").unwrap();
    assert!(setup::choose_disc_path(&shared, &text).is_err());
    assert_eq!(setup_state(&shared), "wrong_disc");
    let truncated = root.join("truncated.iso");
    fs::write(&truncated, &good[..good.len() - 2048]).unwrap();
    let message = setup::choose_disc_path(&shared, &truncated).unwrap_err();
    assert!(message.contains("incomplete"), "{message}");
    let spec = root.join("spec2.iso");
    fs::write(&spec, image("1.00", "SCUS_974.36")).unwrap();
    let message = setup::choose_disc_path(&shared, &spec).unwrap_err();
    assert!(message.starts_with("This disc is not supported yet."), "{message}");
    assert_eq!(screen(&shared)["setup"]["problem"], json!(message));
    // Setup cannot start before a disc was accepted.
    let refused = setup::start_with(&shared, Recorder::default().notify(), fake_renderer()).map(|_| ()).unwrap_err();
    assert_eq!(refused, "Choose your disc image first.");

    // Accept the good disc; the page then calls setup_start.
    let shared = launch(&dirs, &pins);
    let info = setup::choose_disc_path(&shared, &picked).expect("accepted");
    assert_eq!(info.name, "Gran Turismo 4 \u{b7} USA \u{b7} v2.00");
    let recorder = Recorder::default();
    let seen = run_setup(&shared, &recorder, fake_renderer());
    assert!(seen.iter().all(|label| label.starts_with("\"checking\"")), "the page shows setup until it ends: {seen:?}");
    let view = screen(&shared);
    assert_eq!(view["setup"]["state"], "done", "{view}");
    assert_eq!(view["setup"]["note"], "");
    let copying: Vec<Value> = recorder.names("setup-progress").into_iter().filter(|event| event["step"] == "copying").collect();
    assert!(copying.iter().any(|event| event["percent"] == 100 && event["bytesDone"] == json!(good.len())), "{copying:?}");
    let renderer = recorder.names("renderer-progress");
    assert!(renderer.iter().any(|event| event["step"] == "downloading" && event["percent"] == 50), "{renderer:?}");
    assert!(renderer.iter().any(|event| event["step"] == "done"));
    assert_eq!(recorder.names("setup-finished"), vec![json!({ "ok": true, "message": "" })]);
    assert!(data.join("disc").join("SCUS-97328.iso").exists());

    // The picked file may go away: the next launch goes straight to Home, with no hash (well under the time a hash
    // of a real image takes).
    fs::remove_file(&picked).unwrap();
    let started = Instant::now();
    let second = launch(&dirs, &pins);
    assert_eq!(setup_state(&second), "done");
    assert!(started.elapsed() < Duration::from_secs(2));

    // Settings, saves, controls, about.
    let log = walk_pages(&second, &root.join("test-card.ps2"));
    assert_eq!(log.len(), 4);

    // Play: only the plan. The stand-in program is a text file; nothing is started.
    let program = root.join("program");
    fs::create_dir_all(program.join("game")).unwrap();
    fs::write(program.join("game").join("gt4vulkan.exe"), b"stand-in, never started").unwrap();
    let plan = play_plan(&second, &program).expect("plan");
    let disc_index = plan.arguments.iter().position(|argument| argument == "--disc").unwrap();
    assert_eq!(PathBuf::from(&plan.arguments[disc_index + 1]), data.join("disc").join("SCUS-97328.iso"));
    assert_eq!(PathBuf::from(&plan.arguments[0]), data.join("prepared").join("SCUS_973.28"));
    assert!(plan.environment.iter().any(|(name, value)| name == "GT4_RENDERER" && value == "donor"), "no gt4-gs installed here: CoreGS 1.1");

    // The player cleaned the data folder: Home is not shown, Play refuses, Setup offers to choose the disc again.
    fs::remove_file(data.join("disc").join("SCUS-97328.iso")).unwrap();
    let third = launch(&dirs, &pins);
    let view = screen(&third);
    assert_eq!(view["setup"]["state"], "files_missing", "{view}");
    assert!(view["setup"]["problem"].as_str().unwrap().contains("Choose your disc image again"));
    assert!(play_plan(&third, &program).unwrap_err().contains("Game files missing"));

    // Offline renderer: setup still finishes, with the note; CoreGS 1.1 is used.
    let picked_again = root.join("again.iso");
    fs::write(&picked_again, &good).unwrap();
    setup::choose_disc_path(&third, &picked_again).unwrap();
    run_setup(&third, &Recorder::default(), offline_renderer());
    let view = screen(&third);
    assert_eq!(view["setup"]["state"], "done");
    assert!(view["setup"]["note"].as_str().unwrap().contains("CoreGS 1.1"));

    // Cancel: back to a failed run with nothing half-written, and the earlier copy stays usable.
    let shared = launch(&dirs, &pins);
    setup::choose_disc_path(&shared, &picked_again).unwrap();
    // start_with clears the flag; Cancel is pressed right after the start, as the page's Cancel button would.
    let handle = setup::start_with(&shared, Recorder::default().notify(), fake_renderer()).unwrap();
    setup::cancel_with(&shared);
    handle.join().unwrap();
    let state = setup_state(&shared);
    assert!(state == "failed" || state == "done", "{state}");
    assert!(!data.join("disc").join("SCUS-97328.iso.tmp").exists());
    assert!(!data.join("prepared.tmp").exists());
    let _ = fs::remove_dir_all(&root);
}

#[test]
#[ignore = "real discs, a real copy of the image and a real download; see the module comment"]
fn real_flow_end_to_end() {
    let data = PathBuf::from(std::env::var("GT4_AUDIT_DATA_DIR").expect("set GT4_AUDIT_DATA_DIR"));
    let real = PathBuf::from(std::env::var("GT4_REAL_ISO").expect("set GT4_REAL_ISO"));
    let dirs = DataDirs::new(&data);
    let pins = Pins::embedded().unwrap();
    let mut report: Vec<String> = Vec::new();
    let mut note = |line: String| {
        println!("{line}");
        report.push(line);
    };

    let shared = launch(&dirs, &pins);
    note(format!("first launch: {}", screen(&shared)["setup"]));
    assert_ne!(setup_state(&shared), "done");

    let scratch = data.parent().unwrap().join("audit-inputs");
    fs::create_dir_all(&scratch).unwrap();
    let text = scratch.join("not-a-disc.iso");
    fs::write(&text, b"plain text, not a disc image").unwrap();
    note(format!("non-ISO: {}", setup::choose_disc_path(&shared, &text).unwrap_err()));
    let missing = scratch.join("missing.iso");
    note(format!("missing file: {}", setup::choose_disc_path(&shared, &missing).unwrap_err()));
    // A truncated copy: the first 64 MiB of the real image (holds SYSTEM.CNF and CORE.GT4, not the rest).
    let truncated = scratch.join("truncated.iso");
    {
        use std::io::{Read, Write};
        let mut input = fs::File::open(&real).unwrap().take(64 * 1024 * 1024);
        let mut output = fs::File::create(&truncated).unwrap();
        let mut buffer = Vec::new();
        input.read_to_end(&mut buffer).unwrap();
        output.write_all(&buffer).unwrap();
    }
    note(format!("truncated copy: {}", setup::choose_disc_path(&shared, &truncated).unwrap_err()));
    let _ = fs::remove_file(&truncated);
    if let Ok(spec) = std::env::var("GT4_SPEC2_ISO") {
        note(format!("Spec II: {}", setup::choose_disc_path(&shared, Path::new(&spec)).unwrap_err()));
        assert_eq!(setup_state(&shared), "wrong_disc");
    }

    let info = setup::choose_disc_path(&shared, &real).expect("the v2.00 disc is accepted");
    note(format!("accepted: {} ({})", info.name, info.serial));
    let free = gt4l_platform::disk::free_space(&dirs.disc_folder());
    note(format!("free space before the copy: {free:?} bytes"));
    let recorder = Recorder::default();
    let started = Instant::now();
    let seen = run_setup(&shared, &recorder, setup::github_renderer());
    note(format!("setup took {:.1} s; states seen: {seen:?}", started.elapsed().as_secs_f64()));
    let progress = recorder.names("setup-progress");
    note(format!("setup-progress events: {} (first {:?}, last {:?})", progress.len(), progress.first(), progress.last()));
    note(format!("renderer-progress events: {:?}", recorder.names("renderer-progress").into_iter().filter(|event| event["step"] != "downloading").collect::<Vec<_>>()));
    note(format!("setup-finished: {:?}", recorder.names("setup-finished")));
    let view = screen(&shared);
    note(format!("after setup: {}", view["setup"]));
    assert_eq!(view["setup"]["state"], "done");

    let started = Instant::now();
    let second = launch(&dirs, &pins);
    let state = setup_state(&second);
    note(format!("second launch: {state} in {} ms", started.elapsed().as_millis()));
    assert_eq!(state, "done");

    for line in walk_pages(&second, &scratch.join("test-card.ps2")) {
        note(line);
    }
    let check = crate::updates::check(&dirs, None, None);
    note(format!(
        "check_updates: {} offers, errors {:?}, gt4-gs installed {:?}",
        check.offers.len(),
        check.errors,
        check.renderer_installed.map(|version| version.text())
    ));
    if let Ok(game_dir) = std::env::var("GT4_AUDIT_GAME_DIR") {
        lock(&second).config.dev_game_dir = Some(PathBuf::from(game_dir));
        match play_plan(&second, Path::new(".")) {
            Ok(plan) => {
                note(format!("play program: {}", plan.program.display()));
                note(format!("play arguments: {:?}", plan.arguments));
                note(format!("play environment: {:?}", plan.environment));
                note(format!("play removes inherited: {:?}", plan.removed_inherited));
            }
            Err(message) => note(format!("play plan refused: {message}")),
        }
    }
    fs::write(data.parent().unwrap().join("real-flow-report.txt"), report.join("\n")).unwrap();
}

//! Compiles the Slint UI and embeds the launcher's icon and version resource (`assets/brand`).

fn main() {
    let style = slint_build::CompilerConfiguration::new().with_style("fluent-dark".to_string());
    slint_build::compile_with_config("ui/app.slint", style).expect("Slint UI compiles");

    println!("cargo:rerun-if-changed=../../assets/brand/launcher.rc");
    println!("cargo:rerun-if-changed=../../assets/brand/launcher.ico");
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("windows") {
        embed_resource::compile("../../assets/brand/launcher.rc", embed_resource::NONE)
            .manifest_optional()
            .expect("launcher icon and version resource embed");
    }
}

use std::{env, error::Error, fs, path::Path};

fn main() -> Result<(), Box<dyn Error>> {
    let arguments: Vec<String> = env::args().collect();
    if arguments.len() != 3 && arguments.len() != 4 {
        return Err("usage: gt4-wgsl-spirv <gs.wgsl> <output-directory> [scale 1-4]".into());
    }
    // The SCALE override is folded into the SPIR-V (Naga has no runtime override here), so each
    // internal resolution has its own shader directory. Scale 1 is the qualified default.
    let scale: u32 = match arguments.get(3) {
        Some(text) => text.parse()?,
        None => 1,
    };
    if !(1..=4).contains(&scale) {
        return Err("scale must be 1, 2, 3 or 4".into());
    }
    let source = fs::read_to_string(&arguments[1])?;
    let output = Path::new(&arguments[2]);
    fs::create_dir_all(output)?;
    let module = naga::front::wgsl::parse_str(&source)?;
    let information = naga::valid::Validator::new(
        naga::valid::ValidationFlags::all(),
        naga::valid::Capabilities::all(),
    )
    .validate(&module)?;
    let mut constants = naga::back::PipelineConstants::default();
    constants.insert("SCALE".to_string(), scale as f64);
    for entry in [
        "upload",
        "copy_read",
        "copy_write",
        "load_clut",
        "display",
        "raster",
        "resolve",
        "snapshot",
        "decode",
    ] {
        let (resolved_module, resolved_information) =
            naga::back::pipeline_constants::process_overrides(
                &module,
                &information,
                Some((naga::ShaderStage::Compute, entry)),
                &constants,
            )?;
        let mut options = naga::back::spv::Options::default();
        options.fake_missing_bindings = false;
        for binding in 0..8 {
            options.binding_map.insert(
                naga::ResourceBinding { group: 0, binding },
                naga::back::spv::BindingInfo {
                    descriptor_set: 0,
                    binding,
                    binding_array_size: None,
                },
            );
        }
        let pipeline = naga::back::spv::PipelineOptions {
            shader_stage: naga::ShaderStage::Compute,
            entry_point: entry.to_string(),
        };
        let words = naga::back::spv::write_vec(
            &resolved_module,
            &resolved_information,
            &options,
            Some(&pipeline),
        )?;
        let bytes: Vec<u8> = words.iter().flat_map(|word| word.to_le_bytes()).collect();
        fs::write(output.join(format!("{entry}.spv")), bytes)?;
        println!("{entry}: {} SPIR-V words", words.len());
    }
    Ok(())
}

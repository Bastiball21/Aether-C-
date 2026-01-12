use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use bullet_lib::nn::*;
use bullet_lib::trainer::*;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let dataset_dir = parse_args()?;
    validate_dataset_dir(&dataset_dir)?;

    let use_cuda = cuda_available();
    println!("CUDA available: {use_cuda}");

    let data_loader = DirectSequentialDataLoader::new(dataset_dir.clone());
    let feature_set = HalfKP::new();

    let accumulator = Accumulator::new(256, Activation::SCReLU);
    let input_size = accumulator.output_size() * 2; // STM/NTM concatenation
    let network = Network::new()
        .add(accumulator)
        .add(ConcatenateSTMNTM::new())
        .add(Affine::new(input_size, 32, Activation::CReLU))
        .add(Affine::new(32, 32, Activation::CReLU))
        .add(Affine::new(32, 1, Activation::Identity));

    let saved_format = SavedFormat::i16()
        .with_output_scales([255, 64, 64]);

    let optimizer = AdamW::new();
    let superbatches = 90;
    let lr_schedule = CosineDecaySchedule::new(0.001, 1.0e-6, superbatches);

    let device = if use_cuda {
        Device::Cuda(0)
    } else {
        Device::Cpu
    };

    let mut trainer = Trainer::builder()
        .device(device)
        .data_loader(data_loader)
        .feature_set(feature_set)
        .network(network)
        .optimizer(optimizer)
        .saved_format(saved_format)
        .build()?;

    for superbatch in 0..superbatches {
        let lr = lr_schedule.rate(superbatch);
        let loss = trainer.train_superbatch(superbatch, lr)?;
        println!(
            "superbatch {superbatch}/{superbatches} | lr={lr:.6} | loss={loss:.6}",
        );
    }

    Ok(())
}

fn parse_args() -> Result<PathBuf, Box<dyn std::error::Error>> {
    let mut args = env::args().skip(1);
    let dataset_dir = args
        .next()
        .ok_or("usage: train <dataset_dir>")?;
    Ok(PathBuf::from(dataset_dir))
}

fn validate_dataset_dir(path: &Path) -> Result<(), Box<dyn std::error::Error>> {
    if !path.is_dir() {
        return Err(format!("{path:?} is not a directory").into());
    }

    let mut has_bins = false;
    for entry in fs::read_dir(path)? {
        let entry = entry?;
        let entry_path = entry.path();
        if entry_path.extension().and_then(|ext| ext.to_str()) == Some("bin") {
            has_bins = true;
            break;
        }
    }

    if !has_bins {
        return Err(format!("no .bin files found in {path:?}").into());
    }

    Ok(())
}

fn cuda_available() -> bool {
    match Command::new("nvidia-smi").arg("-L").output() {
        Ok(output) => output.status.success(),
        Err(_) => false,
    }
}

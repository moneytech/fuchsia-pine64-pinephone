// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::{ByteOrder, NativeEndian};
use fidl_fuchsia_media::*;
use itertools::Itertools;
use stream_processor_test::*;

const PCM_SAMPLE_SIZE: usize = 2;
const PCM_MIME_TYPE: &str = "audio/pcm";

#[derive(Clone, Debug)]
pub struct PcmAudio {
    pcm_format: PcmFormat,
    buffer: Vec<u8>,
}

impl PcmAudio {
    pub fn create_saw_wave(pcm_format: PcmFormat, frame_count: usize) -> Self {
        const FREQUENCY: f32 = 20.0;
        const AMPLITUDE: f32 = 0.2;

        let pcm_frame_size = PCM_SAMPLE_SIZE * pcm_format.channel_map.len();
        let samples_per_frame = pcm_format.channel_map.len();
        let sample_count = frame_count * samples_per_frame;

        let mut buffer = vec![0; frame_count * pcm_frame_size];

        for i in 0..sample_count {
            let frame = (i / samples_per_frame) as f32;
            let value =
                ((frame * FREQUENCY / (pcm_format.frames_per_second as f32)) % 1.0) * AMPLITUDE;
            let sample = (value * i16::max_value() as f32) as i16;

            let mut sample_bytes = [0; std::mem::size_of::<i16>()];
            NativeEndian::write_i16(&mut sample_bytes, sample);

            let offset = i * PCM_SAMPLE_SIZE;
            buffer[offset] = sample_bytes[0];
            buffer[offset + 1] = sample_bytes[1];
        }

        Self { pcm_format, buffer }
    }

    fn frame_size(&self) -> usize {
        self.pcm_format.channel_map.len() * PCM_SAMPLE_SIZE
    }
}

#[allow(dead_code)]
pub struct PcmAudioStream<I, E> {
    pub pcm_audio: PcmAudio,
    pub encoder_settings: E,
    pub frames_per_packet: I,
    pub timebase: Option<u64>,
}

impl<I, E> PcmAudioStream<I, E>
where
    I: Iterator<Item = usize> + Clone,
    E: Fn() -> EncoderSettings,
{
    #[allow(dead_code)]
    fn new(
        pcm_audio: PcmAudio,
        encoder_settings: E,
        frames_per_packet: I,
        timebase: Option<u64>,
    ) -> Self {
        Self { pcm_audio, encoder_settings, frames_per_packet, timebase }
    }
}

impl<I, E> ElementaryStream for PcmAudioStream<I, E>
where
    I: Iterator<Item = usize> + Clone,
    E: Fn() -> EncoderSettings,
{
    fn format_details(&self, format_details_version_ordinal: u64) -> FormatDetails {
        FormatDetails {
            domain: Some(DomainFormat::Audio(AudioFormat::Uncompressed(
                AudioUncompressedFormat::Pcm(self.pcm_audio.pcm_format.clone()),
            ))),
            encoder_settings: Some((self.encoder_settings)()),
            format_details_version_ordinal: Some(format_details_version_ordinal),
            mime_type: Some(String::from(PCM_MIME_TYPE)),
            oob_bytes: None,
            pass_through_parameters: None,
            timebase: self.timebase,
        }
    }

    fn is_access_units(&self) -> bool {
        false
    }

    fn stream<'a>(&'a self) -> Box<dyn Iterator<Item = ElementaryStreamChunk> + 'a> {
        let data = self.pcm_audio.buffer.as_slice();
        let frame_size = self.pcm_audio.frame_size();
        let mut offset = 0;
        let mut frames_per_packet = self.frames_per_packet.clone();

        let chunks = (0..)
            .map(move |_| {
                let number_of_frames_for_this_packet = frames_per_packet.next()?;
                let payload_size = number_of_frames_for_this_packet * frame_size;
                let payload_size = data
                    .len()
                    .checked_sub(offset)
                    .map(|remaining_bytes| std::cmp::min(remaining_bytes, payload_size))
                    .filter(|payload_size| *payload_size > 0)?;

                let range = offset..(offset + payload_size);
                offset += payload_size;

                data.get(range)
            })
            .while_some();
        Box::new(chunks.map(|data| ElementaryStreamChunk {
            start_access_unit: false,
            known_end_access_unit: false,
            data,
            significance: Significance::Audio(AudioSignificance::PcmFrames),
            timestamp: None,
        }))
    }
}

#[test]
fn elementary_chunk_data() {
    let pcm_format = PcmFormat {
        pcm_mode: AudioPcmMode::Linear,
        bits_per_sample: 16,
        frames_per_second: 44100,
        channel_map: vec![AudioChannelId::Lf, AudioChannelId::Rf],
    };
    let pcm_audio = PcmAudio::create_saw_wave(pcm_format, /*frame_count=*/ 100);

    let encoder_settings = move || -> EncoderSettings {
        // Settings are arbitrary; we just need to construct an instance.
        EncoderSettings::Sbc(SbcEncoderSettings {
            sub_bands: SbcSubBands::SubBands8,
            allocation: SbcAllocation::AllocLoudness,
            block_count: SbcBlockCount::BlockCount16,
            channel_mode: SbcChannelMode::JointStereo,
            bit_pool: 59,
        })
    };
    let stream = PcmAudioStream {
        pcm_audio: pcm_audio.clone(),
        encoder_settings,
        frames_per_packet: (0..).map(|_| 40),
        timebase: None,
    };

    let actual: Vec<u8> = stream.stream().flat_map(|chunk| chunk.data.iter()).cloned().collect();
    assert_eq!(pcm_audio.buffer, actual);
}

#[test]
fn saw_wave_matches_hash() {
    use hex;
    use mundane::hash::*;

    /// This was obtained by writing the buffer out to file and inspecting the wave on each channel.
    const GOLDEN_DIGEST: &str = "2bf4f233a179f0cb572b72570a28c07a334e406baa7fb4fc65f641b82d0ae64a";

    let pcm_audio = PcmAudio::create_saw_wave(
        PcmFormat {
            pcm_mode: AudioPcmMode::Linear,
            bits_per_sample: 16,
            frames_per_second: 44100,
            channel_map: vec![AudioChannelId::Lf, AudioChannelId::Rf],
        },
        /*frame_count=*/ 50000,
    );

    let actual_digest = hex::encode(Sha256::hash(&pcm_audio.buffer).bytes());
    assert_eq!(&actual_digest, GOLDEN_DIGEST);
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/lib/test/analysis.h"

#include <fbl/algorithm.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio::test {

namespace {

constexpr double RT_2 = 1.4142135623730950488016887242;

// Local version of GenerateCosineAudio that uses doubles, the same type used by our FFT methods.
void OverwriteCosine(double* buffer, uint32_t buf_size, double freq, double magn = 1.0,
                     double phase = 0.0) {
  // If frequency is 0 (constant val), phase offset causes reduced amplitude
  FX_DCHECK(freq > 0.0 || (freq == 0.0 && phase == 0.0));

  // Freqs above buf_size/2 (Nyquist limit) will alias into lower frequencies.
  FX_DCHECK(freq * 2.0 <= buf_size) << "Buffer too short--requested frequency will be aliased";

  // freq is defined as: cosine recurs exactly 'freq' times within buf_size.
  const double mult = 2.0 * M_PI / buf_size * freq;

  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    buffer[idx] = magn * std::cos(mult * idx + phase);
  }
}

}  // namespace

TEST(AnalysisHelpers, GetPhase) {
  double reals[] = {0.5, 23, 0, -42, -0.1, -123, 0, 68, 0};
  double imags[] = {0, 23, 243, 42, 0, -123, -243, -68, 0};
  double expect[] = {0,         M_PI / 4,  M_PI / 2, 3 * M_PI / 4, M_PI, -3 * M_PI / 4,
                     -M_PI / 2, -M_PI / 4, 0};
  static_assert(fbl::count_of(imags) == fbl::count_of(reals), "buf mismatch");
  static_assert(fbl::count_of(expect) == fbl::count_of(reals), "buf mismatch");

  for (uint32_t idx = 0; idx < fbl::count_of(reals); ++idx) {
    EXPECT_DOUBLE_EQ(expect[idx], internal::GetPhase(reals[idx], imags[idx]));
  }
}

TEST(AnalysisHelpers, RectToPolar) {
  double real[] = {1.0, 1.0, 0.0, -1.0, -1.0, -1.0, 0.0, 1.0, 0.0, -0.0};
  double imag[] = {0.0, 1.0, 1.0, 1.0, -0.0, -1.0, -1.0, -1.0, 0.0, -0.0};
  double magn[10];
  double phase[10];
  const double epsilon = 0.00000001;

  internal::RectangularToPolar(real, imag, fbl::count_of(real), magn, phase);
  double expect_magn[] = {1.0, RT_2, 1.0, RT_2, 1.0, RT_2, 1.0, RT_2, 0.0, 0.0};

  double expect_phase[] = {0.0,           M_PI / 4,  M_PI / 2,  3 * M_PI / 4, M_PI,
                           -3 * M_PI / 4, -M_PI / 2, -M_PI / 4, 0.0,          0.0};

  // We used double here; below are acceptable and reliable tolerances
  for (uint32_t idx = 0; idx < fbl::count_of(expect_magn); ++idx) {
    EXPECT_LE(magn[idx], expect_magn[idx] + epsilon) << idx;
    EXPECT_GE(magn[idx], expect_magn[idx] - epsilon) << idx;

    EXPECT_LE(phase[idx], expect_phase[idx] + epsilon) << idx;
    EXPECT_GE(phase[idx], expect_phase[idx] - epsilon) << idx;
  }
}

TEST(AnalysisHelpers, RealDFT) {
  double reals[16];
  const uint32_t buf_size = fbl::count_of(reals);
  const double epsilon = 0.0000001024;

  const uint32_t buf_sz_2 = buf_size >> 1;
  double real_freq[9];
  double imag_freq[9];
  static_assert(fbl::count_of(real_freq) == buf_sz_2 + 1, "buf sizes must match");
  static_assert(fbl::count_of(imag_freq) == buf_sz_2 + 1, "buf sizes must match");

  // impulse
  OverwriteCosine(reals, buf_size, 0.0, 0.0);
  reals[0] = 1000000.0;
  internal::RealDFT(reals, buf_size, real_freq, imag_freq);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = 1000000.0;
    EXPECT_LE(real_freq[idx], expect + epsilon) << idx;
    EXPECT_GE(real_freq[idx], expect - epsilon) << idx;

    EXPECT_LE(imag_freq[idx], epsilon) << idx;
    EXPECT_GE(imag_freq[idx], -epsilon) << idx;
  }

  // DC
  OverwriteCosine(reals, buf_size, 0.0, 700000.0);
  internal::RealDFT(reals, buf_size, real_freq, imag_freq);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = (idx == 0 ? 700000.0 * static_cast<double>(buf_size) : 0.0);
    EXPECT_LE(real_freq[idx], expect + epsilon) << idx;
    EXPECT_GE(real_freq[idx], expect - epsilon) << idx;

    EXPECT_LE(imag_freq[idx], epsilon) << idx;
    EXPECT_GE(imag_freq[idx], -epsilon) << idx;
  }

  // folding freq
  OverwriteCosine(reals, buf_size, buf_size / 2.0, 1001001.0);
  internal::RealDFT(reals, buf_size, real_freq, imag_freq);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = (idx == buf_size / 2) ? (1001001.0 * static_cast<double>(buf_size)) : 0.0;
    EXPECT_LE(real_freq[idx], expect + epsilon) << idx;
    EXPECT_GE(real_freq[idx], expect - epsilon) << idx;

    EXPECT_LE(imag_freq[idx], epsilon) << idx;
    EXPECT_GE(imag_freq[idx], -epsilon) << idx;
  }

  // 1
  OverwriteCosine(reals, buf_size, 1.0, 20202020.0);
  internal::RealDFT(reals, buf_size, real_freq, imag_freq);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = (idx == 1) ? (20202020.0 * static_cast<double>(buf_size) / 2.0) : 0.0;
    EXPECT_LE(real_freq[idx], expect + epsilon) << idx;
    EXPECT_GE(real_freq[idx], expect - epsilon) << idx;

    EXPECT_LE(imag_freq[idx], epsilon) << idx;
    EXPECT_GE(imag_freq[idx], -epsilon) << idx;
  }

  // 1, with -PI/2 phase
  OverwriteCosine(reals, buf_size, 1.0, 20202020.0, -M_PI / 2.0);
  internal::RealDFT(reals, buf_size, real_freq, imag_freq);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    EXPECT_LE(real_freq[idx], epsilon) << idx;
    EXPECT_GE(real_freq[idx], -epsilon) << idx;

    const double expect = (idx == 1) ? (20202020.0 * static_cast<double>(buf_size) / 2.0) : 0.0;
    EXPECT_LE(imag_freq[idx], -expect + epsilon) << idx;
    EXPECT_GE(imag_freq[idx], -expect - epsilon) << idx;
  }
}

TEST(AnalysisHelpers, IDFT) {
  double reals[16];
  double expects[16];
  const uint32_t buf_size = fbl::count_of(reals);
  const double epsilon = 0.00000002;
  static_assert(buf_size == fbl::count_of(expects), "buf size mismatch");

  double real_freq[9];
  double imag_freq[9];
  const uint32_t buf_sz_2 = buf_size >> 1;
  static_assert(fbl::count_of(real_freq) == buf_sz_2 + 1, "buf size mismatch");
  static_assert(fbl::count_of(imag_freq) == buf_sz_2 + 1, "buf size mismatch");

  // impulse
  OverwriteCosine(real_freq, buf_sz_2 + 1, 0.0, 123.0);
  OverwriteCosine(imag_freq, buf_sz_2 + 1, 0.0, 0.0);

  internal::InverseDFT(real_freq, imag_freq, buf_size, reals);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    const double expect = (idx == 0 ? 123.0 : 0.0);
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;
  }

  // DC
  OverwriteCosine(real_freq, buf_sz_2 + 1, 0.0, 0.0);
  real_freq[0] = 4321.0 * buf_size;
  OverwriteCosine(imag_freq, buf_sz_2 + 1, 0.0, 0.0);

  internal::InverseDFT(real_freq, imag_freq, buf_size, reals);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    const double expect = 4321.0;
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;
  }

  // folding freq
  OverwriteCosine(real_freq, buf_sz_2 + 1, 0.0, 0.0);
  real_freq[buf_sz_2] = 10203.0 * buf_size;
  OverwriteCosine(imag_freq, buf_sz_2 + 1, 0.0, 0.0);

  internal::InverseDFT(real_freq, imag_freq, buf_size, reals);

  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    const double expect = (idx % 2 == 0 ? 10203.0 : -10203.0);
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;
  }

  // freq 1
  OverwriteCosine(real_freq, buf_sz_2 + 1, 0.0, 0.0);
  real_freq[1] = 20202020.0 * buf_sz_2;
  OverwriteCosine(imag_freq, buf_sz_2 + 1, 0.0, 0.0);

  OverwriteCosine(expects, buf_size, 1.0, 20202020.0);
  internal::InverseDFT(real_freq, imag_freq, buf_size, reals);

  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    EXPECT_LE(reals[idx], expects[idx] + epsilon) << idx;
    EXPECT_GE(reals[idx], expects[idx] - epsilon) << idx;
  }

  // freq 1, with 3PI/4 phase
  OverwriteCosine(real_freq, buf_sz_2 + 1, 0.0, 0.0);
  real_freq[1] = -20202020.0 / RT_2 * buf_sz_2;
  OverwriteCosine(imag_freq, buf_sz_2 + 1, 0.0, 0.0);
  imag_freq[1] = 20202020.0 / RT_2 * buf_sz_2;

  OverwriteCosine(expects, buf_size, 1.0, 20202020.0, 3.0 * M_PI / 4.0);
  internal::InverseDFT(real_freq, imag_freq, buf_size, reals);

  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    EXPECT_LE(reals[idx], expects[idx] + epsilon) << idx;
    EXPECT_GE(reals[idx], expects[idx] - epsilon) << idx;
  }
}

TEST(AnalysisHelpers, FFT) {
  double reals[16];
  double imags[16];
  const double epsilon = 0.00000015;

  const uint32_t buf_size = fbl::count_of(reals);
  static_assert(fbl::count_of(imags) == buf_size, "buf sizes must match");
  const uint32_t buf_sz_2 = buf_size >> 1;

  // Impulse input produces constant val in all frequency bins
  OverwriteCosine(reals, buf_size, 0.0, 0.0);
  reals[0] = 1000000.0;
  OverwriteCosine(imags, buf_size, 0.0, 0.0);
  internal::FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = 1000000.0;
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;

    EXPECT_LE(imags[idx], epsilon) << idx;
    EXPECT_GE(imags[idx], -epsilon) << idx;
  }

  // DC input produces val only in frequency bin 0
  OverwriteCosine(reals, buf_size, 0.0, 700000.0);
  OverwriteCosine(imags, buf_size, 0.0, 0.0);
  internal::FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = (idx == 0 ? 700000.0 * static_cast<double>(buf_size) : 0.0);
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;

    EXPECT_LE(imags[idx], epsilon) << idx;
    EXPECT_GE(imags[idx], -epsilon) << idx;
  }

  // Folding frequency (buf_size/2) produces all zeroes except N/2.
  double test_val = 1001001.0;
  OverwriteCosine(reals, buf_size, buf_sz_2, test_val);
  OverwriteCosine(imags, buf_size, 0.0, 0.0);
  internal::FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx < buf_sz_2; ++idx) {
    EXPECT_LE(reals[idx], epsilon) << idx;
    EXPECT_GE(reals[idx], -epsilon) << idx;

    EXPECT_LE(imags[idx], epsilon) << idx;
    EXPECT_GE(imags[idx], -epsilon) << idx;
  }
  EXPECT_LE(reals[buf_sz_2], (test_val * buf_size) + epsilon);
  EXPECT_GE(reals[buf_sz_2], (test_val * buf_size) - epsilon);
  EXPECT_LE(imags[buf_sz_2], epsilon);
  EXPECT_GE(imags[buf_sz_2], -epsilon);

  // Cosines that fit exactly into buf_sie should produce zero in all frequency bins except bin 1.
  test_val = 20202020.0;
  OverwriteCosine(reals, buf_size, 1.0, test_val);
  OverwriteCosine(imags, buf_size, 0.0, 0.0);
  internal::FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    const double expect = (idx == 1) ? (test_val * buf_size / 2.0) : 0.0;
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;

    EXPECT_LE(imags[idx], epsilon) << idx;
    EXPECT_GE(imags[idx], -epsilon) << idx;
  }

  // That cosine shifted by PI/2 should have identical results, flipped between real and imaginary.
  OverwriteCosine(reals, buf_size, 1.0, test_val, -M_PI / 2.0);
  OverwriteCosine(imags, buf_size, 0.0, 0.0, 0.0);
  internal::FFT(reals, imags, buf_size);

  for (uint32_t idx = 0; idx <= buf_sz_2; ++idx) {
    EXPECT_LE(reals[idx], epsilon) << idx;
    EXPECT_GE(reals[idx], -epsilon) << idx;

    const double expect = (idx == 1) ? (test_val * buf_size / 2.0) : 0.0;
    EXPECT_LE(imags[idx], -expect + epsilon) << idx;
    EXPECT_GE(imags[idx], -expect - epsilon) << idx;
  }
}

TEST(AnalysisHelpers, IFFT) {
  double reals[16];
  double imags[16];
  double expects[16];
  const uint32_t buf_size = fbl::count_of(reals);
  const uint32_t buf_sz_2 = buf_size >> 1;

  const double epsilon = 0.00000002;
  static_assert(buf_size == fbl::count_of(imags), "buf size mismatch");
  static_assert(buf_size == fbl::count_of(expects), "buf size mismatch");

  // impulse
  OverwriteCosine(reals, buf_size, 0.0, 123.0);
  OverwriteCosine(imags, buf_size, 0.0, 0.0);

  internal::InverseFFT(reals, imags, buf_size);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    const double expect = (idx == 0 ? 123.0 : 0.0);
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;

    EXPECT_LE(imags[idx], epsilon) << idx;
    EXPECT_GE(imags[idx], -epsilon) << idx;
  }

  // DC
  OverwriteCosine(reals, buf_size, 0.0, 0.0);
  reals[0] = 4321.0 * buf_size;
  OverwriteCosine(imags, buf_size, 0.0, 0.0);

  internal::InverseFFT(reals, imags, buf_size);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    const double expect = 4321.0;
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;
  }

  // folding freq
  OverwriteCosine(reals, buf_size, 0.0, 0.0);
  reals[buf_sz_2] = 10203.0 * buf_size;
  OverwriteCosine(imags, buf_size, 0.0, 0.0);

  internal::InverseFFT(reals, imags, buf_size);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    const double expect = (idx % 2 == 0 ? 10203.0 : -10203.0);
    EXPECT_LE(reals[idx], expect + epsilon) << idx;
    EXPECT_GE(reals[idx], expect - epsilon) << idx;
  }

  // freq 1
  OverwriteCosine(reals, buf_size, 0.0, 0.0);
  reals[1] = 20202020.0 * buf_size;
  OverwriteCosine(imags, buf_size, 0.0, 0.0);

  OverwriteCosine(expects, buf_size, 1.0, 20202020.0);
  internal::InverseFFT(reals, imags, buf_size);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    EXPECT_LE(reals[idx], expects[idx] + epsilon) << idx;
    EXPECT_GE(reals[idx], expects[idx] - epsilon) << idx;
  }

  // freq 1, with 3PI/4 phase
  OverwriteCosine(reals, buf_size, 0.0, 0.0);
  reals[1] = -20202020.0 / RT_2 * buf_size;
  OverwriteCosine(imags, buf_size, 0.0, 0.0);
  imags[1] = 20202020.0 / RT_2 * buf_size;

  OverwriteCosine(expects, buf_size, 1.0, 20202020.0, 3.0 * M_PI / 4.0);
  internal::InverseFFT(reals, imags, buf_size);
  for (uint32_t idx = 0; idx < buf_size; ++idx) {
    EXPECT_LE(reals[idx], expects[idx] + epsilon) << idx;
    EXPECT_GE(reals[idx], expects[idx] - epsilon) << idx;
  }
}

// MeasureAudioFreq function accepts buffer of audio data, length and the frequency at which to
// analyze audio. It returns magnitude of signal at that frequency, and combined (root-sum-square)
// magnitude of all OTHER frequencies. For inputs of magnitude 3 and 4, their combination equals 5.
TEST(AnalysisHelpers, MeasureAudioFreq_32) {
  auto format = Format::Create(fuchsia::media::AudioStreamType{
                                   .sample_format = ASF::SIGNED_24_IN_32,
                                   .channels = 1,
                                   .frames_per_second = 48000 /* unused */,
                               })
                    .take_value();

  AudioBuffer<ASF::SIGNED_24_IN_32> reals(format, 4);
  reals.samples = {5, -3, 13, -3};  // cos freq 0,1,2; mag 3,4,6; phase 0,pi,0
  double magn_signal = -54.32;      // will be overwritten
  double magn_other = 42.0;         // will be overwritten

  MeasureAudioFreq(AudioBufferSlice(&reals), 0, &magn_signal);
  EXPECT_DOUBLE_EQ(3.0, magn_signal);

  MeasureAudioFreq(AudioBufferSlice(&reals), 1, &magn_signal, &magn_other);
  EXPECT_DOUBLE_EQ(4.0, magn_signal);

  MeasureAudioFreq(AudioBufferSlice(&reals), 2, &magn_signal, &magn_other);
  EXPECT_DOUBLE_EQ(6.0, magn_signal);
  EXPECT_DOUBLE_EQ(5.0, magn_other);
}

// Test float-based MeasureAudioFreq (only needed to validate OutputProducer).
// Reals[] consists of cosines with freq 0,1,2; magnitude 3,4,6; phase 0,pi,pi.
TEST(AnalysisHelpers, MeasureAudioFreq_Float) {
  auto format = Format::Create(fuchsia::media::AudioStreamType{
                                   .sample_format = ASF::FLOAT,
                                   .channels = 1,
                                   .frames_per_second = 48000 /* unused */,
                               })
                    .take_value();

  AudioBuffer<ASF::FLOAT> reals(format, 4);
  reals.samples = {-7.0f, 9.0f, 1.0f, 9.0f};
  double magn_signal = -54.32;
  double magn_other = 42.0;

  MeasureAudioFreq(AudioBufferSlice(&reals), 0, &magn_signal);
  EXPECT_DOUBLE_EQ(3.0, magn_signal);

  MeasureAudioFreq(AudioBufferSlice(&reals), 1, &magn_signal, &magn_other);
  EXPECT_DOUBLE_EQ(4.0, magn_signal);

  MeasureAudioFreq(AudioBufferSlice(&reals), 2, &magn_signal, &magn_other);
  EXPECT_DOUBLE_EQ(6.0, magn_signal);  // Magnitude is absolute value (ignore phase)
  EXPECT_DOUBLE_EQ(5.0, magn_other);
}

}  // namespace media::audio::test
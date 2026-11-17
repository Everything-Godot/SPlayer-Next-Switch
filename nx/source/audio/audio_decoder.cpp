// SPDX-License-Identifier: AGPL-3.0
// Audio decoding (dr_libs) + playback through libnx audout.
#include "spn/audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>


#include "dr_libs/dr_flac.h"
#include "dr_libs/dr_mp3.h"
#include "dr_libs/dr_wav.h"
#include "stb/stb_vorbis.c"

namespace spn {

namespace {

std::string lowerExt(const std::string& path) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return {};
  std::string ext = path.substr(dot + 1);
  for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
  return ext;
}

}  // namespace

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

Decoder::~Decoder() { close(); }

void Decoder::close() {
  if (handle_) {
    switch (format_) {
      case Format::Mp3: {
        auto* p = static_cast<drmp3*>(handle_);
        drmp3_uninit(p);
        delete p;
        break;
      }
      case Format::Flac:
        drflac_close(static_cast<drflac*>(handle_));
        break;
      case Format::Wav: {
        auto* p = static_cast<drwav*>(handle_);
        drwav_uninit(p);
        delete p;
        break;
      }
      case Format::Vorbis: {
        auto* p = static_cast<stb_vorbis*>(handle_);
        stb_vorbis_close(p);
        break;
      }
      default:
        break;
    }
  }
  handle_ = nullptr;
  format_ = Format::None;
  sampleRate_ = 0;
  channels_ = 0;
  totalFrames_ = 0;
}

bool Decoder::open(const std::string& path) {
  close();
  const std::string ext = lowerExt(path);

  if (ext == "mp3") {
    auto* p = new drmp3();
    if (!drmp3_init_file(p, path.c_str(), nullptr)) {
      delete p;
      return false;
    }
    format_ = Format::Mp3;
    handle_ = p;
    sampleRate_ = int(p->sampleRate);
    channels_ = int(p->channels);
    totalFrames_ = drmp3_get_pcm_frame_count(p);
    return channels_ > 0;
  }
  if (ext == "flac") {
    drflac* p = drflac_open_file(path.c_str(), nullptr);
    if (!p) return false;
    format_ = Format::Flac;
    handle_ = p;
    sampleRate_ = int(p->sampleRate);
    channels_ = int(p->channels);
    totalFrames_ = p->totalPCMFrameCount;
    return channels_ > 0;
  }
  if (ext == "wav" || ext == "wave") {
    auto* p = new drwav();
    if (!drwav_init_file(p, path.c_str(), nullptr)) {
      delete p;
      return false;
    }
    format_ = Format::Wav;
    handle_ = p;
    sampleRate_ = int(p->sampleRate);
    channels_ = int(p->channels);
    totalFrames_ = p->totalPCMFrameCount;
    return channels_ > 0;
  }
  if (ext == "ogg") {
    int err = 0;
    stb_vorbis* p = stb_vorbis_open_filename(path.c_str(), &err, nullptr);
    if (!p) return false;
    const stb_vorbis_info info = stb_vorbis_get_info(p);
    format_ = Format::Vorbis;
    handle_ = p;
    sampleRate_ = int(info.sample_rate);
    channels_ = int(info.channels);
    totalFrames_ = uint64_t(stb_vorbis_stream_length_in_samples(p));
    return channels_ > 0;
  }
  return false;
}

double Decoder::durationSec() const {
  if (sampleRate_ <= 0) return 0.0;
  return double(totalFrames_) / double(sampleRate_);
}

uint64_t Decoder::readFrames(float* out, uint64_t frames) {
  if (!handle_ || frames == 0) return 0;
  switch (format_) {
    case Format::Mp3:
      return drmp3_read_pcm_frames_f32(static_cast<drmp3*>(handle_), frames, out);
    case Format::Flac:
      return drflac_read_pcm_frames_f32(static_cast<drflac*>(handle_), frames,
                                        out);
    case Format::Wav:
      return drwav_read_pcm_frames_f32(static_cast<drwav*>(handle_), frames, out);
    case Format::Vorbis: {
      // stb_vorbis hands back interleaved shorts; convert to float in place.
      auto* p = static_cast<stb_vorbis*>(handle_);
      const uint64_t ch = uint64_t(channels_ > 0 ? channels_ : 1);
      scratch_.resize(size_t(frames * ch));
      const int got = stb_vorbis_get_samples_short_interleaved(
          p, channels_, reinterpret_cast<short*>(scratch_.data()),
          int(frames * ch));
      if (got <= 0) return 0;
      for (size_t i = 0; i < size_t(got) * size_t(ch); ++i) {
        out[i] = float(scratch_[i]) / 32768.0f;
      }
      return uint64_t(got);
    }
    default:
      return 0;
  }
}

bool Decoder::seekToFrame(uint64_t frame) {
  if (!handle_) return false;
  switch (format_) {
    case Format::Mp3:
      return drmp3_seek_to_pcm_frame(static_cast<drmp3*>(handle_), frame);
    case Format::Flac:
      return drflac_seek_to_pcm_frame(static_cast<drflac*>(handle_), frame);
    case Format::Wav:
      return drwav_seek_to_pcm_frame(static_cast<drwav*>(handle_), frame);
    case Format::Vorbis:
      return stb_vorbis_seek_frame(static_cast<stb_vorbis*>(handle_),
                                   (unsigned int)frame) != 0;
    default:
      return false;
  }
}

}  // namespace spn

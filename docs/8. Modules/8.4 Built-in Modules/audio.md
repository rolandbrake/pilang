# audio Module

The `audio` module loads sound clips and plays them through the native SDL_mixer
backend. It is intended for short sound effects and simple game audio.

```swift
import audio

let clip = audio.load("sounds/jump.wav")
audio.play(clip)

while audio.is_playing(clip)
    sleep(10)
```

Playback is asynchronous: `audio.play` starts a channel and returns
immediately. Keep the program or game loop running while a clip is playing.

## Loading

### `audio.load(path)`

Loads an audio file and returns an `audio` clip object. WAV files are supported;
MP3 and OGG are also available when their SDL_mixer decoders are present in the
native build.

```swift
let effect = audio.load("sounds/coin.ogg")
```

Relative paths are resolved from the directory containing the executing `.pi`
file. Absolute paths are used unchanged.

## Clip Metadata

### `audio.sample_rate(clip)`

Returns the clip sample rate in Hz.

### `audio.channels(clip)`

Returns the number of audio channels, such as `1` for mono or `2` for stereo.

### `audio.frames(clip)`

Returns the number of sample frames in the clip.

### `audio.duration(clip)`

Returns the clip duration in seconds.

### `audio.format(clip)`

Returns the in-memory sample format: `"f32"` for float samples or `"pcm"` for
the mixer-provided PCM format.

```swift
println(audio.sample_rate(clip))
println(audio.channels(clip))
println(audio.duration(clip))
```

## Playback

### `audio.play(clip, loops = 0)`

Starts playback and returns the SDL_mixer channel number. `loops` is the number
of additional repeats: `0` plays once, `1` plays twice, and `-1` loops until it
is stopped.

Calling `play` on a clip that is already active restarts that clip.

```swift
audio.play(clip)      // play once
audio.play(clip, 2)   // play three times total
audio.play(clip, -1)  // loop forever
```

### `audio.pause(clip)`

Pauses the active playback channel. Returns `true` when the clip had an active
channel, otherwise `false`.

### `audio.resume(clip)`

Resumes the clip's active playback channel. Returns `true` when the clip had an
active channel, otherwise `false`.

### `audio.stop(clip)`

Stops the clip's active playback channel. Returns `true` when playback was
stopped, otherwise `false`.

### `audio.is_playing(clip)`

Returns `true` while the clip is playing or paused on its active channel, and
`false` once it has finished or been stopped.

```swift
audio.play(clip, -1)
sleep(1000)
audio.pause(clip)
sleep(500)
audio.resume(clip)
sleep(1000)
audio.stop(clip)
```

## Streaming music

Use streaming playback for long music tracks. Unlike `audio.load`, streaming
music is not decoded into a full in-memory sound effect.

```swift
audio.play_music("music/theme.ogg")
if !audio.music_isPlaying() {
    audio.play_music("music/next_theme.ogg")
}
audio.stop_music()
```

`audio.play_music(path, loops = 0)` accepts the same loop count as
`audio.play`; `-1` loops indefinitely.

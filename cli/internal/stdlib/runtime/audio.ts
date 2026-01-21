// ============================================================================
// Audio API
// ============================================================================
// Provides a high-level interface for playing sounds in the game.
// Wraps the low-level host functions with a convenient class-based API.
// ============================================================================

/**
 * Sound class for playing audio in the game.
 * 
 * Usage:
 *   let sound = new Sound("assets/sounds/explosion.wav");
 *   sound.play(1.0, false);  // Play once at full volume
 *   sound.play(0.5, true);  // Play looping at half volume
 *   sound.stop();           // Stop playback
 */
class Sound {
  private name: string;
  private id: u32 = 0;
  private _gain: f32 = 1.0;

  /**
   * Creates a new Sound instance.
   * @param name - The asset path to the sound file (e.g., "assets/sounds/explosion.wav")
   */
  constructor(name: string) {
    this.name = name;
  }

  /**
   * Plays the sound.
   * @param gain - Volume level (0.0 to 1.0, default: 1.0)
   * @param looping - Whether to loop the sound (default: false)
   * @returns The sound instance ID for controlling playback
   */
  public play(gain: f32 = 1.0, looping: boolean = false): u32 {
    const utf8 = String.UTF8.encode(this.name, true);
    const view = Uint8Array.wrap(utf8);
    this.id = __jmPlaySound(<i32>view.dataStart, view.length - 1, gain, looping ? 1 : 0);
    return this.id;
  }

  /**
   * Stops the currently playing sound instance.
   * Does nothing if the sound is not playing.
   */
  public stop(): void {
    if (this.id !== 0) {
      __jmStopSound(this.id);
      this.id = 0; // Reset ID after stopping
    }
  }

  /**
   * Fades out the sound over the specified duration.
   * @param durationInSeconds - How long the fade should take
   */
  public fadeOut(durationInSeconds: f32): void {
    if (this.id !== 0) {
      __jmFadeOutSound(this.id, durationInSeconds);
    }
  }

  /**
   * Gets the current gain (volume) level.
   */
  public get gain(): f32 {
    return this._gain;
  }

  /**
   * Sets the gain (volume) level for the currently playing sound.
   * @param gain - Volume level (0.0 to 1.0)
   */
  public set gain(gain: f32) {
    if (this.id !== 0) {
      this._gain = gain;
      __jmSetGainSound(this.id, gain);
    } else {
      // Store gain for next play() call
      this._gain = gain;
    }
  }

  /**
   * Gets the current sound instance ID.
   * Returns 0 if the sound is not currently playing.
   */
  public get instanceId(): u32 {
    return this.id;
  }
}
// ============================================================================
// Console API
// ============================================================================
// Provides console logging functionality for scripts.
// Messages are sent to the engine's log output.
// ============================================================================

/**
 * Console class for logging messages from scripts.
 * 
 * Usage:
 *   console.log("Hello, world!");
 *   console.log("Value: ", value.toString());
 */
class console {
  /**
   * Logs a message to the engine console.
   * Multiple arguments are concatenated into a single message.
   * 
   * @param messages - One or more strings to log
   * 
   * Example:
   *   console.log("Player health: ", health.toString());
   */
  static log(...messages: string[]): void {
    const message = messages.join("");
    const utf8 = String.UTF8.encode(message, true);
    const view = Uint8Array.wrap(utf8);
    __jmLog(view.dataStart, view.length - 1);
  }
}
